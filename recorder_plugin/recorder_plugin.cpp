// ============================================================================
// recorder_plugin/recorder_plugin.cpp
// Minimal plugin that records all MX Bikes API events to file
// ============================================================================

#include "recorder_plugin.h"
#include "event_recorder.h"
#include <cstdio>
#include <ctime>
#include <windows.h>

// ============================================================================
// MX Bikes Plugin Interface - REQUIRED FUNCTIONS
// ============================================================================
// These three functions are called by MX Bikes to identify and validate the plugin.
// Without them, MX Bikes will not recognize the DLL as a plugin!

extern "C" __declspec(dllexport) char* GetModID() {
    return (char*)"mxbikes";  // Must match MX Bikes mod ID
}

extern "C" __declspec(dllexport) int GetModDataVersion() {
    return 8;  // MX Bikes data version
}

extern "C" __declspec(dllexport) int GetInterfaceVersion() {
    return 9;  // MX Bikes plugin interface version
}

// ============================================================================
// Global state
// ============================================================================
static EventRecorder* g_recorder = nullptr;
static bool g_recordingEnabled = true;  // Set to true to auto-record
static char g_currentRecordingPath[512] = {0};
static char g_savePath[256] = {0};  // Store save path from Startup
static FILE* g_logFile = nullptr;  // Log file for debugging

// Logging helper - writes to both console and log file
static void Log(const char* message) {
    // Try console
    printf("%s", message);

    // Always write to log file
    if (g_logFile) {
        fprintf(g_logFile, "%s", message);
        fflush(g_logFile);
    }
}

static void LogF(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Log(buffer);
}

// Initialize plugin
extern "C" __declspec(dllexport) int Startup(char* _szSavePath) {
    // Create log file
    char logPath[512];
    const char* savePath = _szSavePath ? _szSavePath : ".";
    snprintf(logPath, sizeof(logPath), "%s/recorder.log", savePath);
    fopen_s(&g_logFile, logPath, "w");

    // Try to create console for live feedback
    if (AllocConsole()) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        SetConsoleTitleA("MX Bikes Recorder");
    }

    // Initialize recorder
    g_recorder = &EventRecorder::getInstance();

    // Store save path
    if (_szSavePath) {
        strncpy_s(g_savePath, sizeof(g_savePath), _szSavePath, sizeof(g_savePath) - 1);
    }

    // Start recording immediately
    if (g_recordingEnabled) {
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);

        char filename[512];
        const char* savePath = (g_savePath[0] != '\0') ? g_savePath : ".";

        // Create recordings directory
        char recordingsDir[512];
        snprintf(recordingsDir, sizeof(recordingsDir), "%s/recordings", savePath);
        CreateDirectoryA(recordingsDir, NULL);

        snprintf(filename, sizeof(filename), "%s/recordings/session_%04d%02d%02d_%02d%02d%02d.mxbrec",
                 savePath,
                 timeinfo.tm_year + 1900,
                 timeinfo.tm_mon + 1,
                 timeinfo.tm_mday,
                 timeinfo.tm_hour,
                 timeinfo.tm_min,
                 timeinfo.tm_sec);

        if (g_recorder->startRecording(filename)) {
            strncpy_s(g_currentRecordingPath, sizeof(g_currentRecordingPath), filename, sizeof(g_currentRecordingPath) - 1);
            // EventRecorder logs the start message internally, no need to duplicate
        } else {
            LogF("[Recorder] ERROR: Failed to start recording: %s\n", filename);
        }
    }

    // Record startup event
    if (g_recorder) {
        g_recorder->recordStartup(_szSavePath, 1);
    }

    return 10;  // Telemetry rate (Hz)
}

// Shutdown plugin
extern "C" __declspec(dllexport) void Shutdown() {
    // Record shutdown event BEFORE stopping recording
    if (g_recorder) {
        g_recorder->recordShutdown();
    }

    if (g_recorder && g_recorder->isRecording()) {
        g_recorder->stopRecording();
        LogF("[Recorder] Recording saved: %s\n", g_currentRecordingPath);
    }

    // Close log file
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

// Event callbacks
extern "C" __declspec(dllexport) void EventInit(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordEventInit((SPluginsBikeEvent_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void EventDeinit() {
    if (g_recorder) {
        g_recorder->recordEventDeinit();
    }
}

extern "C" __declspec(dllexport) void RunInit(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRunInit((SPluginsBikeSession_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RunDeinit() {
    if (g_recorder) {
        g_recorder->recordRunDeinit();
    }
}

extern "C" __declspec(dllexport) void RunStart() {
    if (g_recorder) {
        g_recorder->recordRunStart();
    }
}

extern "C" __declspec(dllexport) void RunStop() {
    if (g_recorder) {
        g_recorder->recordRunStop();
    }
}

extern "C" __declspec(dllexport) void RunLap(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRunLap((SPluginsBikeLap_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RunSplit(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRunSplit((SPluginsBikeSplit_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RunTelemetry(void* _pData, int _iDataSize, float _fTime, float _fPos) {
    if (g_recorder && _pData) {
        g_recorder->recordRunTelemetry((SPluginsBikeData_t*)_pData, _fTime, _fPos);
    }
}

extern "C" __declspec(dllexport) int DrawInit(int* _piNumSprites, char** _pszSpriteName, int* _piNumFonts, char** _pszFontName) {
    // No drawing
    *_piNumSprites = 0;
    *_piNumFonts = 0;
    int result = 1;

    // Record DrawInit result
    if (g_recorder) {
        g_recorder->recordDrawInit(*_piNumSprites, _pszSpriteName, *_piNumFonts, _pszFontName, result);
    }

    return result;
}

extern "C" __declspec(dllexport) void Draw(int _iState, int* _piNumQuads, void** _ppQuad, int* _piNumString, void** _ppString) {
    // No drawing
    *_piNumQuads = 0;
    *_piNumString = 0;

    // Record Draw event
    if (g_recorder) {
        g_recorder->recordDraw();
    }
}

extern "C" __declspec(dllexport) void TrackCenterline(int _iNumSegments, SPluginsTrackSegment_t* _pasSegment, void* _pRaceData) {
    if (g_recorder) {
        g_recorder->recordTrackCenterline(_iNumSegments, _pasSegment, _pRaceData);
    }
}

// Race events
extern "C" __declspec(dllexport) void RaceEvent(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceEvent((SPluginsRaceEvent_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RaceDeinit() {
    if (g_recorder) {
        g_recorder->recordRaceDeinit();
    }
}

extern "C" __declspec(dllexport) void RaceAddEntry(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceAddEntry((SPluginsRaceAddEntry_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RaceRemoveEntry(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceRemoveEntry((SPluginsRaceRemoveEntry_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RaceSession(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceSession((SPluginsRaceSession_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RaceSessionState(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceSessionState((SPluginsRaceSessionState_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RaceLap(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceLap((SPluginsRaceLap_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RaceSplit(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceSplit((SPluginsRaceSplit_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RaceHoleshot(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceHoleshot((SPluginsRaceHoleshot_t*)_pData);
    }
}

extern "C" __declspec(dllexport) void RaceCommunication(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceCommunication((SPluginsRaceCommunication_t*)_pData, _iDataSize);
    }
}

extern "C" __declspec(dllexport) void RaceClassification(void* _pData, int _iDataSize, void* _pArray, int _iElemSize) {
    if (!_pData || !_pArray) {
        return;
    }

    // The number of entries is stored in the header structure, NOT calculated from sizes!
    SPluginsRaceClassification_t* psRaceClassification = (SPluginsRaceClassification_t*)_pData;
    int numEntries = psRaceClassification->m_iNumEntries;

    if (g_recorder && numEntries > 0) {
        g_recorder->recordRaceClassification(
            psRaceClassification,
            (SPluginsRaceClassificationEntry_t*)_pArray,
            numEntries
        );
    }
}

extern "C" __declspec(dllexport) void RaceTrackPosition(int _iNumVehicles, void* _pArray, int _iElemSize) {
    if (g_recorder && _pArray) {
        g_recorder->recordRaceTrackPosition((SPluginsRaceTrackPosition_t*)_pArray, _iNumVehicles);
    }
}

extern "C" __declspec(dllexport) void RaceVehicleData(void* _pData, int _iDataSize) {
    if (g_recorder && _pData) {
        g_recorder->recordRaceVehicleData((SPluginsRaceVehicleData_t*)_pData);
    }
}

extern "C" __declspec(dllexport) int SpectateVehicles(int _iNumVehicles, void* _pasVehicle, int _iCurSelection, int* _piSelection) {
    return 0;
}

extern "C" __declspec(dllexport) int SpectateCameras(int _iNumCameras, void* _paCamera, int _iCurSelection, int* _piSelection) {
    return 0;
}
