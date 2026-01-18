// ============================================================================
// vendor/piboso/krp_api.h
// Kart Racing Pro (KRP) Plugin API Header
// ============================================================================
// API Version: Interface 9, Data Version 6
// Source: KRP plugin SDK
// ============================================================================
#pragma once

extern "C" {

/******************************************************************************
structures and functions to receive data from the simulated kart
******************************************************************************/

typedef struct
{
	char m_szDriverName[100];
	char m_szKartID[100];
	char m_szKartName[100];
	int m_iDriveType;												/* 0 = direct; 1 = clutch; 2 = shifter */
	int m_iNumberOfGears;
	int m_iMaxRPM;
	int m_iLimiter;
	int m_iShiftRPM;
	int m_iEngineCooling;											/* 0 = aircooled; 1 = watercooled */
	float m_fEngineOptTemperature;									/* degrees Celsius */
	float m_afEngineTemperatureAlarm[2];							/* degrees Celsius. Lower and upper limits */
	float m_fMaxFuel;												/* liters */
	char m_szCategory[100];
	char m_szDash[100];
	char m_szTrackID[100];
	char m_szTrackName[100];
	float m_fTrackLength;											/* centerline length. meters */
	int m_iType;													/* 1 = testing; 2 = race; 4 = challenge */
} SPluginsKRPKartEvent_t;

typedef struct
{
	int m_iSession;													/* testing: always 0. Race: 1 = practice; 2 = qualify; 3 = warmup; 4 = qualify heat; 5 = second chance heat; 6 = prefinal; 7 = final. Challenge: 0 = waiting; 1 = practice; 2 = race */
	int m_iSessionSeries;
	int m_iConditions;												/* 0 = sunny; 1 = cloudy; 2 = rainy */
	float m_fAirTemperature;										/* degrees Celsius */
	float m_fTrackTemperature;										/* degrees Celsius */
	char m_szSetupFileName[100];
} SPluginsKRPKartSession_t;

typedef struct
{
	int m_iRPM;														/* engine rpm */
	float m_fCylinderHeadTemperature;								/* degrees Celsius */
	float m_fWaterTemperature;										/* degrees Celsius */
	int m_iGear;													/* 0 = Neutral */
	float m_fFuel;													/* liters */
	float m_fSpeedometer;											/* meters/second */
	float m_fPosX,m_fPosY,m_fPosZ;									/* world position of a reference point attached to chassis ( not CG ) */
	float m_fVelocityX,m_fVelocityY,m_fVelocityZ;					/* velocity of CG in world coordinates. meters/second */
	float m_fAccelerationX,m_fAccelerationY,m_fAccelerationZ;		/* acceleration of CG local to chassis rotation, expressed in G ( 9.81 m/s2 ) and averaged over the latest 10ms */
	float m_aafRot[3][3];											/* rotation matrix of the chassis */
	float m_fYaw,m_fPitch,m_fRoll;									/* degrees, -180 to 180 */
	float m_fYawVelocity,m_fPitchVelocity,m_fRollVelocity;			/* degress / second */
	float m_fInputSteer;											/* degrees. Negative = left */
	float m_fInputThrottle;											/* 0 to 1 */
	float m_fInputBrake;											/* 0 to 1 */
	float m_fInputFrontBrakes;										/* 0 to 1 */
	float m_fInputClutch;											/* 0 to 1. 0 = Fully engaged */
	float m_afWheelSpeed[4];										/* meters/second. 0 = front-left; 1 = front-right; 2 = rear-left; 3 = rear-right */
	int m_aiWheelMaterial[4];										/* material index. 0 = not in contact */
	float m_fSteerTorque;											/* Nm */
} SPluginsKRPKartData_t;

typedef struct
{
	int m_iLapNum;													/* lap index */
	int m_iInvalid;
	int m_iLapTime;													/* milliseconds */
	int m_iPos;														/* 1 = best lap */
} SPluginsKRPKartLap_t;

typedef struct
{
	int m_iSplit;													/* split index */
	int m_iSplitTime;												/* milliseconds */
	int m_iBestDiff;												/* milliseconds. Difference with best lap */
} SPluginsKRPKartSplit_t;

/******************************************************************************
structures and functions to draw
******************************************************************************/

typedef struct
{
	float m_aafPos[4][2];			/* 0,0 -> top left. 1,1 -> bottom right. counter-clockwise */
	int m_iSprite;					/* 1 based index in SpriteName buffer. 0 = fill with m_ulColor */
	unsigned long m_ulColor;		/* ABGR */
} SPluginsKRPQuad_t;

typedef struct
{
	char m_szString[100];
	float m_afPos[2];				/* 0,0 -> top left. 1,1 -> bottom right */
	int m_iFont;					/* 1 based index in FontName buffer */
	float m_fSize;
	int m_iJustify;					/* 0 = left; 1 = center; 2 = right */
	unsigned long m_ulColor;		/* ABGR */
} SPluginsKRPString_t;

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
} SPluginsKRPTrackSegment_t;

/******************************************************************************
structures and functions to receive the race data
******************************************************************************/

typedef struct
{
	int m_iType;										/* -1 = replay; 1 = testing; 2 = race; 4 = challenge */
	char m_szName[100];
	char m_szTrackName[100];
	float m_fTrackLength;								/* meters */
} SPluginsKRPRaceEvent_t;

typedef struct
{
	int m_iRaceNum;										/* unique race number */
	char m_szName[100];
	char m_szKartName[100];
	char m_szKartShortName[100];
	char m_szCategory[100];
	int m_iUnactive;									/* if set to 1, the driver left the event and the following fields are not set */
	int m_iNumberOfGears;
	int m_iMaxRPM;
} SPluginsKRPRaceAddEntry_t;

typedef struct
{
	int m_iRaceNum;										/* race number */
} SPluginsKRPRaceRemoveEntry_t;

typedef struct
{
	int m_iSession;										/* testing: always 0. Race: 1 = practice; 2 = qualify; 3 = warmup; 4 = qualify heat; 5 = second chance heat; 6 = prefinal; 7 = final. Challenge: 0 = waiting; 1 = practice; 2 = race */
	int m_iSessionSeries;
	int m_iGroup1,m_iGroup2;							/* 0 = A, 1 = B, 2 = C, ... Only used for Qualify Heats */
	int m_iSessionState;								/* testing / waiting: always 0. practice / qualify / warmup: 16 = in progress; 32 = completed. qualify heat / second chance heat / prefinal / final: 16 = in progress; 32 = semaphore; 64 = sighting lap; 128 = warmup lap; 256 = pre-start; 512 = race over; 1024 = completed; 2048 = rolling start */
	int m_iSessionLength;								/* milliseconds. 0 = no limit */
	int m_iSessionNumLaps;
	int m_iNumEntries;
	int m_aiEntries[50];
	int m_aiGrid[50];
	int m_iConditions;									/* 0 = sunny; 1 = cloudy; 2 = rainy */
	float m_fAirTemperature;							/* degrees Celsius */
	float m_fTrackTemperature;							/* degrees Celsius */
} SPluginsKRPRaceSession_t;

typedef struct
{
	int m_iSession;										/* testing: always 0. Race: 1 = practice; 2 = qualify; 3 = warmup; 4 = qualify heat; 5 = second chance heat; 6 = prefinal; 7 = final. Challenge: 0 = waiting; 1 = practice; 2 = race */
	int m_iSessionSeries;
	int m_iSessionState;								/* testing / waiting: always 0. practice / qualify / warmup: 16 = in progress; 32 = completed. qualify heat / second chance heat / prefinal / final: 16 = in progress; 32 = semaphore; 64 = sighting lap; 128 = warmup lap; 256 = pre-start; 512 = race over; 1024 = completed; 2048 = rolling start */
	int m_iSessionLength;								/* milliseconds. 0 = no limit */
} SPluginsKRPRaceSessionState_t;

typedef struct
{
	int m_iSession;										/* testing: always 0. Race: 1 = practice; 2 = qualify; 3 = warmup; 4 = qualify heat; 5 = second chance heat; 6 = prefinal; 7 = final. Challenge: 0 = waiting; 1 = practice; 2 = race */
	int m_iSessionSeries;
	int m_iRaceNum;										/* race number */
	int m_iLapNum;										/* lap index */
	int m_iInvalid;
	int m_iLapTime;										/* milliseconds */
	int m_aiSplit[2];									/* milliseconds */
	float m_fSpeed;										/* meters/second */
	int m_iBest;										/* 1 = personal best lap; 2 = overall best lap */
} SPluginsKRPRaceLap_t;

typedef struct
{
	int m_iSession;										/* testing: always 0. Race: 1 = practice; 2 = qualify; 3 = warmup; 4 = qualify heat; 5 = second chance heat; 6 = prefinal; 7 = final. Challenge: 0 = waiting; 1 = practice; 2 = race */
	int m_iSessionSeries;
	int m_iRaceNum;										/* race number */
	int m_iLapNum;										/* lap index */
	int m_iSplit;										/* split index */
	int m_iSplitTime;									/* milliseconds */
} SPluginsKRPRaceSplit_t;

typedef struct
{
	int m_iSession;										/* testing: always 0. Race: 1 = practice; 2 = qualify; 3 = warmup; 4 = qualify heat; 5 = second chance heat; 6 = prefinal; 7 = final. Challenge: 0 = waiting; 1 = practice; 2 = race */
	int m_iSessionSeries;
	int m_iRaceNum;										/* race number */
	int m_iLapNum;										/* lap index */
	float m_fSpeed;										/* meters/second */
} SPluginsKRPRaceSpeed_t;

typedef struct
{
	int m_iSession;										/* testing: always 0. Race: 1 = practice; 2 = qualify; 3 = warmup; 4 = qualify heat; 5 = second chance heat; 6 = prefinal; 7 = final. Challenge: 0 = waiting; 1 = practice; 2 = race */
	int m_iSessionSeries;
	int m_iRaceNum;										/* race number */
	int m_iCommunication;								/* 1 = change state; 2 = penalty */
	int m_iState;										/* 1 = DNS; 2 = retired; 3 = DSQ */
	int m_iReason;										/* Reason for DSQ. 0 = jump start; 1 = too many offences; 3 = rolling start speeding; 4 = rolling start too slow; 5 = rolling start corridor crossing; 6 = rolling start overtaking; 7 = director */
	int m_iOffence;										/* 1 = jump start; 3 = cutting; 4 = rolling start speeding; 5 = rolling start too slow; 6 = rolling start corridor crossing; 7 = rolling start overtaking */
	int m_iLap;											/* lap index */
	int m_iType;										/* 1 = time penalty; 2 = position penalty */
	int m_iTime;										/* milliseconds. Penalty time */
} SPluginsKRPRaceCommunication_t;

typedef struct
{
	int m_iSession;										/* testing: always 0. Race: 1 = practice; 2 = qualify; 3 = warmup; 4 = qualify heat; 5 = second chance heat; 6 = prefinal; 7 = final. Challenge: 0 = waiting; 1 = practice; 2 = race */
	int m_iSessionSeries;
	int m_iSessionState;								/* testing / waiting: always 0. practice / qualify / warmup: 16 = in progress; 32 = completed. qualify heat / second chance heat / prefinal / final: 16 = in progress; 32 = semaphore; 64 = sighting lap; 128 = warmup lap; 256 = pre-start; 512 = race over; 1024 = completed; 2048 = rolling start */
	int m_iSessionTime;									/* milliseconds. Current session time */
	int m_iNumEntries;									/* number of entries */
} SPluginsKRPRaceClassification_t;

typedef struct
{
	int m_iRaceNum;										/* race number */
	int m_iState;										/* 1 = DNS; 2 = retired; 3 = DSQ */
	int m_iBestLap;										/* milliseconds */
	float m_fBestSpeed;									/* meters/second */
	int m_iBestLapNum;									/* best lap index */
	int m_iNumLaps;										/* number of laps */
	int m_iGap;											/* milliseconds */
	int m_iGapLaps;
	int m_iPenalty;										/* milliseconds */
	int m_iPit;											/* 0 = on track; 1 = in the pits */
} SPluginsKRPRaceClassificationEntry_t;

typedef struct
{
	int m_iRaceNum;										/* race number */
	float m_fPosX,m_fPosY,m_fPosZ;						/* meters */
	float m_fYaw;										/* angle from north. degrees */
	float m_fTrackPos;									/* position on the centerline, from 0 to 1 */
} SPluginsKRPRaceTrackPosition_t;

typedef struct
{
	int m_iRaceNum;										/* race number */
	int m_iActive;										/* if set to 0, the vehicle is not active and the following fields are not set */
	int m_iRPM;											/* engine RPM */
	int m_iGear;										/* 0 = Neutral */
	float m_fSpeedometer;								/* meters/second */
	float m_fSteer;										/* -1 ( left ) to 1 ( right ) */
	float m_fThrottle;									/* 0 to 1 */
	float m_fBrake;										/* 0 to 1 */
} SPluginsKRPRaceVehicleData_t;

/******************************************************************************
structures and functions to control the replay
******************************************************************************/

typedef struct
{
	int m_iRaceNum;
	char m_szName[100];
} SPluginsKRPSpectateVehicle_t;

} // extern "C"
