// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

namespace UE::CPS::AddressPaths
{
// Requests/Responses
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GKeepAlive;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GStartSession;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GStopSession;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSubscribe;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GUnsubscribe;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GGetServerInformation;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GGetState;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GStartRecordingTake;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GStopRecordingTake;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GAbortRecordingTake;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GGetTakeList;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GGetTakeMetadata;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GExportTakeData;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GExportTakeVideoFrame;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GExportCameraFeedFrame;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GPauseExport;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GCancelExport;

// Updates
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSessionStopped;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GRecordingStatus;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTakeAdded;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTakeRemoved;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTakeUpdated;

// Platform Specific: IOS
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GDiskCapacity;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GBattery;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GThermalState;
}

namespace UE::CPS::Properties
{
// Keys
// Messages
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSessionId;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GAddressPath;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTransactionId;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTimestamp;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GType;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GBody;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GError;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GDescription;

// Requests
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSlateName;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTakeNumber;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSubject;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GScenario;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTags;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GNames;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTakeName;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GFiles;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GName;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GOffset;


// Responses
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GId;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GModel;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GPlatformName;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GPlatformVersion;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSoftwareName;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSoftwareVersion;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GExportPort;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GIsRecording;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GPlatformState;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTakes;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GDateTime;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GFrames;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GAppVersion;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GLength;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GVideo;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GFrameRate;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GHeight;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GWidth;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GAudio;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GChannels;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSampleRate;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GBitsPerChannel;

// Platform Specific: iOS
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTotalCapacity;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GRemainingCapacity;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GBatteryLevel;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GThermalState;

UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GTotal;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GRemaining;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GLevel;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GState;

// Values
// Messages
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GRequest;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GResponse;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GUpdate;

UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GIOS;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GAndroid;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GWindows;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GWindowsServer;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GLinux;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GMacOS;

// Platform Specific: iOS
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GNominal;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GFair;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GSerious;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GCritical;
}

namespace UE::CPS::ErrorNames
{
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GUnsupportedProtocolVersion;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GRequestFailed;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GRequestParsingFailed;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GNotSupported;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GNoSession;
UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
extern METAHUMANCAPTUREPROTOCOLSTACK_API const TCHAR* GMissingBody;
}