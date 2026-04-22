// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILauncherProfileAutomatedTest.h"
#include "Dom/JsonObject.h"

class FLauncherProfileAutomatedTest final : public ILauncherProfileAutomatedTest
{
public:
	FLauncherProfileAutomatedTest( const TCHAR* InInternalName ) 
		: InternalName(InInternalName)
	{
	}

	FLauncherProfileAutomatedTest(const FJsonObject& Object)
	{
		Load(Object);
	}

	FLauncherProfileAutomatedTest( FArchive& Archive )
	{
		Serialize(Archive);
	}

	virtual void SetUATCommand( const TCHAR* InUATCommand ) override
	{
		UATCommand = InUATCommand;
	}

	virtual const FString& GetUATCommand() const override
	{
		return UATCommand;
	}

	virtual void SetTests( const TCHAR* InTests ) override
	{
		Tests = InTests;
	}
	virtual const FString& GetTests() const override
	{
		return Tests;
	}

	virtual void SetAdditionalCommandLine( const TCHAR* InAdditionalCommandLine ) override
	{
		AdditionalCommandLine = InAdditionalCommandLine;
	}
	virtual const FString& GetAdditionalCommandLine() const override
	{
		return AdditionalCommandLine;
	}

	virtual void SetEnabled( bool bInEnabled ) override
	{
		bEnabled = bInEnabled;
	}
	virtual bool IsEnabled() const override
	{
		return bEnabled;
	}

	virtual void SetPriority( int32 InPriority ) override
	{
		Priority = InPriority;
	}
	virtual int32 GetPriority() const override
	{
		return Priority;
	}

	virtual const TCHAR* GetInternalName() const override
	{
		return *InternalName;
	}

	virtual void Load(const FJsonObject& Object) override
	{
		InternalName          = Object.GetStringField(TEXT("InternalName"));
		UATCommand            = Object.GetStringField(TEXT("UATCommand"));
		Tests                 = Object.GetStringField(TEXT("Tests"));
		AdditionalCommandLine = Object.GetStringField(TEXT("AdditionalCommandLine"));
		bEnabled              = Object.GetBoolField(TEXT("Enabled"));
		Priority              = Object.GetIntegerField(TEXT("Priority"));

	}
	virtual void Save(TJsonWriter<>& Writer) override
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("InternalName"),          InternalName);
		Writer.WriteValue(TEXT("UATCommand"),            UATCommand);
		Writer.WriteValue(TEXT("Tests"),                 Tests);
		Writer.WriteValue(TEXT("AdditionalCommandLine"), AdditionalCommandLine);
		Writer.WriteValue(TEXT("Enabled"),               bEnabled);
		Writer.WriteValue(TEXT("Priority"),              Priority);
		Writer.WriteObjectEnd();
	}

	virtual void Serialize( FArchive& Archive ) override final
	{
		Archive << InternalName
				<< UATCommand
				<< Tests
				<< AdditionalCommandLine
				<< bEnabled
				<< Priority
		;
	}


protected:
	FString UATCommand = TEXT("RunUnreal");
	FString Tests; //-Test=xxx,yyy
	FString AdditionalCommandLine;

	bool bEnabled = true;
	int32 Priority = 0; // longer tests should generally have lower priority

	FString InternalName;
};


