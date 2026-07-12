// ============================================================================
// tests/integration/loader.cpp
// Minimal, dependency-free survival host: loads the cross-compiled plugin DLL
// and drives the core lifecycle the way the game does (Startup -> DrawInit ->
// Draw -> Shutdown), then exits 0. No assertions — it just has to run without
// crashing. Reused by the survival runners (run_fuzz.sh, run_persist_test.sh),
// which invoke it repeatedly over fuzzed configs / perturbed settings and check
// only that it survives. The *asserted* lifecycle check is tests/smoke_test.cpp.
//
//   x86_64-w64-mingw32-g++ loader.cpp -o loader.exe
//   wine loader.exe mxbmrp3_test.dlo
// ============================================================================
#include <windows.h>
#include <cstdio>

typedef int  (*PFN_Startup)(char*);
typedef void (*PFN_Shutdown)();
typedef int  (*PFN_DrawInit)(int*, char**, int*, char**);
typedef void (*PFN_Draw)(int, int*, void**, int*, void**);

int main(int argc, char** argv) {
    const char* dll = (argc > 1) ? argv[1] : "mxbmrp3_test.dlo";

    printf("[loader] LoadLibrary(%s)\n", dll);
    HMODULE h = LoadLibraryA(dll);
    if (!h) { printf("[loader] FAIL: LoadLibrary error %lu\n", GetLastError()); return 1; }
    printf("[loader] ok: DLL loaded at %p\n", (void*)h);

    auto Startup  = (PFN_Startup)  GetProcAddress(h, "Startup");
    auto Shutdown = (PFN_Shutdown) GetProcAddress(h, "Shutdown");
    auto DrawInit = (PFN_DrawInit) GetProcAddress(h, "DrawInit");
    auto Draw     = (PFN_Draw)     GetProcAddress(h, "Draw");
    if (!Startup || !Shutdown) { printf("[loader] FAIL: missing required exports\n"); return 1; }
    printf("[loader] ok: resolved Startup/Shutdown/DrawInit/Draw\n");

    // Wine maps Z: to the unix root; caller creates this dir.
    char savePath[] = "Z:\\tmp\\mxbsave\\";
    printf("[loader] calling Startup(\"%s\") ...\n", savePath);
    int rate = Startup(savePath);
    printf("[loader] ok: Startup() returned telemetry-rate enum = %d\n", rate);

    if (DrawInit) {
        int ns = 0, nf = 0; char* sn = nullptr; char* fn = nullptr;
        DrawInit(&ns, &sn, &nf, &fn);
        printf("[loader] ok: DrawInit() -> %d sprites, %d fonts\n", ns, nf);
    }
    if (Draw) {
        int nq = 0, nstr = 0; void* q = nullptr; void* s = nullptr;
        Draw(0, &nq, &q, &nstr, &s);
        printf("[loader] ok: Draw() -> %d quads, %d strings\n", nq, nstr);
    }

    printf("[loader] calling Shutdown() ...\n");
    Shutdown();
    printf("[loader] ok: Shutdown() returned\n");
    printf("[loader] LOADER OK\n");
    return 0;
}
