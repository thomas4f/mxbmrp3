// ============================================================================
// vendor/piboso/gpb_api.cpp
// Thin wrapper around the GP Bikes Plugin API that forwards to PluginManager
// Uses GPBikesAdapter to convert game-specific types to unified types
// ============================================================================
#include <stdio.h>
#include <algorithm>
#include <cstring>
#include <exception>
#include "gpb_api.h"
#include "api_guard.h"
#include "../../core/plugin_manager.h"
#include "../../core/plugin_constants.h"
#include "../../diagnostics/logger.h"
#include "../../game/adapters/gpbikes_adapter.h"

using namespace PluginConstants;
using Adapter = Adapters::GPBikes::Adapter;

// Validate that API version constants match adapter - catches mismatches at compile time
static_assert(Adapter::MOD_DATA_VERSION == 12, "MOD_DATA_VERSION mismatch: update GetModDataVersion() return value");
static_assert(Adapter::INTERFACE_VERSION == 9, "INTERFACE_VERSION mismatch: update GetInterfaceVersion() return value");

__declspec(dllexport) char* GetModID()
{
	static char modId[] = "gpbikes";
	return modId;
}

__declspec(dllexport) int GetModDataVersion()
{
	return 12;
}

__declspec(dllexport) int GetInterfaceVersion()
{
	return 9;
}

/* called when software is started */
__declspec(dllexport) int Startup(char* _szSavePath)
{
	try {
		return PluginManager::getInstance().handleStartup(_szSavePath);
	} API_GUARD_CATCH("Startup")
	// Telemetry rate fallback: returning -1 (which docs say means "disable")
	// is rejected by the game and unloads the plugin — safest outcome on a
	// startup failure since downstream state can't be trusted anyway.
	return -1;
}

/* called when software is closed */
__declspec(dllexport) void Shutdown()
{
	try {
		PluginManager::getInstance().handleShutdown();
	} API_GUARD_CATCH("Shutdown")
}

/* called when event is initialized. This function is optional */
__declspec(dllexport) void EventInit(void* _pData, int _iDataSize)
{
	try {
		// Crash-safety guard: copy at most _iDataSize bytes (clamped to
		// sizeof(safeData)) into a zero-initialized local. If a future PiBoSo
		// build appends fields to SPluginsGPBBikeEvent_t we'll just see zeroes
		// in the trailing slots instead of OOB-reading; if it sends fewer bytes
		// than expected today, same outcome. Mirrors the MX Bikes pattern.
		SPluginsGPBBikeEvent_t safeData{};
		size_t copySize = _iDataSize > 0
			? std::min(static_cast<size_t>(_iDataSize), sizeof(safeData))
			: 0;
		if (_pData && copySize > 0) {
			memcpy(&safeData, _pData, copySize);
		}

		auto unified = Adapter::toVehicleEvent(&safeData);
		PluginManager::getInstance().handleEventInit(&unified);
	} API_GUARD_CATCH("EventInit")
}

/* called when event is closed. This function is optional */
__declspec(dllexport) void EventDeinit()
{
	try {
		PluginManager::getInstance().handleEventDeinit();
	} API_GUARD_CATCH("EventDeinit")
}

/* called when bike goes to track. This function is optional */
__declspec(dllexport) void RunInit(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBBikeSession_t*>(_pData);
		auto unified = Adapter::toSessionData(gameData);
		PluginManager::getInstance().handleRunInit(&unified);
	} API_GUARD_CATCH("RunInit")
}

/* called when bike leaves the track. This function is optional */
__declspec(dllexport) void RunDeinit()
{
	try {
		PluginManager::getInstance().handleRunDeinit();
	} API_GUARD_CATCH("RunDeinit")
}

/* called when simulation is started / resumed. This function is optional */
__declspec(dllexport) void RunStart()
{
	try {
		PluginManager::getInstance().handleRunStart();
	} API_GUARD_CATCH("RunStart")
}

/* called when simulation is paused. This function is optional */
__declspec(dllexport) void RunStop()
{
	try {
		PluginManager::getInstance().handleRunStop();
	} API_GUARD_CATCH("RunStop")
}

/* called when a new lap is recorded. This function is optional */
__declspec(dllexport) void RunLap(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBBikeLap_t*>(_pData);
		auto unified = Adapter::toPlayerLap(gameData);
		PluginManager::getInstance().handleRunLap(&unified);
	} API_GUARD_CATCH("RunLap")
}

/* called when a split is crossed. This function is optional */
__declspec(dllexport) void RunSplit(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBBikeSplit_t*>(_pData);
		auto unified = Adapter::toPlayerSplit(gameData);
		PluginManager::getInstance().handleRunSplit(&unified);
	} API_GUARD_CATCH("RunSplit")
}

/* _fTime is the ontrack time, in seconds. _fPos is the position on centerline, from 0 to 1. This function is optional */
__declspec(dllexport) void RunTelemetry(void* _pData, int _iDataSize, float _fTime, float _fPos)
{
	try {
		auto* gameData = static_cast<SPluginsGPBBikeData_t*>(_pData);
		auto unified = Adapter::toTelemetry(gameData, _fTime, _fPos);
		PluginManager::getInstance().handleRunTelemetry(&unified);
	} API_GUARD_CATCH("RunTelemetry")
}

/*
called when software is started.
Set _piNumSprites to the number of zero-separated filenames in _pszSpriteName.
Set _piNumFonts to the number of zero-separated filenames in _pszFontName.
The base path for the sprite and font files is the plugins folder.
This function is optional
*/
__declspec(dllexport) int DrawInit(int* _piNumSprites, char** _pszSpriteName, int* _piNumFonts, char** _pszFontName)
{
	// Zero output counts up-front so a throw in handleDrawInit leaves the
	// game reading "no sprites, no fonts" instead of stale memory and a
	// possibly-uninitialized name pointer.
	if (_piNumSprites) *_piNumSprites = 0;
	if (_piNumFonts)   *_piNumFonts   = 0;
	try {
		return PluginManager::getInstance().handleDrawInit(_piNumSprites, _pszSpriteName, _piNumFonts, _pszFontName);
	} API_GUARD_CATCH("DrawInit")
	return 0;
}

/*
_iState: 0 = on track; 1 = spectate; 2 = replay.
Set _piNumQuads to the number of quads to draw.
Set _ppQuad to an array of SPluginQuad_t structures.
Set _piNumString to the number of strings to draw.
Set _ppString to an array of SPluginString_t structures.
This function is optional
*/
__declspec(dllexport) void Draw(int _iState, int* _piNumQuads, void** _ppQuad, int* _piNumString, void** _ppString)
{
	// Zero output counts up-front so a throw inside HudManager::draw() (which
	// writes the count and pointer together at the end) leaves the game
	// reading "no quads / no strings" instead of stale counts paired with
	// uninitialized pointers.
	if (_piNumQuads)  *_piNumQuads  = 0;
	if (_ppQuad)      *_ppQuad      = nullptr;
	if (_piNumString) *_piNumString = 0;
	if (_ppString)    *_ppString    = nullptr;
	try {
		PluginManager::getInstance().handleDraw(_iState, _piNumQuads, _ppQuad, _piNumString, _ppString);
	} API_GUARD_CATCH("Draw")
}

/*
_pRaceData is a pointer to a float array with the longitudinal position of the start / finish line and splits.
This function is optional
*/
__declspec(dllexport) void TrackCenterline(int _iNumSegments, SPluginsGPBTrackSegment_t* _pasSegment, void* _pRaceData)
{
	try {
		// TrackSegment has identical layout to SPluginsGPBTrackSegment_t, safe to reinterpret
		static_assert(sizeof(Unified::TrackSegment) == sizeof(SPluginsGPBTrackSegment_t),
			"TrackSegment layout mismatch - update Unified::TrackSegment to match game struct");
		auto* unified = reinterpret_cast<Unified::TrackSegment*>(_pasSegment);
		// _pRaceData (S/F + split positions) is broken in current GP Bikes builds —
		// passing nullptr disables the map's S/F repositioning and split markers
		// here. Re-enable by forwarding _pRaceData once PiBoSo fixes the data.
		PluginManager::getInstance().handleTrackCenterline(_iNumSegments, unified, nullptr);
		(void)_pRaceData;
	} API_GUARD_CATCH("TrackCenterline")
}

/* called when event is initialized or a replay is loaded. This function is optional */
__declspec(dllexport) void RaceEvent(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceEvent_t*>(_pData);
		auto unified = Adapter::toRaceEvent(gameData);
		PluginManager::getInstance().handleRaceEvent(&unified);
	} API_GUARD_CATCH("RaceEvent")
}

/* called when event is closed. This function is optional */
__declspec(dllexport) void RaceDeinit()
{
	try {
		PluginManager::getInstance().handleRaceDeinit();
	} API_GUARD_CATCH("RaceDeinit")
}

/* This function is optional */
__declspec(dllexport) void RaceAddEntry(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceAddEntry_t*>(_pData);
		auto unified = Adapter::toRaceEntry(gameData);
		PluginManager::getInstance().handleRaceAddEntry(&unified);
	} API_GUARD_CATCH("RaceAddEntry")
}

/* This function is optional */
__declspec(dllexport) void RaceRemoveEntry(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceRemoveEntry_t*>(_pData);
		if (!gameData) return;
		PluginManager::getInstance().handleRaceRemoveEntry(gameData->m_iRaceNum);
	} API_GUARD_CATCH("RaceRemoveEntry")
}

/* This function is optional */
__declspec(dllexport) void RaceSession(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceSession_t*>(_pData);
		auto unified = Adapter::toRaceSession(gameData);
		PluginManager::getInstance().handleRaceSession(&unified);
	} API_GUARD_CATCH("RaceSession")
}

/* This function is optional */
__declspec(dllexport) void RaceSessionState(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceSessionState_t*>(_pData);
		auto unified = Adapter::toRaceSessionState(gameData);
		PluginManager::getInstance().handleRaceSessionState(&unified);
	} API_GUARD_CATCH("RaceSessionState")
}

/* This function is optional */
__declspec(dllexport) void RaceLap(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceLap_t*>(_pData);
		auto unified = Adapter::toRaceLap(gameData);
		PluginManager::getInstance().handleRaceLap(&unified);
	} API_GUARD_CATCH("RaceLap")
}

/* This function is optional */
__declspec(dllexport) void RaceSplit(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceSplit_t*>(_pData);
		auto unified = Adapter::toRaceSplit(gameData);
		PluginManager::getInstance().handleRaceSplit(&unified);
	} API_GUARD_CATCH("RaceSplit")
}

/* GP Bikes specific: called when speed trap is crossed. This function is optional */
__declspec(dllexport) void RaceSpeed(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceSpeed_t*>(_pData);
		auto unified = Adapter::toRaceSpeed(gameData);
		PluginManager::getInstance().handleRaceSpeed(&unified);
	} API_GUARD_CATCH("RaceSpeed")
}

/* This function is optional */
__declspec(dllexport) void RaceCommunication(void* _pData, int _iDataSize)
{
	try {
		// Crash-safety guard mirroring the MX Bikes pattern: defensive copy in
		// case a future GP Bikes build appends or shifts struct fields.
		SPluginsGPBRaceCommunication_t safeData{};
		size_t copySize = _iDataSize > 0
			? std::min(static_cast<size_t>(_iDataSize), sizeof(safeData))
			: 0;
		if (_pData && copySize > 0) {
			memcpy(&safeData, _pData, copySize);
		}

		auto unified = Adapter::toRaceCommunication(&safeData);
		PluginManager::getInstance().handleRaceCommunication(&unified);
	} API_GUARD_CATCH("RaceCommunication")
}

/* The number of elements of _pArray if given by m_iNumEntries in _pData. This function is optional */
__declspec(dllexport) void RaceClassification(void* _pData, int _iDataSize, void* _pArray, int _iElemSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceClassification_t*>(_pData);
		auto* gameEntries = static_cast<SPluginsGPBRaceClassificationEntry_t*>(_pArray);
		if (!gameData) return;

		// Reject element-size mismatch: if a game update reshapes the entry
		// struct, indexing gameEntries with the compiled stride would misread
		// every entry past index 0 and run off the end of the real array.
		// (Same version-skew hazard EventInit/RaceCommunication guard against.)
		if (_iElemSize != static_cast<int>(sizeof(SPluginsGPBRaceClassificationEntry_t))) {
			static bool s_warnedElemSize = false;
			if (!s_warnedElemSize) {
				s_warnedElemSize = true;
				DEBUG_WARN_F("RaceClassification: element size %d != expected %zu, ignoring (game/plugin version mismatch?)",
					_iElemSize, sizeof(SPluginsGPBRaceClassificationEntry_t));
			}
			return;
		}

		auto unified = Adapter::toRaceClassification(gameData);

		// Clamp at the boundary so a corrupt count can't drive an OOB read of
		// the game's array (downstream clamps to MAX_RACE_ENTRIES anyway).
		int numEntries = std::clamp(gameData->m_iNumEntries, 0, Unified::MAX_RACE_ENTRIES);
		if (numEntries > 0 && !gameEntries) return;
		unified.numEntries = numEntries;

		// Static buffer pattern: Avoid heap allocations in high-frequency callbacks.
		// These callbacks fire every frame at 240fps+ so per-call allocations would
		// create significant GC pressure. Thread-safe: Piboso plugin callbacks are
		// serialized on the game thread, even though the process itself runs other
		// threads (HttpServer SSE, UpdateChecker, etc.) — those don't touch this buffer.
		static std::vector<Unified::RaceClassificationEntry> unifiedEntries;
		unifiedEntries.clear();
		unifiedEntries.reserve(numEntries > 0 ? numEntries : 0);
		for (int i = 0; i < numEntries; ++i) {
			unifiedEntries.push_back(Adapter::toRaceClassificationEntry(&gameEntries[i]));
		}

		PluginManager::getInstance().handleRaceClassification(&unified, unifiedEntries.data(), numEntries);
	} API_GUARD_CATCH("RaceClassification")
}

/* This function is optional */
__declspec(dllexport) void RaceTrackPosition(int _iNumVehicles, void* _pArray, int _iElemSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceTrackPosition_t*>(_pArray);

		// Reject element-size mismatch (see RaceClassification): wrong stride
		// would misread every vehicle past index 0 and overrun the array.
		if (_iElemSize != static_cast<int>(sizeof(SPluginsGPBRaceTrackPosition_t))) {
			static bool s_warnedElemSize = false;
			if (!s_warnedElemSize) {
				s_warnedElemSize = true;
				DEBUG_WARN_F("RaceTrackPosition: element size %d != expected %zu, ignoring (game/plugin version mismatch?)",
					_iElemSize, sizeof(SPluginsGPBRaceTrackPosition_t));
			}
			return;
		}

		int numVehicles = std::clamp(_iNumVehicles, 0, Unified::MAX_RACE_ENTRIES);
		if (numVehicles > 0 && !gameData) return;

		// Convert entries array - use static buffer to avoid per-call allocations
		static std::vector<Unified::TrackPositionData> unified;
		unified.clear();
		unified.reserve(numVehicles > 0 ? numVehicles : 0);
		for (int i = 0; i < numVehicles; ++i) {
			unified.push_back(Adapter::toTrackPosition(&gameData[i]));
		}

		PluginManager::getInstance().handleRaceTrackPosition(numVehicles, unified.data());
	} API_GUARD_CATCH("RaceTrackPosition")
}

/* This function is optional */
__declspec(dllexport) void RaceVehicleData(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsGPBRaceVehicleData_t*>(_pData);
		auto unified = Adapter::toRaceVehicleData(gameData);
		PluginManager::getInstance().handleRaceVehicleData(&unified);
	} API_GUARD_CATCH("RaceVehicleData")
}

/* Return 1 if _piSelect is set, from 0 to _iNumVehicles - 1 */
__declspec(dllexport) int SpectateVehicles(int _iNumVehicles, void* _pVehicleData, int _iCurSelection, int* _piSelect)
{
	try {
		auto* gameData = static_cast<SPluginsGPBSpectateVehicle_t*>(_pVehicleData);
		if (_iNumVehicles > 0 && !gameData) return 0;

		// Clamp count: PiBoSo can hand us an unbounded/garbage count and there's no
		// _iElemSize here to sanity-check against, so the clamp is the only defense.
		int numVehicles = std::clamp(_iNumVehicles, 0, Unified::MAX_RACE_ENTRIES);

		// Convert entries array - use static buffer to avoid per-call allocations
		static std::vector<Unified::SpectateVehicle> unified;
		unified.clear();
		unified.reserve(numVehicles);
		for (int i = 0; i < numVehicles; ++i) {
			unified.push_back(Adapter::toSpectateVehicle(&gameData[i]));
		}

		return PluginManager::getInstance().handleSpectateVehicles(numVehicles, unified.data(), _iCurSelection, _piSelect);
	} API_GUARD_CATCH("SpectateVehicles")
	return 0;
}

/* Return 1 if _piSelect is set, from 0 to _iNumCameras - 1 */
/* _pCameraData contains null-terminated camera names (iterate with strlen+1) */
__declspec(dllexport) int SpectateCameras(int _iNumCameras, void* _pCameraData, int _iCurSelection, int* _piSelect)
{
	try {
		// TODO: Camera switching not yet implemented
		return PluginManager::getInstance().handleSpectateCameras(_iNumCameras, _pCameraData, _iCurSelection, _piSelect);
	} API_GUARD_CATCH("SpectateCameras")
	return 0;
}
