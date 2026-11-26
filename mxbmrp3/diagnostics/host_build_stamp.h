// ============================================================================
// diagnostics/host_build_stamp.h
// The game's build fingerprint: the PE TimeDateStamp of the host .exe.
//
// The PiBoSo games ship no VERSIONINFO resource, so there is no FileVersion to
// read. The link timestamp is the same value the Windows Application event log
// reports as the faulting image's "time stamp" (e.g. 0x6a21833d), and a
// mxbikes.exe+offset fault only means something against one build, so the log
// and the crash beacon both carry it.
//
// WHY FROM DISK. The plugin reads nothing of the game's memory, its own image
// headers included: this opens the .exe file the process was started from,
// read-only and shared, and reads the two headers it needs. It is read once
// and cached, so the crash filter (no heap, no file I/O) only copies a number.
//
// Pinned by environment_report_test.cpp's "the game's build" case.
// ============================================================================
#pragma once

#include <windows.h>

namespace HostBuildStamp {

// Reads the stamp from the file at `path`; 0 when it can't be read.
inline unsigned long readFromFile(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;

    unsigned long stamp = 0;
    DWORD got = 0;
    IMAGE_DOS_HEADER dos = {};
    if (ReadFile(file, &dos, sizeof(dos), &got, nullptr) && got == sizeof(dos) &&
        dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0) {
        LARGE_INTEGER at = {};
        at.QuadPart = dos.e_lfanew;
        DWORD signature = 0;
        IMAGE_FILE_HEADER header = {};
        if (SetFilePointerEx(file, at, nullptr, FILE_BEGIN) &&
            ReadFile(file, &signature, sizeof(signature), &got, nullptr) && got == sizeof(signature) &&
            signature == IMAGE_NT_SIGNATURE &&
            ReadFile(file, &header, sizeof(header), &got, nullptr) && got == sizeof(header)) {
            stamp = header.TimeDateStamp;
        }
    }
    CloseHandle(file);
    return stamp;
}

// The host .exe's stamp, read on the first call and cached; 0 if unreadable.
// Call it first from startup (the game thread), never first from the crash filter.
inline unsigned long get() {
    static const unsigned long s_stamp = [] {
        wchar_t path[MAX_PATH] = { 0 };
        const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        return (len > 0 && len < MAX_PATH) ? readFromFile(path) : 0UL;
    }();
    return s_stamp;
}

}  // namespace HostBuildStamp
