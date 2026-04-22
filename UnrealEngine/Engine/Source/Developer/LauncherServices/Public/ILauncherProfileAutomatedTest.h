// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/JsonWriter.h"

class FJsonObject;
class ILauncherProfileLaunchRole;

/**
 * Automated test parameters
 */
class ILauncherProfileAutomatedTest
{
public:
	//@todo: comments

	// recommendation for InternalName is is "ExtensionName.InternalName"
	virtual void SetUATCommand( const TCHAR* UATCommand ) = 0;
	virtual const FString& GetUATCommand() const = 0;

	virtual void SetTests( const TCHAR* Tests ) = 0; //-Test=xxx,yyy
	virtual const FString& GetTests() const = 0;

	virtual void SetAdditionalCommandLine( const TCHAR* AdditionalCommandLine ) = 0;
	virtual const FString& GetAdditionalCommandLine() const = 0;

	virtual void SetEnabled( bool bEnabled ) = 0;
	virtual bool IsEnabled() const = 0;

	virtual void SetPriority( int32 Priority ) = 0; // longer tests should generally have lower priority
	virtual int32 GetPriority() const = 0;

	virtual const TCHAR* GetInternalName() const = 0;

	virtual void Load(const FJsonObject& Object) = 0;
	virtual void Save(TJsonWriter<>& Writer) = 0;
	virtual void Serialize( FArchive& Archive ) = 0;


	virtual ~ILauncherProfileAutomatedTest() = default;
};

typedef TSharedPtr<ILauncherProfileAutomatedTest> ILauncherProfileAutomatedTestPtr;
typedef TSharedRef<ILauncherProfileAutomatedTest> ILauncherProfileAutomatedTestRef;
