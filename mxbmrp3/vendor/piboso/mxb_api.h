// ============================================================================
// vendor/piboso/mxb_api.h
// ============================================================================
#pragma once

extern "C" {

	/******************************************************************************
	structures and functions to receive data from the simulated bike
	******************************************************************************/

	typedef struct
	{
		char m_szRiderName[100];
		char m_szBikeID[100];
		char m_szBikeName[100];
		int m_iNumberOfGears;
		int m_iMaxRPM;
		int m_iLimiter;
		int m_iShiftRPM;
		float m_fEngineOptTemperature;									/* degrees Celsius */
		float m_afEngineTemperatureAlarm[2];							/* degrees Celsius. Lower and upper limits */
		float m_fMaxFuel;												/* fuel tank capacity. liters */
		float m_afSuspMaxTravel[2];										/* maximum travel of the shocks. meters. 0 = front; 1 = rear. */
		float m_fSteerLock;												/* degrees */
		char m_szCategory[100];
		char m_szTrackID[100];
		char m_szTrackName[100];
		float m_fTrackLength;											/* centerline length. meters */
		int m_iType;													/* 1 = testing; 2 = race; 4 = straight rhythm */
	} SPluginsBikeEvent_t;

	typedef struct
	{
		int m_iSession;													/* testing: 0 = waiting; 1 = in progress. Race: 0 = waiting; 1 = practice; 2 = pre-qualify; 3 = qualify practice; 4 = qualify; 5 = warmup; 6 = race1; 7 = race2; straight rhythm: 0 = waiting; 1 = practice; 2 = round; 3 = quarter-finals; 4 = semi-finals; 5 = final */
		int m_iConditions;												/* 0 = sunny; 1 = cloudy; 2 = rainy */
		float m_fAirTemperature;										/* degrees Celsius */
		char m_szSetupFileName[100];
	} SPluginsBikeSession_t;

	typedef struct
	{
		int m_iRPM;														/* engine rpm */
		float m_fEngineTemperature;										/* degrees Celsius */
		float m_fWaterTemperature;										/* degrees Celsius */
		int m_iGear;													/* 0 = Neutral */
		float m_fFuel;													/* liters */
		float m_fSpeedometer;											/* meters/second */
		float m_fPosX, m_fPosY, m_fPosZ;									/* world position of a reference point attached to chassis ( not CG ) */
		float m_fVelocityX, m_fVelocityY, m_fVelocityZ;					/* velocity of CG in world coordinates. meters/second */
		float m_fAccelerationX, m_fAccelerationY, m_fAccelerationZ;		/* acceleration of CG local to chassis rotation, expressed in G ( 9.81 m/s2 ) and averaged over the latest 10ms */
		float m_aafRot[3][3];											/* rotation matrix of the chassis. It incorporates lean and wheeling */
		float m_fYaw, m_fPitch, m_fRoll;									/* degrees, -180 to 180 */
		float m_fYawVelocity, m_fPitchVelocity, m_fRollVelocity;			/* degress / second */
		float m_afSuspLength[2];										/* shocks length. meters. 0 = front; 1 = rear. */
		float m_afSuspVelocity[2];										/* shocks velocity. meters/second. 0 = front; 1 = rear */
		int m_iCrashed;													/* 1 = rider is detached from bike */
		float m_fSteer;													/* degrees. Negative = right  */
		float m_fThrottle;												/* 0 to 1 */
		float m_fFrontBrake;											/* 0 to 1 */
		float m_fRearBrake;												/* 0 to 1 */
		float m_fClutch;												/* 0 to 1. 0 = Fully engaged */
		float m_afWheelSpeed[2];										/* meters/second. 0 = front; 1 = rear */
		int m_aiWheelMaterial[2];										/* material index. 0 = not in contact */
		float m_afBrakePressure[2];										/* kPa */
		float m_fSteerTorque;											/* Nm */
	} SPluginsBikeData_t;

	typedef struct
	{
		int m_iLapNum;													/* lap index */
		int m_iInvalid;
		int m_iLapTime;													/* milliseconds */
		int m_iBest;													/* 1 = best lap */
	} SPluginsBikeLap_t;

	typedef struct
	{
		int m_iSplit;													/* split index */
		int m_iSplitTime;												/* milliseconds */
		int m_iBestDiff;												/* milliseconds. Difference with best lap */
	} SPluginsBikeSplit_t;

	/******************************************************************************
	structures and functions to draw
	******************************************************************************/

	typedef struct
	{
		float m_aafPos[4][2];			/* 0,0 -> top left. 1,1 -> bottom right. counter-clockwise */
		int m_iSprite;					/* 1 based index in SpriteName buffer. 0 = fill with m_ulColor */
		unsigned long m_ulColor;		/* ABGR */
	} SPluginQuad_t;

	typedef struct
	{
		char m_szString[100];
		float m_afPos[2];				/* 0,0 -> top left. 1,1 -> bottom right */
		int m_iFont;					/* 1 based index in FontName buffer */
		float m_fSize;
		int m_iJustify;					/* 0 = left; 1 = center; 2 = right */
		unsigned long m_ulColor;		/* ABGR */
	} SPluginString_t;

	/******************************************************************************
	structures and functions to receive the track center line
	******************************************************************************/

	typedef struct
	{
		int m_iType;					/* 0 = straight; 1 = curve */
		float m_fLength;				/* meters */
		float m_fRadius;				/* curve radius in meters. < 0 for left curves; 0 for straights */
		float m_fAngle;					/* start angle in degrees. 0 = north */
		float m_afStart[2];				/* start position in meters */
		float m_fHeight;				/* start height in meters */
	} SPluginsTrackSegment_t;

	/******************************************************************************
	structures and functions to receive the race data
	******************************************************************************/

	typedef struct
	{
		int m_iType;										/* 1 = testing; 2 = race; 4 = straight rhythm; -1 = loaded replay */
		char m_szName[100];
		char m_szTrackName[100];
		float m_fTrackLength;								/* meters */
	} SPluginsRaceEvent_t;

	typedef struct
	{
		int m_iRaceNum;										/* unique race number */
		char m_szName[100];
		char m_szBikeName[100];
		char m_szBikeShortName[100];
		char m_szCategory[100];
		int m_iUnactive;									/* if set to 1, the rider left the event and the following fields are not set */
		int m_iNumberOfGears;
		int m_iMaxRPM;
	} SPluginsRaceAddEntry_t;

	typedef struct
	{
		int m_iRaceNum;										/* race number */
	} SPluginsRaceRemoveEntry_t;

	typedef struct
	{
		int m_iSession;										/* testing: 0 = waiting; 1 = in progress. Race: 0 = waiting; 1 = practice; 2 = pre-qualify; 3 = qualify practice; 4 = qualify; 5 = warmup; 6 = race1; 7 = race2 */
		int m_iSessionState;								/* testing / waiting: always 0. practice / pre-qualify / warmup: 16 = in progress; 32 = completed. qualify / race1 / race2: 16 = in progress; 32 = completed; 64 = sighting lap; 256 = pre-start; 512 = race over; 2048 = cancelled */
		int m_iSessionLength;								/* milliseconds. 0 = no limit */
		int m_iSessionNumLaps;
		int m_iConditions;									/* 0 = sunny; 1 = cloudy; 2 = rainy */
		float m_fAirTemperature;							/* degrees Celsius */
	} SPluginsRaceSession_t;

	typedef struct
	{
		int m_iSession;										/* testing: 0 = waiting; 1 = in progress. Race: 0 = waiting; 1 = practice; 2 = pre-qualify; 3 = qualify practice; 4 = qualify; 5 = warmup; 6 = race1; 7 = race2 */
		int m_iSessionState;								/* testing / waiting: always 0. practice / pre-qualify / warmup: 16 = in progress; 32 = completed. qualify / race1 / race2: 16 = in progress; 32 = completed; 64 = sighting lap; 256 = pre-start; 512 = race over; 2048 = cancelled */
		int m_iSessionLength;								/* milliseconds. 0 = no limit */
	} SPluginsRaceSessionState_t;

	typedef struct
	{
		int m_iSession;										/* testing: 0 = waiting; 1 = in progress. Race: 0 = waiting; 1 = practice; 2 = pre-qualify; 3 = qualify practice; 4 = qualify; 5 = warmup; 6 = race1; 7 = race2 */
		int m_iRaceNum;										/* race number */
		int m_iLapNum;										/* lap index */
		int m_iInvalid;
		int m_iLapTime;										/* milliseconds */
		int m_aiSplit[2];									/* milliseconds */
		int m_iBest;										/* 1 = personal best lap; 2 = overall best lap */
	} SPluginsRaceLap_t;

	typedef struct
	{
		int m_iSession;										/* testing: 0 = waiting; 1 = in progress. Race: 0 = waiting; 1 = practice; 2 = pre-qualify; 3 = qualify practice; 4 = qualify; 5 = warmup; 6 = race1; 7 = race2 */
		int m_iRaceNum;										/* race number */
		int m_iLapNum;										/* lap index */
		int m_iSplit;										/* split index */
		int m_iSplitTime;									/* milliseconds */
	} SPluginsRaceSplit_t;

	typedef struct
	{
		int m_iSession;
		int m_iRaceNum;
		int m_iTime;
	} SPluginsRaceHoleshot_t;

	typedef struct
	{
		int m_iSession;										/* testing: 0 = waiting; 1 = in progress. Race: 0 = waiting; 1 = practice; 2 = pre-qualify; 3 = qualify practice; 4 = qualify; 5 = warmup; 6 = race1; 7 = race2 */
		int m_iRaceNum;										/* race number */
		int m_iCommunication;								/* 1 = change state; 2 = penalty */
		int m_iState;										/* 1 = DNS; 2 = unknown; 3 = retired; 4 = DSQ */
		int m_iReason;										/* Reason field. 0 = jump start; 1 = too many offences; 2 = director */
		int m_iOffence;										/* 1 = jump start; 2 = cutting */
		int m_iLap;											/* lap index */
		int m_iType;										/* always 0 = time penalty */
		int m_iTime;										/* milliseconds. Penalty time */
	} SPluginsRaceCommunication_t;

	typedef struct
	{
		int m_iSession;										/* testing: 0 = waiting; 1 = in progress. Race: 0 = waiting; 1 = practice; 2 = pre-qualify; 3 = qualify practice; 4 = qualify; 5 = warmup; 6 = race1; 7 = race2 */
		int m_iSessionState;								/* testing / waiting: always 0. practice / pre-qualify / warmup: 16 = in progress; 32 = completed. qualify / race1 / race2: 16 = in progress; 32 = completed; 64 = sighting lap; 256 = pre-start; 512 = race over; 2048 = cancelled */
		int m_iSessionTime;									/* milliseconds. Current session time */
		int m_iNumEntries;									/* number of entries */
	} SPluginsRaceClassification_t;

	typedef struct
	{
		int m_iRaceNum;										/* race number */
		int m_iState;										/* 1 = DNS; 2 = unknown; 3 = retired; 4 = DSQ */
		int m_iBestLap;										/* milliseconds */
		int m_iBestLapNum;									/* best lap index */
		int m_iNumLaps;										/* number of laps */
		int m_iGap;											/* milliseconds */
		int m_iGapLaps;
		int m_iPenalty;										/* milliseconds */
		int m_iPit;											/* 0 = on track; 1 = in the pits */
	} SPluginsRaceClassificationEntry_t;

	typedef struct
	{
		int m_iRaceNum;										/* race number */
		float m_fPosX, m_fPosY, m_fPosZ;						/* meters */
		float m_fYaw;										/* angle from north. degrees */
		float m_fTrackPos;									/* position on the centerline, from 0 to 1 */
		int m_iCrashed;
	} SPluginsRaceTrackPosition_t;

	typedef struct
	{
		int m_iRaceNum;										/* race number */
		int m_iActive;										/* if set to 0, the vehicle is not active and the following fields are not set */
		int m_iRPM;											/* engine RPM */
		int m_iGear;										/* 0 = Neutral */
		float m_fSpeedometer;								/* meters/second */
		float m_fThrottle;									/* 0 to 1 */
		float m_fFrontBrake;								/* 0 to 1 */
		float m_fLean;										/* degrees. Negative = left */
	} SPluginsRaceVehicleData_t;

	/******************************************************************************
	structures and functions to control the replay
	******************************************************************************/

	typedef struct
	{
		int m_iRaceNum;
		char m_szName[100];
	} SPluginsSpectateVehicle_t;

	// Function declarations
	__declspec(dllexport) const char* GetModID();
	__declspec(dllexport) int GetModDataVersion();
	__declspec(dllexport) int GetInterfaceVersion();

	/* called when software is started */
	__declspec(dllexport) int Startup(char* _szSavePath);

	/* called when software is closed */
	__declspec(dllexport) void Shutdown();

	/* called when event is initialized. This function is optional */
	__declspec(dllexport) void EventInit(void* _pData, int _iDataSize);

	/* called when event is closed. This function is optional */
	__declspec(dllexport) void EventDeinit();

	/* called when bike goes to track. This function is optional */
	__declspec(dllexport) void RunInit(void* _pData, int _iDataSize);

	/* called when bike leaves the track. This function is optional */
	__declspec(dllexport) void RunDeinit();

	/* called when simulation is started / resumed. This function is optional */
	__declspec(dllexport) void RunStart();

	/* called when simulation is paused. This function is optional */
	__declspec(dllexport) void RunStop();

	/* called when a new lap is recorded. This function is optional */
	__declspec(dllexport) void RunLap(void* _pData, int _iDataSize);

	/* called when a split is crossed. This function is optional */
	__declspec(dllexport) void RunSplit(void* _pData, int _iDataSize);

	/* _fTime is the ontrack time, in seconds. _fPos is the position on centerline, from 0 to 1. This function is optional */
	__declspec(dllexport) void RunTelemetry(void* _pData, int _iDataSize, float _fTime, float _fPos);

	/*
	called when software is started.
	Set _piNumSprites to the number of zero-separated filenames in _pszSpriteName.
	Set _piNumFonts to the number of zero-separated filenames in _pszFontName.
	The base path for the sprite and font files is the plugins folder.
	This function is optional
	*/
	__declspec(dllexport) int DrawInit(int* _piNumSprites, char** _pszSpriteName, int* _piNumFonts, char** _pszFontName);

	/*
	_iState: 0 = on track; 1 = spectate; 2 = replay.
	Set _piNumQuads to the number of quads to draw.
	Set _ppQuad to an array of SPluginQuad_t structures.
	Set _piNumString to the number of strings to draw.
	Set _ppString to an array of SPluginString_t structures.
	This function is optional
	*/
	__declspec(dllexport) void Draw(int _iState, int* _piNumQuads, void** _ppQuad, int* _piNumString, void** _ppString);

	/*
	_pRaceData is a pointer to a float array with the longitudinal position of the start / finish line, splits and holeshot.
	This function is optional
	*/
	__declspec(dllexport) void TrackCenterline(int _iNumSegments, SPluginsTrackSegment_t* _pasSegment, void* _pRaceData);

	/* called when event is initialized or a replay is loaded. This function is optional */
	__declspec(dllexport) void RaceEvent(void* _pData, int _iDataSize);

	/* called when event is closed. This function is optional */
	__declspec(dllexport) void RaceDeinit();

	/* This function is optional */
	__declspec(dllexport) void RaceAddEntry(void* _pData, int _iDataSize);

	/* This function is optional */
	__declspec(dllexport) void RaceRemoveEntry(void* _pData, int _iDataSize);

	/* This function is optional */
	__declspec(dllexport) void RaceSession(void* _pData, int _iDataSize);

	/* This function is optional */
	__declspec(dllexport) void RaceSessionState(void* _pData, int _iDataSize);

	/* This function is optional */
	__declspec(dllexport) void RaceLap(void* _pData, int _iDataSize);

	/* This function is optional */
	__declspec(dllexport) void RaceSplit(void* _pData, int _iDataSize);

	/* This function is optional */
	__declspec(dllexport) void RaceHoleshot(void* _pData, int _iDataSize);

	/* This function is optional */
	__declspec(dllexport) void RaceCommunication(void* _pData, int _iDataSize);

	/* The number of elements of _pArray if given by m_iNumEntries in _pData. This function is optional */
	__declspec(dllexport) void RaceClassification(void* _pData, int _iDataSize, void* _pArray, int _iElemSize);

	/* This function is optional */
	__declspec(dllexport) void RaceTrackPosition(int _iNumVehicles, void* _pArray, int _iElemSize);

	/* This function is optional */
	__declspec(dllexport) void RaceVehicleData(void* _pData, int _iDataSize);

	/* Return 1 if _piSelect is set, from 0 to _iNumVehicles - 1 */
	__declspec(dllexport) int SpectateVehicles(int _iNumVehicles, void* _pVehicleData, int _iCurSelection, int* _piSelect);

	/* Return 1 if _piSelect is set, from 0 to _iNumCameras - 1 */
	__declspec(dllexport) int SpectateCameras(int _iNumCameras, void* _pCameraData, int _iCurSelection, int* _piSelect);
}
