// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FRange
{
	uint32	Begin;
	uint32	End;

	uint32	Num() const { return End - Begin; }

	bool operator<( const FRange& Other) const { return Begin < Other.Begin; }
};