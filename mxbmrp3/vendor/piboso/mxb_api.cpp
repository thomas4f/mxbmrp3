// ============================================================================
// vendor/piboso/mxb_api.cpp
// Thin wrapper around the MXBikes Plugin API that forwards to PluginManager
// Keep this file minimal to allow easy updates when the API changes
// ============================================================================
#include <stdio.h>
#include <cstring>
#include "mxb_api.h"
#include "../../core/plugin_manager.h"
#include "../../core/plugin_constants.h"
#include "../../diagnostics/logger.h"

using namespace PluginConstants;

__declspec(dllexport) const char* GetModID()
{
	return MOD_ID;
}

__declspec(dllexport) int GetModDataVersion()
{
	return MOD_DATA_VERSION;
}

__declspec(dllexport) int GetInterfaceVersion()
{
	return INTERFACE_VERSION;
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
	SPluginsBikeEvent_t* psEventData = (SPluginsBikeEvent_t*)_pData;
	PluginManager::getInstance().handleEventInit(psEventData);
}

/* called when event is closed. This function is optional */
__declspec(dllexport) void EventDeinit()
{
	PluginManager::getInstance().handleEventDeinit();
}

/* called when bike goes to track. This function is optional */
__declspec(dllexport) void RunInit(void* _pData, int _iDataSize)
{
	SPluginsBikeSession_t* psSessionData = (SPluginsBikeSession_t*)_pData;
	PluginManager::getInstance().handleRunInit(psSessionData);
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
	SPluginsBikeLap_t* psLapData = (SPluginsBikeLap_t*)_pData;
	PluginManager::getInstance().handleRunLap(psLapData);
}

/* called when a split is crossed. This function is optional */
__declspec(dllexport) void RunSplit(void* _pData, int _iDataSize)
{
	SPluginsBikeSplit_t* psSplitData = (SPluginsBikeSplit_t*)_pData;
	PluginManager::getInstance().handleRunSplit(psSplitData);
}

/* _fTime is the ontrack time, in seconds. _fPos is the position on centerline, from 0 to 1. This function is optional */
__declspec(dllexport) void RunTelemetry(void* _pData, int _iDataSize, float _fTime, float _fPos)
{
	SPluginsBikeData_t* psBikeData = (SPluginsBikeData_t*)_pData;
	PluginManager::getInstance().handleRunTelemetry(psBikeData, _fTime, _fPos);
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
_pRaceData is a pointer to a float array with the longitudinal position of the start / finish line, splits and holeshot.
This function is optional
*/
__declspec(dllexport) void TrackCenterline(int _iNumSegments, SPluginsTrackSegment_t* _pasSegment, void* _pRaceData)
{
	PluginManager::getInstance().handleTrackCenterline(_iNumSegments, _pasSegment, _pRaceData);
}

/* called when event is initialized or a replay is loaded. This function is optional */
__declspec(dllexport) void RaceEvent(void* _pData, int _iDataSize)
{
	SPluginsRaceEvent_t* psRaceEvent = (SPluginsRaceEvent_t*)_pData;
	PluginManager::getInstance().handleRaceEvent(psRaceEvent);
}

/* called when event is closed. This function is optional */
__declspec(dllexport) void RaceDeinit()
{
	PluginManager::getInstance().handleRaceDeinit();
}

/* This function is optional */
__declspec(dllexport) void RaceAddEntry(void* _pData, int _iDataSize)
{
	SPluginsRaceAddEntry_t* psRaceAddEntry = (SPluginsRaceAddEntry_t*)_pData;
	PluginManager::getInstance().handleRaceAddEntry(psRaceAddEntry);
}

/* This function is optional */
__declspec(dllexport) void RaceRemoveEntry(void* _pData, int _iDataSize)
{
	SPluginsRaceRemoveEntry_t* psRaceRemoveEntry = (SPluginsRaceRemoveEntry_t*)_pData;
	PluginManager::getInstance().handleRaceRemoveEntry(psRaceRemoveEntry);
}

/* This function is optional */
__declspec(dllexport) void RaceSession(void* _pData, int _iDataSize)
{
	SPluginsRaceSession_t* psRaceSession = (SPluginsRaceSession_t*)_pData;
	PluginManager::getInstance().handleRaceSession(psRaceSession);
}

/* This function is optional */
__declspec(dllexport) void RaceSessionState(void* _pData, int _iDataSize)
{
	SPluginsRaceSessionState_t* psRaceSessionState = (SPluginsRaceSessionState_t*)_pData;
	PluginManager::getInstance().handleRaceSessionState(psRaceSessionState);
}

/* This function is optional */
__declspec(dllexport) void RaceLap(void* _pData, int _iDataSize)
{
	SPluginsRaceLap_t* psRaceLap = (SPluginsRaceLap_t*)_pData;
	PluginManager::getInstance().handleRaceLap(psRaceLap);
}

/* This function is optional */
__declspec(dllexport) void RaceSplit(void* _pData, int _iDataSize)
{
	SPluginsRaceSplit_t* psRaceSplit = (SPluginsRaceSplit_t*)_pData;
	PluginManager::getInstance().handleRaceSplit(psRaceSplit);
}

/* This function is optional */
__declspec(dllexport) void RaceHoleshot(void* _pData, int _iDataSize)
{
	SPluginsRaceHoleshot_t* psRaceHoleshot = (SPluginsRaceHoleshot_t*)_pData;
	PluginManager::getInstance().handleRaceHoleshot(psRaceHoleshot);
}

/* This function is optional */
__declspec(dllexport) void RaceCommunication(void* _pData, int _iDataSize)
{
	SPluginsRaceCommunication_t* psRaceCommunication = (SPluginsRaceCommunication_t*)_pData;
	PluginManager::getInstance().handleRaceCommunication(psRaceCommunication, _iDataSize);
}

/* The number of elements of _pArray if given by m_iNumEntries in _pData. This function is optional */
__declspec(dllexport) void RaceClassification(void* _pData, int _iDataSize, void* _pArray, int _iElemSize)
{
	SPluginsRaceClassification_t* psRaceClassification = (SPluginsRaceClassification_t*)_pData;
	SPluginsRaceClassificationEntry_t* pasRaceClassificationEntry = (SPluginsRaceClassificationEntry_t*)_pArray;
	PluginManager::getInstance().handleRaceClassification(psRaceClassification, pasRaceClassificationEntry, psRaceClassification->m_iNumEntries);
}

/* This function is optional */
__declspec(dllexport) void RaceTrackPosition(int _iNumVehicles, void* _pArray, int _iElemSize)
{
	SPluginsRaceTrackPosition_t* pasRaceTrackPosition = (SPluginsRaceTrackPosition_t*)_pArray;
	PluginManager::getInstance().handleRaceTrackPosition(_iNumVehicles, pasRaceTrackPosition);
}

/* This function is optional */
__declspec(dllexport) void RaceVehicleData(void* _pData, int _iDataSize)
{
	SPluginsRaceVehicleData_t* psRaceVehicleData = (SPluginsRaceVehicleData_t*)_pData;
	PluginManager::getInstance().handleRaceVehicleData(psRaceVehicleData);
}

/* Return 1 if _piSelect is set, from 0 to _iNumVehicles - 1 */
__declspec(dllexport) int SpectateVehicles(int _iNumVehicles, void* _pVehicleData, int _iCurSelection, int* _piSelect)
{
	SPluginsSpectateVehicle_t* pasVehicleData = (SPluginsSpectateVehicle_t*)_pVehicleData;
	return PluginManager::getInstance().handleSpectateVehicles(_iNumVehicles, pasVehicleData, _iCurSelection, _piSelect);
}

/* Return 1 if _piSelect is set, from 0 to _iNumCameras - 1 */
__declspec(dllexport) int SpectateCameras(int _iNumCameras, void* _pCameraData, int _iCurSelection, int* _piSelect)
{
	char* pszCameraName = (char*)_pCameraData;
	for (int i = 0; i < _iNumCameras; i++)
	{
		pszCameraName += strlen(pszCameraName) + 1;
	}

	return PluginManager::getInstance().handleSpectateCameras(_iNumCameras, _pCameraData, _iCurSelection, _piSelect);
}
