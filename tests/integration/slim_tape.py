#!/usr/bin/env python3
# ============================================================================
# tests/integration/slim_tape.py
# Derive a small, committable callback-tape *fixture* from a full master capture
# (the in-plugin recorder's MXBHREC format). A master records every callback; a fixture
# keeps only the event types the test that uses it needs, so the file is tiny.
#
# Slimming is PER-TEST: keep the streams that test exercises, drop the rest.
# The master is one-shot — never slim it in place; slim into a new file and keep
# the master (see tests/integration/tapes/).
#
#   python3 slim_tape.py MASTER.tape OUT.tape --profile min
#   python3 slim_tape.py MASTER.tape OUT.tape --keep 3,15,17,24 --stats
#   python3 slim_tape.py MASTER.tape OUT.tape --profile gaps --every 25:12,24:8 --near 21:250
#
# --every thins a dense stream; --near exempts the moments that matter from the
# thinning. A fixture that pins what happens AT THE LINE needs every position
# around each RaceLap: thinned to every 12th, the kept sample nearest the
# crossing decides whether the wrap or the RaceLap comes first, and the
# gap-to-PB golden had 3 of its 11 lap ends flipped that way -- passing over a
# bug the master's order hits four laps running.
#
# Profiles (each is a KEEP set of event-type ids):
#   min    snapshot state-changers only (standings/session/events). ~tiny.
#          Used by replay_golden_test.
#   gaps   min + splits/holeshot/track-position — for live gaps, best sectors,
#          posDeltaStart/Split, map geometry. Bigger (track positions are dense).
#   all    everything except Draw (33k/session of pure render spam) — for
#          telemetry / vehicle-data / FMX tests. Largest.
#   full   verbatim copy (drops nothing).
# Gzip the output before committing to tests/integration/tests/fixtures/.
# ============================================================================
import argparse
import collections
import struct
import sys

NAMES = {1:'Startup',2:'Shutdown',3:'EventInit',4:'EventDeinit',5:'RunInit',
6:'RunDeinit',7:'RunStart',8:'RunStop',9:'RunLap',10:'RunSplit',11:'RunTelemetry',
12:'DrawInit',13:'Draw',14:'TrackCenterline',15:'RaceEvent',16:'RaceDeinit',
17:'RaceSession',18:'RaceSessionState',19:'RaceAddEntry',20:'RaceRemoveEntry',
21:'RaceLap',22:'RaceSplit',23:'RaceHoleshot',24:'RaceClassification',
25:'RaceTrackPosition',26:'RaceCommunication',27:'RaceVehicleData'}
HDR = 72  # sizeof(FileHeader), default alignment — see harness/tape.h

# Snapshot state-changers (what replayTape applies that reaches /api/state).
MIN = {3, 15, 17, 18, 19, 20, 21, 24, 26}
PROFILES = {
    'min':  MIN,
    'gaps': MIN | {22, 23, 25},                    # + splits, holeshot, track positions
    'all':  set(NAMES) - {13},                     # everything except Draw
    'full': set(NAMES),                            # verbatim
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--profile', choices=PROFILES, default='min')
    ap.add_argument('--keep', help='explicit comma-separated event-type ids (overrides profile)')
    ap.add_argument('--stats', action='store_true', help='print the kept/dropped histogram')
    ap.add_argument('--every', help='subsample dense kept types: TYPE:N[,TYPE:N] keeps every Nth '
                                    'event of that type (e.g. 25:12,24:8). Order is preserved; '
                                    'a fixture that pins a rate-independent property can shed '
                                    'most of its per-frame positions and classifications this way')
    ap.add_argument('--near', help='exempt from --every anything within MS of an event of TYPE: '
                                   'TYPE:MS[,TYPE:MS] (e.g. 21:250 keeps every position around '
                                   'each RaceLap, so the order at the line is the game\'s)')
    a = ap.parse_args()

    keep = set(int(x) for x in a.keep.split(',')) if a.keep else PROFILES[a.profile]
    every = {}
    if a.every:
        for item in a.every.split(','):
            t, n = item.split(':'); every[int(t)] = int(n)
    near = {}
    if a.near:
        for item in a.near.split(','):
            t, ms = item.split(':'); near[int(t)] = int(ms) * 1000   # tape timestamps are us
    seen = collections.Counter()
    data = open(a.src, 'rb').read()
    if data[:7] != b'MXBHREC':
        sys.exit(f'{a.src}: not a MXBHREC tape')

    # The anchors' timestamps, so the main pass can ask "is this within MS of one".
    anchors = []
    if near:
        off = HDR
        while off + 16 <= len(data):
            et, sz, ts = struct.unpack_from('<IIQ', data, off)
            if et in near:
                anchors.append((ts - near[et], ts + near[et]))
            off += 16 + sz
    def exempt(ts):
        return any(lo <= ts <= hi for lo, hi in anchors)

    out = bytearray(data[:HDR])
    off, kept = HDR, 0
    khist, dhist = collections.Counter(), collections.Counter()
    while off + 16 <= len(data):
        et, sz, ts = struct.unpack_from('<IIQ', data, off)
        rec = data[off:off + 16 + sz]
        off += 16 + sz
        if et in keep:
            seen[et] += 1
            if et in every and (seen[et] - 1) % every[et] != 0 and not exempt(ts):
                dhist[et] += 1
                continue
            out += rec; kept += 1; khist[et] += 1
        else:
            dhist[et] += 1
    struct.pack_into('<I', out, 12, kept)           # patch numEvents
    open(a.dst, 'wb').write(out)

    print(f'{a.src} ({len(data)} B) -> {a.dst} ({len(out)} B), {kept} events kept '
          f'(profile={a.profile})')
    if a.stats:
        print('  kept:'); [print(f'    {NAMES.get(t,t):20s} {c}') for t, c in sorted(khist.items())]
        print('  dropped:'); [print(f'    {NAMES.get(t,t):20s} {c}') for t, c in sorted(dhist.items())]

if __name__ == '__main__':
    main()
