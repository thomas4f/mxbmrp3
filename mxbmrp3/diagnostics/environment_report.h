// ============================================================================
// diagnostics/environment_report.h
// A short block in mxbmrp3_log.txt describing the PC and what is loaded in the
// game with the plugin, so a player's graphics report (ghost images, flicker, a
// HUD drawn twice) can be diagnosed from the log they already send.
//
// Three moments, each once per launch, all on the game thread:
//   logStartup()     - OS, CPU, RAM, monitors. Right after the Logger opens.
//   logFirstFrame()  - the first Draw, the earliest point the game's OpenGL
//                      context is current on our thread: GPU and driver strings,
//                      the game's viewport, the plugin's drawing settings, and
//                      the modules loaded in the game (diagnostics/module_census.h).
//                      Overlays inject after launch, so the first frame sees far
//                      more of them than plugin load does.
//   logLateModules() - from Shutdown(): modules that arrived after the first
//                      frame, such as an overlay opened with its hotkey.
//
// Read-only throughout: documented Windows calls that list the process's own
// modules and glGetString/glGetIntegerv, which change no GL state. Nothing reads
// the game's memory, nothing is hooked, and no other process is looked at. The
// output is anonymous (file names and folder kinds only; see module_census.h).
// docs/what-it-does.md lists it.
// ============================================================================
#pragma once

namespace EnvironmentReport {

void logStartup();
void logFirstFrame();
void logLateModules();

}  // namespace EnvironmentReport
