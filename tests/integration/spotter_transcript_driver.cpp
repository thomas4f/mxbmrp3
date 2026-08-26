// ============================================================================
// tests/integration/spotter_transcript_driver.cpp
// Dev driver: replay a real recorded .tape with the SHIPPED spotter pack
// installed, so the cues it produces can be READ — the wording, the ordering,
// and how much of it there is — against the events that produced them.
//
//   ./build.sh
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 -w -static -static-libgcc \
//       -static-libstdc++ -I harness -I ../../mxbmrp3 -I ../../mxbmrp3/vendor \
//       spotter_transcript_driver.cpp -o build/spotter_transcript_driver.exe -lws2_32
//   gunzip -c tests/fixtures/race_farm14_24riders.tape.gz > /tmp/farm14.tape
//   ( . ./wine_env.sh; mxb_wine_env; cd build && wine spotter_transcript_driver.exe \
//       mxbmrp3_test.dlo 'Z:\tmp\farm14.tape' 'Z:\...\default.ini' )
//   grep 'SPOTTER SAY' /tmp/mxbspotter/mxbmrp3/mxbmrp3_log.txt
//
// Why this and not an assertion: the census tests can prove every cue key
// exists, is documented and is reachable, and say NOTHING about whether the
// sentence is worth hearing. A real 24-rider race is what shows that a third of
// the callouts are other riders' track-limits penalties, or that the position
// report was reading a running total. Reading it is the point; there is no
// oracle to automate.
//
// Needs the TEST build (the transcript log line is #ifdef MXBMRP3_TEST_BUILD).
// Reads the pack ini as raw text and installs it via the test hook, so no pack
// staging under the working directory is needed.
// ============================================================================
#include "plugin_host.h"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const char* dll  = argc > 1 ? argv[1] : "mxbmrp3_test.dlo";
    const char* tape = argc > 2 ? argv[2] : "Z:\\tmp\\mp.tape";
    const char* pack = argc > 3 ? argv[3] : nullptr;

    std::string packIni = "[Cues]\n";
    if (pack) {
        FILE* f = fopen(pack, "rb");
        if (!f) { fprintf(stderr, "cannot open pack %s\n", pack); return 1; }
        std::vector<char> buf;
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
            buf.insert(buf.end(), chunk, chunk + n);
        fclose(f);
        packIni.assign(buf.begin(), buf.end());
        fprintf(stderr, "pack: %s (%zu bytes)\n", pack, packIni.size());
    }

    PluginHost host(dll);
    if (!host.loaded()) { fprintf(stderr, "failed to load %s\n", dll); return 1; }
    host.startup("Z:\\tmp\\mxbspotter\\");
    host.spotterInstallPack(packIni.c_str());
    // Subtitles only: intake runs, the transcript logs, no audio is attempted
    // (a Wine prefix has no SAPI voice, and audio is not what we're reading).
    host.spotterEnable(false);
    host.spotterSubtitles(true);
    host.spotterCategoryMask(0x1F);

    const int applied = host.replayTape(tape);
    fprintf(stderr, "replayed %d events from %s\n", applied, tape);
    return applied > 0 ? 0 : 3;
}
