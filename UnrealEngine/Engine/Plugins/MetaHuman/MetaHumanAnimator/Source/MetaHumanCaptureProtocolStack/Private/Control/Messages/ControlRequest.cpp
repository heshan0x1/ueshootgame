// Copyright Epic Games, Inc. All Rights Reserved.

#include "Control/Messages/ControlRequest.h"
#include "Control/Messages/Constants.h"

FControlRequest::FControlRequest(FString InAddressPath)
    : AddressPath(MoveTemp(InAddressPath))
{
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
const FString& FControlRequest::GetAddressPath() const
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
    return AddressPath;
}

TSharedPtr<FJsonObject> FControlRequest::GetBody() const
{
    return nullptr;
}

FKeepAliveRequest::FKeepAliveRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GKeepAlive)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FStartSessionRequest::FStartSessionRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GStartSession)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FStopSessionRequest::FStopSessionRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GStopSession)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FGetServerInformationRequest::FGetServerInformationRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GGetServerInformation)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FSubscribeRequest::FSubscribeRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GSubscribe)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FUnsubscribeRequest::FUnsubscribeRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GUnsubscribe)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FGetStateRequest::FGetStateRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GGetState)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FStartRecordingTakeRequest::FStartRecordingTakeRequest(FString InSlateName, 
                                                       uint16 InTakeNumber, 
													   TOptional<FString> InSubject,
													   TOptional<FString> InScenario,
													   TOptional<TArray<FString>> InTags)
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GStartRecordingTake)
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
    , SlateName(MoveTemp(InSlateName))
    , TakeNumber(InTakeNumber)
    , Subject(MoveTemp(InSubject))
    , Scenario(MoveTemp(InScenario))
    , Tags(MoveTemp(InTags))
{
}

TSharedPtr<FJsonObject> FStartRecordingTakeRequest::GetBody() const
{
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Body->SetStringField(UE::CPS::Properties::GSlateName, SlateName);
    Body->SetNumberField(UE::CPS::Properties::GTakeNumber, TakeNumber);
	if (Subject.IsSet())
	{
		Body->SetStringField(UE::CPS::Properties::GSubject, Subject.GetValue());
	}
    
	if (Scenario.IsSet())
	{
		Body->SetStringField(UE::CPS::Properties::GScenario, Scenario.GetValue());
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
    
	if (Tags.IsSet())
	{
		TArray<TSharedPtr<FJsonValue>> TagsJson;
		for (const FString& Tag : Tags.GetValue())
		{
			TagsJson.Add(MakeShared<FJsonValueString>(Tag));
		}

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Body->SetArrayField(UE::CPS::Properties::GTags, TagsJson);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
    
    return Body;
}

FStopRecordingTakeRequest::FStopRecordingTakeRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GStopRecordingTake)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FAbortRecordingTakeRequest::FAbortRecordingTakeRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	: FControlRequest(UE::CPS::AddressPaths::GAbortRecordingTake)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FGetTakeListRequest::FGetTakeListRequest()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GGetTakeList)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
}

FGetTakeMetadataRequest::FGetTakeMetadataRequest(TArray<FString> InNames)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    : FControlRequest(UE::CPS::AddressPaths::GGetTakeMetadata)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    , Names(MoveTemp(InNames))
{
}

TSharedPtr<FJsonObject> FGetTakeMetadataRequest::GetBody() const
{
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();

    TArray<TSharedPtr<FJsonValue>> NamesJson;
    for (const FString& Name : Names)
    {
        NamesJson.Add(MakeShared<FJsonValueString>(Name));
    }

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Body->SetArrayField(UE::CPS::Properties::GNames, NamesJson);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

    return Body;
}
