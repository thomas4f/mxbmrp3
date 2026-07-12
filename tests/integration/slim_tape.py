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
import sys, struct, argparse, collections

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
    a = ap.parse_args()

    keep = set(int(x) for x in a.keep.split(',')) if a.keep else PROFILES[a.profile]
    data = open(a.src, 'rb').read()
    if data[:7] != b'MXBHREC':
        sys.exit(f'{a.src}: not a MXBHREC tape')

    out = bytearray(data[:HDR])
    off, kept = HDR, 0
    khist, dhist = collections.Counter(), collections.Counter()
    while off + 16 <= len(data):
        et, sz, _ = struct.unpack_from('<IIQ', data, off)
        rec = data[off:off + 16 + sz]
        off += 16 + sz
        if et in keep:
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
