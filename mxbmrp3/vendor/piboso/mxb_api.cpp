// ============================================================================
// vendor/piboso/mxb_api.cpp
// Thin wrapper around the MXBikes Plugin API that forwards to PluginManager
// Uses MXBikesAdapter to convert game-specific types to unified types
// ============================================================================
#include <stdio.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include "mxb_api.h"
#include "api_guard.h"
#include "../../core/plugin_manager.h"
#include "../../core/plugin_constants.h"
#include "../../diagnostics/logger.h"
#include "../../game/adapters/mxbikes_adapter.h"

using namespace PluginConstants;
using Adapter = Adapters::MXBikes::Adapter;

// Validate that API version constants match adapter - catches mismatches at compile time
static_assert(Adapter::MOD_DATA_VERSION == 8, "MOD_DATA_VERSION mismatch: update GetModDataVersion() return value");
static_assert(Adapter::INTERFACE_VERSION == 9, "INTERFACE_VERSION mismatch: update GetInterfaceVersion() return value");

__declspec(dllexport) char* GetModID()
{
	static char modId[] = "mxbikes";
	return modId;
}

__declspec(dllexport) int GetModDataVersion()
{
	return 8;
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
		// Guard against older MX Bikes builds that don't send the appended fields
		// (m_szServerName / m_iServerType / m_szGUID). Copy at most _iDataSize
		// bytes (clamped to sizeof(safeData)) into a zero-initialized local; if
		// the game claims to have sent zero / a negative size, copy nothing.
		SPluginsBikeEvent_t safeData{};
		size_t copySize = _iDataSize > 0
			? std::min(static_cast<size_t>(_iDataSize), sizeof(safeData))
			: 0;
		if (copySize > 0) {
			memcpy(&safeData, _pData, copySize);
		}

		// If the build is old enough that m_iServerType wasn't part of the struct,
		// flag it as unknown (-1) instead of letting zero-init lie about "offline".
		// Downstream code treats <0 as "we don't know" and skips online-only UI.
		constexpr size_t kServerTypeEnd =
			offsetof(SPluginsBikeEvent_t, m_iServerType) + sizeof(safeData.m_iServerType);
		if (copySize < kServerTypeEnd) {
			safeData.m_iServerType = -1;
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
		auto* gameData = static_cast<SPluginsBikeSession_t*>(_pData);
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
		auto* gameData = static_cast<SPluginsBikeLap_t*>(_pData);
		auto unified = Adapter::toPlayerLap(gameData);
		PluginManager::getInstance().handleRunLap(&unified);
	} API_GUARD_CATCH("RunLap")
}

/* called when a split is crossed. This function is optional */
__declspec(dllexport) void RunSplit(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsBikeSplit_t*>(_pData);
		auto unified = Adapter::toPlayerSplit(gameData);
		PluginManager::getInstance().handleRunSplit(&unified);
	} API_GUARD_CATCH("RunSplit")
}

/* _fTime is the ontrack time, in seconds. _fPos is the position on centerline, from 0 to 1. This function is optional */
__declspec(dllexport) void RunTelemetry(void* _pData, int _iDataSize, float _fTime, float _fPos)
{
	try {
		auto* gameData = static_cast<SPluginsBikeData_t*>(_pData);
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
_pRaceData is a pointer to a float array with the longitudinal position of the start / finish line, splits and holeshot.
This function is optional
*/
__declspec(dllexport) void TrackCenterline(int _iNumSegments, SPluginsTrackSegment_t* _pasSegment, void* _pRaceData)
{
	try {
		// TrackSegment has identical layout to SPluginsTrackSegment_t, safe to reinterpret
		static_assert(sizeof(Unified::TrackSegment) == sizeof(SPluginsTrackSegment_t),
			"TrackSegment layout mismatch - update Unified::TrackSegment to match game struct");
		auto* unified = reinterpret_cast<Unified::TrackSegment*>(_pasSegment);
		PluginManager::getInstance().handleTrackCenterline(_iNumSegments, unified, _pRaceData);
	} API_GUARD_CATCH("TrackCenterline")
}

/* called when event is initialized or a replay is loaded. This function is optional */
__declspec(dllexport) void RaceEvent(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsRaceEvent_t*>(_pData);
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
		auto* gameData = static_cast<SPluginsRaceAddEntry_t*>(_pData);
		auto unified = Adapter::toRaceEntry(gameData);
		PluginManager::getInstance().handleRaceAddEntry(&unified);
	} API_GUARD_CATCH("RaceAddEntry")
}

/* This function is optional */
__declspec(dllexport) void RaceRemoveEntry(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsRaceRemoveEntry_t*>(_pData);
		PluginManager::getInstance().handleRaceRemoveEntry(gameData->m_iRaceNum);
	} API_GUARD_CATCH("RaceRemoveEntry")
}

/* This function is optional */
__declspec(dllexport) void RaceSession(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsRaceSession_t*>(_pData);
		auto unified = Adapter::toRaceSession(gameData);
		PluginManager::getInstance().handleRaceSession(&unified);
	} API_GUARD_CATCH("RaceSession")
}

/* This function is optional */
__declspec(dllexport) void RaceSessionState(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsRaceSessionState_t*>(_pData);
		auto unified = Adapter::toRaceSessionState(gameData);
		PluginManager::getInstance().handleRaceSessionState(&unified);
	} API_GUARD_CATCH("RaceSessionState")
}

/* This function is optional */
__declspec(dllexport) void RaceLap(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsRaceLap_t*>(_pData);
		auto unified = Adapter::toRaceLap(gameData);
		PluginManager::getInstance().handleRaceLap(&unified);
	} API_GUARD_CATCH("RaceLap")
}

/* This function is optional */
__declspec(dllexport) void RaceSplit(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsRaceSplit_t*>(_pData);
		auto unified = Adapter::toRaceSplit(gameData);
		PluginManager::getInstance().handleRaceSplit(&unified);
	} API_GUARD_CATCH("RaceSplit")
}

/* This function is optional */
__declspec(dllexport) void RaceCommunication(void* _pData, int _iDataSize)
{
	try {
		// Crash-safety guard: copy at most _iDataSize bytes (clamped to
		// sizeof(safeData)) into a zero-initialized local. If the game claims to
		// have sent zero / a negative size, copy nothing.
		//
		// Note: this does NOT preserve correct semantics on older builds, because
		// m_iStart was *inserted* (not appended) between m_iLap and m_iType. An old
		// build's m_iType/m_iTime land at safeData.m_iStart/m_iType respectively,
		// and safeData.m_iTime reads zero. After the adapter's seconds->ms scaling
		// (zero * 1000 = zero) the user-visible result is "no penalty time
		// displayed" - same as old builds where m_iTime was always 0 anyway.
		SPluginsRaceCommunication_t safeData{};
		size_t copySize = _iDataSize > 0
			? std::min(static_cast<size_t>(_iDataSize), sizeof(safeData))
			: 0;
		if (copySize > 0) {
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
		auto* gameData = static_cast<SPluginsRaceClassification_t*>(_pData);
		auto* gameEntries = static_cast<SPluginsRaceClassificationEntry_t*>(_pArray);

		auto unified = Adapter::toRaceClassification(gameData);

		// Static buffer pattern: Avoid heap allocations in high-frequency callbacks.
		// These callbacks fire every frame at 240fps+ so per-call allocations would
		// create significant GC pressure. Thread-safe: Piboso plugin callbacks are
		// serialized on the game thread, even though the process itself runs other
		// threads (HttpServer SSE, UpdateChecker, etc.) — those don't touch this buffer.
		static std::vector<Unified::RaceClassificationEntry> unifiedEntries;
		unifiedEntries.clear();
		unifiedEntries.reserve(gameData->m_iNumEntries);
		for (int i = 0; i < gameData->m_iNumEntries; ++i) {
			unifiedEntries.push_back(Adapter::toRaceClassificationEntry(&gameEntries[i]));
		}

		PluginManager::getInstance().handleRaceClassification(&unified, unifiedEntries.data(), gameData->m_iNumEntries);
	} API_GUARD_CATCH("RaceClassification")
}

/* This function is optional */
__declspec(dllexport) void RaceTrackPosition(int _iNumVehicles, void* _pArray, int _iElemSize)
{
	try {
		auto* gameData = static_cast<SPluginsRaceTrackPosition_t*>(_pArray);

		// Convert entries array - use static buffer to avoid per-call allocations
		static std::vector<Unified::TrackPositionData> unified;
		unified.clear();
		unified.reserve(_iNumVehicles);
		for (int i = 0; i < _iNumVehicles; ++i) {
			unified.push_back(Adapter::toTrackPosition(&gameData[i]));
		}

		PluginManager::getInstance().handleRaceTrackPosition(_iNumVehicles, unified.data());
	} API_GUARD_CATCH("RaceTrackPosition")
}

/* This function is optional */
__declspec(dllexport) void RaceVehicleData(void* _pData, int _iDataSize)
{
	try {
		auto* gameData = static_cast<SPluginsRaceVehicleData_t*>(_pData);
		auto unified = Adapter::toRaceVehicleData(gameData);
		PluginManager::getInstance().handleRaceVehicleData(&unified);
	} API_GUARD_CATCH("RaceVehicleData")
}

/* Return 1 if _piSelect is set, from 0 to _iNumVehicles - 1 */
__declspec(dllexport) int SpectateVehicles(int _iNumVehicles, void* _pVehicleData, int _iCurSelection, int* _piSelect)
{
	try {
		auto* gameData = static_cast<SPluginsSpectateVehicle_t*>(_pVehicleData);

		// Convert entries array - use static buffer to avoid per-call allocations
		static std::vector<Unified::SpectateVehicle> unified;
		unified.clear();
		unified.reserve(_iNumVehicles);
		for (int i = 0; i < _iNumVehicles; ++i) {
			unified.push_back(Adapter::toSpectateVehicle(&gameData[i]));
		}

		return PluginManager::getInstance().handleSpectateVehicles(_iNumVehicles, unified.data(), _iCurSelection, _piSelect);
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
