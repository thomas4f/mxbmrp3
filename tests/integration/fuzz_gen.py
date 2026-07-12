#!/usr/bin/env python3
# ============================================================================
# tests/integration/fuzz_gen.py <baseline_settings.ini> <out_dir>
# Generates a corpus of adversarial config files for the config fuzzer. Each
# case is a full save folder (a mxbmrp3/ dir) the plugin will load on Startup:
#   - settings.ini mutations target the ~170 std::stoul/stoi/stof parse sites
#     and the isfinite guards (the "one bad value aborts the whole load" and
#     "+Inf corrupts a saved float" bug classes).
#   - malformed JSON targets the six user-editable JSON files (nlohmann throws
#     on bad input; every load site must be guarded).
# The fuzzer runs each case under Wine and asserts the plugin does not crash.
# ============================================================================
import sys, os, re, random

BASE = open(sys.argv[1], "r", encoding="utf-8", errors="replace").read()
OUT = sys.argv[2]
random.seed(1234)  # deterministic corpus (Date/random are fine here, not in workflows)

JSON_FILES = [
    "mxbmrp3_stats.json", "mxbmrp3_odometer_data.json", "mxbmrp3_personal_bests.json",
    "mxbmrp3_rumble_profiles.json", "mxbmrp3_tracked_riders.json", "mxbmrp3_analytics.json",
]

def mutate_values(text, fn):
    # Replace the value after each 'key=value' with fn(value), preserving section
    # headers and comments.
    out = []
    for line in text.splitlines():
        m = re.match(r"^([^;#\[][^=]*)=(.*)$", line)
        if m:
            out.append(f"{m.group(1)}={fn(m.group(2))}")
        else:
            out.append(line)
    return "\n".join(out)

# --- settings.ini mutations (name -> bytes) ----------------------------------
settings_cases = {
    "ini_values_garbage":   mutate_values(BASE, lambda v: "GARBAGE").encode(),
    "ini_values_bigint":    mutate_values(BASE, lambda v: "99999999999999999999999999").encode(),
    "ini_values_negbig":    mutate_values(BASE, lambda v: "-99999999999999999999").encode(),
    "ini_values_inf":       mutate_values(BASE, lambda v: "inf").encode(),
    "ini_values_nan":       mutate_values(BASE, lambda v: "nan").encode(),
    "ini_values_alpha":     mutate_values(BASE, lambda v: "abc").encode(),
    "ini_hex_bad":          re.sub(r"0x[0-9A-Fa-f]+", "0xZZZZZZZZ", BASE).encode(),
    "ini_empty":            b"",
    "ini_truncated":        BASE[: max(1, len(BASE)//10)].encode(),
    "ini_binary":           bytes(random.randrange(256) for _ in range(512)),
    "ini_nul_injected":     BASE.replace("=", "=\x00", 20).encode(),
    "ini_longvalue":        mutate_values(BASE, lambda v: "A"*100000).encode(),
    "ini_no_sections":      "\n".join(l for l in BASE.splitlines() if not l.startswith("[")).encode(),
    "ini_no_equals":        "\n".join(l.replace("=", " ") for l in BASE.splitlines()).encode(),
    "ini_dup_lines":        "\n".join(l for l in BASE.splitlines() for _ in range(2)).encode(),
    "ini_crlf_spam":        (BASE.replace("\n", "\r\n\r\n")).encode(),
}

# --- malformed JSON payloads (planted into all six files per case) -----------
json_payloads = {
    "json_truncated":  b'{"a":',
    "json_wrong_root": b'[]',
    "json_nan_inf":    b'{"x": NaN, "y": Infinity, "z": -Infinity}',
    "json_huge_num":   b'{"n": 1e400, "big": 999999999999999999999999999999}',
    "json_binary":     bytes(random.randrange(256) for _ in range(256)),
    "json_empty":      b"",
    "json_deep_nest":  b"[" * 2000 + b"]" * 2000,
}

def write_case(name, settings_bytes, json_map):
    d = os.path.join(OUT, name, "mxbmrp3")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "mxbmrp3_settings.ini"), "wb") as f:
        f.write(settings_bytes)
    for fname, payload in json_map.items():
        with open(os.path.join(d, fname), "wb") as f:
            f.write(payload)

names = []
# settings cases: mutated ini + valid (absent) json
for name, data in settings_cases.items():
    write_case(name, data, {})
    names.append(name)
# json cases: valid baseline ini + the same malformed payload in every json file
for name, payload in json_payloads.items():
    write_case(name, BASE.encode(), {f: payload for f in JSON_FILES})
    names.append(name)

with open(os.path.join(OUT, "cases.txt"), "w") as f:
    f.write("\n".join(names) + "\n")
print(f"generated {len(names)} cases into {OUT}")
