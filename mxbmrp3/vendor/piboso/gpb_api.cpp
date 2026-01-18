// ============================================================================
// vendor/piboso/gpb_api.cpp
// Thin wrapper around the GP Bikes Plugin API that forwards to PluginManager
// Uses GPBikesAdapter to convert game-specific types to unified types
// ============================================================================
#include <stdio.h>
#include <cstring>
#include "gpb_api.h"
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
	return PluginManager::getInstance().handleStartup(_szSavePath);
}

/* called when software is closed */
__declspec(dllexport) void Shutdown()
{
	PluginManager::getInstance().handleShutdown();
}

/* called when event is initialized. This function is optional */
__declspec(dllexport) void EventInit(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBBikeEvent_t*>(_pData);
	auto unified = Adapter::toVehicleEvent(gameData);
	PluginManager::getInstance().handleEventInit(&unified);
}

/* called when event is closed. This function is optional */
__declspec(dllexport) void EventDeinit()
{
	PluginManager::getInstance().handleEventDeinit();
}

/* called when bike goes to track. This function is optional */
__declspec(dllexport) void RunInit(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBBikeSession_t*>(_pData);
	auto unified = Adapter::toSessionData(gameData);
	PluginManager::getInstance().handleRunInit(&unified);
}

/* called when bike leaves the track. This function is optional */
__declspec(dllexport) void RunDeinit()
{
	PluginManager::getInstance().handleRunDeinit();
}

/* called when simulation is started / resumed. This function is optional */
__declspec(dllexport) void RunStart()
{
	PluginManager::getInstance().handleRunStart();
}

/* called when simulation is paused. This function is optional */
__declspec(dllexport) void RunStop()
{
	PluginManager::getInstance().handleRunStop();
}

/* called when a new lap is recorded. This function is optional */
__declspec(dllexport) void RunLap(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBBikeLap_t*>(_pData);
	auto unified = Adapter::toPlayerLap(gameData);
	PluginManager::getInstance().handleRunLap(&unified);
}

/* called when a split is crossed. This function is optional */
__declspec(dllexport) void RunSplit(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBBikeSplit_t*>(_pData);
	auto unified = Adapter::toPlayerSplit(gameData);
	PluginManager::getInstance().handleRunSplit(&unified);
}

/* _fTime is the ontrack time, in seconds. _fPos is the position on centerline, from 0 to 1. This function is optional */
__declspec(dllexport) void RunTelemetry(void* _pData, int _iDataSize, float _fTime, float _fPos)
{
	auto* gameData = static_cast<SPluginsGPBBikeData_t*>(_pData);
	auto unified = Adapter::toTelemetry(gameData, _fTime, _fPos);
	PluginManager::getInstance().handleRunTelemetry(&unified);
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
	return PluginManager::getInstance().handleDrawInit(_piNumSprites, _pszSpriteName, _piNumFonts, _pszFontName);
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
	PluginManager::getInstance().handleDraw(_iState, _piNumQuads, _ppQuad, _piNumString, _ppString);
}

/*
_pRaceData is a pointer to a float array with the longitudinal position of the start / finish line and splits.
This function is optional
*/
__declspec(dllexport) void TrackCenterline(int _iNumSegments, SPluginsGPBTrackSegment_t* _pasSegment, void* _pRaceData)
{
	// TrackSegment has identical layout to SPluginsGPBTrackSegment_t, safe to reinterpret
	static_assert(sizeof(Unified::TrackSegment) == sizeof(SPluginsGPBTrackSegment_t),
		"TrackSegment layout mismatch - update Unified::TrackSegment to match game struct");
	auto* unified = reinterpret_cast<Unified::TrackSegment*>(_pasSegment);
	PluginManager::getInstance().handleTrackCenterline(_iNumSegments, unified, _pRaceData);
}

/* called when event is initialized or a replay is loaded. This function is optional */
__declspec(dllexport) void RaceEvent(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceEvent_t*>(_pData);
	auto unified = Adapter::toRaceEvent(gameData);
	PluginManager::getInstance().handleRaceEvent(&unified);
}

/* called when event is closed. This function is optional */
__declspec(dllexport) void RaceDeinit()
{
	PluginManager::getInstance().handleRaceDeinit();
}

/* This function is optional */
__declspec(dllexport) void RaceAddEntry(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceAddEntry_t*>(_pData);
	auto unified = Adapter::toRaceEntry(gameData);
	PluginManager::getInstance().handleRaceAddEntry(&unified);
}

/* This function is optional */
__declspec(dllexport) void RaceRemoveEntry(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceRemoveEntry_t*>(_pData);
	PluginManager::getInstance().handleRaceRemoveEntry(gameData->m_iRaceNum);
}

/* This function is optional */
__declspec(dllexport) void RaceSession(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceSession_t*>(_pData);
	auto unified = Adapter::toRaceSession(gameData);
	PluginManager::getInstance().handleRaceSession(&unified);
}

/* This function is optional */
__declspec(dllexport) void RaceSessionState(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceSessionState_t*>(_pData);
	auto unified = Adapter::toRaceSessionState(gameData);
	PluginManager::getInstance().handleRaceSessionState(&unified);
}

/* This function is optional */
__declspec(dllexport) void RaceLap(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceLap_t*>(_pData);
	auto unified = Adapter::toRaceLap(gameData);
	PluginManager::getInstance().handleRaceLap(&unified);
}

/* This function is optional */
__declspec(dllexport) void RaceSplit(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceSplit_t*>(_pData);
	auto unified = Adapter::toRaceSplit(gameData);
	PluginManager::getInstance().handleRaceSplit(&unified);
}

/* GP Bikes specific: called when speed trap is crossed. This function is optional */
__declspec(dllexport) void RaceSpeed(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceSpeed_t*>(_pData);
	auto unified = Adapter::toRaceSpeed(gameData);
	PluginManager::getInstance().handleRaceSpeed(&unified);
}

/* This function is optional */
__declspec(dllexport) void RaceCommunication(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceCommunication_t*>(_pData);
	auto unified = Adapter::toRaceCommunication(gameData);
	PluginManager::getInstance().handleRaceCommunication(&unified);
}

/* The number of elements of _pArray if given by m_iNumEntries in _pData. This function is optional */
__declspec(dllexport) void RaceClassification(void* _pData, int _iDataSize, void* _pArray, int _iElemSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceClassification_t*>(_pData);
	auto* gameEntries = static_cast<SPluginsGPBRaceClassificationEntry_t*>(_pArray);

	auto unified = Adapter::toRaceClassification(gameData);

	// Static buffer pattern: Avoid heap allocations in high-frequency callbacks.
	// These callbacks fire every frame at 240fps+ so per-call allocations would
	// create significant GC pressure. Thread-safe: Piboso games are single-threaded.
	static std::vector<Unified::RaceClassificationEntry> unifiedEntries;
	unifiedEntries.clear();
	unifiedEntries.reserve(gameData->m_iNumEntries);
	for (int i = 0; i < gameData->m_iNumEntries; ++i) {
		unifiedEntries.push_back(Adapter::toRaceClassificationEntry(&gameEntries[i]));
	}

	PluginManager::getInstance().handleRaceClassification(&unified, unifiedEntries.data(), gameData->m_iNumEntries);
}

/* This function is optional */
__declspec(dllexport) void RaceTrackPosition(int _iNumVehicles, void* _pArray, int _iElemSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceTrackPosition_t*>(_pArray);

	// Convert entries array - use static buffer to avoid per-call allocations
	static std::vector<Unified::TrackPositionData> unified;
	unified.clear();
	unified.reserve(_iNumVehicles);
	for (int i = 0; i < _iNumVehicles; ++i) {
		unified.push_back(Adapter::toTrackPosition(&gameData[i]));
	}

	PluginManager::getInstance().handleRaceTrackPosition(_iNumVehicles, unified.data());
}

/* This function is optional */
__declspec(dllexport) void RaceVehicleData(void* _pData, int _iDataSize)
{
	auto* gameData = static_cast<SPluginsGPBRaceVehicleData_t*>(_pData);
	auto unified = Adapter::toRaceVehicleData(gameData);
	PluginManager::getInstance().handleRaceVehicleData(&unified);
}

/* Return 1 if _piSelect is set, from 0 to _iNumVehicles - 1 */
__declspec(dllexport) int SpectateVehicles(int _iNumVehicles, void* _pVehicleData, int _iCurSelection, int* _piSelect)
{
	auto* gameData = static_cast<SPluginsGPBSpectateVehicle_t*>(_pVehicleData);

	// Convert entries array - use static buffer to avoid per-call allocations
	static std::vector<Unified::SpectateVehicle> unified;
	unified.clear();
	unified.reserve(_iNumVehicles);
	for (int i = 0; i < _iNumVehicles; ++i) {
		unified.push_back(Adapter::toSpectateVehicle(&gameData[i]));
	}

	return PluginManager::getInstance().handleSpectateVehicles(_iNumVehicles, unified.data(), _iCurSelection, _piSelect);
}

/* Return 1 if _piSelect is set, from 0 to _iNumCameras - 1 */
/* _pCameraData contains null-terminated camera names (iterate with strlen+1) */
__declspec(dllexport) int SpectateCameras(int _iNumCameras, void* _pCameraData, int _iCurSelection, int* _piSelect)
{
	// TODO: Camera switching not yet implemented
	return PluginManager::getInstance().handleSpectateCameras(_iNumCameras, _pCameraData, _iCurSelection, _piSelect);
}
