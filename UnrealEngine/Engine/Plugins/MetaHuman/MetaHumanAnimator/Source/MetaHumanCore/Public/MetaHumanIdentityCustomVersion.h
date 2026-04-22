// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

#define UE_API METAHUMANCORE_API

struct FMetaHumanIdentityCustomVersion
{
	enum Type : int32
	{
		// MetaHuman Identity was updated to use EditorBulkData
		EditorBulkDataUpdate = 0,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static UE_API const FGuid GUID;

private:

	FMetaHumanIdentityCustomVersion() = default;
};

#undef UE_API
