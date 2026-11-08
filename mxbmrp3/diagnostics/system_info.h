// ============================================================================
// diagnostics/system_info.h
// The machine the plugin runs on: Windows (or Wine/Proton) version, CPU model
// and cores, physical memory. One home for what the usage survey, the
// Benchmark report and the log's environment block each need, so the three
// never disagree about the same PC.
//
// Every call reads Windows (registry, kernel32) and allocates at most one
// string: call it from one-time code, never per frame.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>

namespace SystemInfo {

// Real OS version, robust against compatibility mode, with Wine/Proton/Steam
// Deck told apart: "Windows 11 (22631)", "Proton [Wine 8.0] (Linux)".
// "" when nothing could be determined.
std::string osVersion();

// The compatibility layers Windows applied to the game ("WIN7RTM
// DISABLEDXMAXIMIZEDWINDOWEDMODE" for Windows 7 mode plus "Disable fullscreen
// optimizations"); "" when none. Log only, never sent with the usage survey.
std::string compatibilityLayers();

// CPU model as Windows names it ("AMD Ryzen 9 5950X 16-Core Processor"),
// trimmed, written into `buf`; "unknown" when the registry has none. Returns
// a pointer into `buf`.
const char* cpuName(char* buf, size_t size);

// Logical processors across every processor group.
unsigned logicalCores();

struct Memory {
    bool ok = false;
    double totalGB = 0.0;
    double freeGB = 0.0;
};
Memory physicalMemory();

}  // namespace SystemInfo
