#!/usr/bin/env bash
# ============================================================================
# tools/virustotal_scan.sh - upload release files to VirusTotal, wait for the
# verdicts, and write one Markdown line per file.
#
#   VT_API_KEY=... tools/virustotal_scan.sh [--strict] <out.md> <file>...
#
# Run by release.yml (a real, public release: the links go into the draft
# release notes before you press Publish) and by virustotal.yml (a manual
# re-scan of an already-published release, which is also how the API key and
# this whole path are tested without cutting a release).
#
# For each file it writes one row to <out.md>, for the release notes:
#   | `name` | [Scan report](https://www.virustotal.com/gui/file/<sha256>) |
# The link is built from the file's SHA-256, so it is the file's permanent
# VirusTotal page (it keeps showing the latest re-scan), not one analysis. The
# notes carry the link, never a count: a count is frozen at release time while
# the page behind the link moves on as engines fix their false positives.
# The counts go to the job summary instead, for you, before you publish.
# Detections are WARNINGS there, never a failure: a single
# machine-learning hit on a fresh unsigned installer is routine, and blocking a
# release on it would hand the release button to the noisiest engine. Read the
# summary before you publish.
#
# Without --strict (release.yml) an API problem - no key, quota, outage - is a
# warning (the file gets no row in the notes, "not scanned" in the summary),
# so VirusTotal being down can never stop a release. With --strict (virustotal.yml) any API problem fails
# the run: that workflow exists to prove the key works.
#
# EVALUATED: crazy-max/ghaction-virustotal. It only uploads; it does not wait
# for or report the verdict, it links the per-analysis page rather than the
# file's permanent one, and it edits release notes only when triggered by a
# `release` event, which our workflow_dispatch release never is. Its whole
# useful part is the one POST below, so a third-party action with our API key
# bought nothing. Revisit if it learns to report verdicts.
#
# Public API limits: 4 requests/minute, 500/day. Every request below goes
# through vt_get/vt_post, which space themselves REQ_GAP seconds apart.
# ============================================================================
set -euo pipefail

STRICT=0
if [[ "${1:-}" == "--strict" ]]; then STRICT=1; shift; fi
if [[ $# -lt 2 ]]; then
    echo "usage: VT_API_KEY=... $0 [--strict] <out.md> <file>..." >&2
    exit 2
fi
OUT=$1; shift

API=${VT_API_URL:-https://www.virustotal.com/api/v3}   # override = local mock only
REQ_GAP=${VT_REQ_GAP:-16}          # 60 s / 4 requests, plus a second of slack
POLL_LIMIT=${VT_POLL_LIMIT:-900}   # seconds to wait for all verdicts
SUMMARY=${GITHUB_STEP_SUMMARY:-/dev/null}
last_req=0

problem() {   # API trouble: fatal under --strict, else a warning
    if [[ $STRICT -eq 1 ]]; then echo "::error::$1" >&2; exit 1; fi
    echo "::warning::$1" >&2
}

pace() {
    local now wait
    now=$(date +%s)
    wait=$(( last_req + REQ_GAP - now ))
    if (( wait > 0 )); then sleep "$wait"; fi
    last_req=$(date +%s)
}

# Every request has a deadline: a VirusTotal that never answers must not hang
# the release job until its timeout. 300 s covers a 32 MB upload.
CURL=(curl -sS --fail-with-body --connect-timeout 20 --max-time 300 -H "x-apikey: ${VT_API_KEY:-}")
vt_get()  { pace; "${CURL[@]}" "$1"; }
vt_post() { pace; "${CURL[@]}" -F "file=@$2" "$1"; }

: > "$OUT"
files=("$@"); names=(); shas=(); ids=()
for f in "${files[@]}"; do
    [[ -f "$f" ]] || { echo "::error::$f: no such file" >&2; exit 2; }
    names+=("$(basename "$f")")
    shas+=("$(sha256sum "$f" | cut -d' ' -f1)")
    ids+=("")
done

if [[ -z "${VT_API_KEY:-}" ]]; then
    problem "VT_API_KEY is not set - files not scanned"
else
    # 1. Upload. Above 32 MB the API wants a one-off upload URL first.
    for i in "${!names[@]}"; do
        f=${files[i]}
        url=$API/files
        if (( $(stat -c %s "$f") > 32 * 1024 * 1024 )); then
            if ! url=$(vt_get "$API/files/upload_url" | jq -er '.data' 2>/dev/null); then
                problem "${names[i]}: no upload URL"; continue
            fi
        fi
        # Every parse is guarded: a 200 that is not the JSON we expect (an HTML
        # maintenance page, say) is an API problem like any other, never a
        # set -e exit from jq.
        if ! resp=$(vt_post "$url" "$f"); then
            problem "${names[i]}: upload failed: $resp"; continue
        fi
        if ! id=$(jq -er '.data.id // empty' <<<"$resp" 2>/dev/null); then
            problem "${names[i]}: unexpected upload response"; continue
        fi
        ids[i]=$id
        echo "uploaded ${names[i]} (sha256 ${shas[i]})"
    done

    # 2. Wait for each analysis to complete, in order.
    deadline=$(( $(date +%s) + POLL_LIMIT ))
    for i in "${!names[@]}"; do
        [[ -n "${ids[i]}" ]] || continue
        while :; do
            if ! resp=$(vt_get "$API/analyses/${ids[i]}"); then
                problem "${names[i]}: analysis lookup failed: $resp"; ids[i]=""; break
            fi
            if ! status=$(jq -er '.data.attributes.status' <<<"$resp" 2>/dev/null); then
                problem "${names[i]}: unexpected analysis response"; ids[i]=""; break
            fi
            [[ $status == completed ]] && break
            if (( $(date +%s) > deadline )); then
                problem "${names[i]}: no verdict within ${POLL_LIMIT}s"; ids[i]=""; break
            fi
        done
        # Keep the counts plus the name and verdict of every engine that flagged
        # it - the analysis already carries them, so this costs no request.
        [[ -n "${ids[i]}" ]] || continue
        if ! ids[i]=$(jq -ec '.data.attributes as $a | $a.stats + {flagged: [
                ($a.results // {})[] | select(.category == "malicious" or .category == "suspicious")
                | "\(.engine_name): \(.result)"] | join(", ")}
                | select(.malicious != null and .undetected != null)' <<<"$resp" 2>/dev/null); then
            problem "${names[i]}: analysis has no verdict counts"; ids[i]=""
        fi
    done
fi

# 3. Report. ids[] now holds each file's stats JSON, or "" when it was not scanned.
{
    echo "### VirusTotal"
    echo
    echo "| File | Flagged by | SHA-256 |"
    echo "|------|------------|---------|"
} >> "$SUMMARY"
for i in "${!names[@]}"; do
    link="https://www.virustotal.com/gui/file/${shas[i]}"
    if [[ -n "${ids[i]}" ]]; then
        bad=$(jq '.malicious + .suspicious' <<<"${ids[i]}")
        total=$(jq '.malicious + .suspicious + .undetected + .harmless' <<<"${ids[i]}")
        echo "| \`${names[i]}\` | [Scan report]($link) |" >> "$OUT"
        echo "| \`${names[i]}\` | [$bad of $total]($link) | \`${shas[i]}\` |" >> "$SUMMARY"
        if (( bad > 0 )); then
            echo "::warning::${names[i]}: flagged by $bad of $total engines ($(jq -r '.flagged' <<<"${ids[i]}")) - $link"
        fi
    else
        echo "| \`${names[i]}\` | not scanned | \`${shas[i]}\` |" >> "$SUMMARY"
    fi
done
cat "$OUT"
