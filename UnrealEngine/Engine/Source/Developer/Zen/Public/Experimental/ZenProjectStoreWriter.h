// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "Experimental/ZenGlobals.h"
#include "Serialization/Archive.h"
#include "Templates/UniquePtr.h"

#define UE_API ZEN_API

#if UE_WITH_ZEN

namespace UE::Zen
{

class FZenProjectStoreWriter
{
public:
	UE_API FZenProjectStoreWriter(const TCHAR* TargetFileName);
	~FZenProjectStoreWriter() = default;

	UE_API bool Write(FStringView ServiceHostName, uint16 ServicePort, bool bIsRunningLocally, FStringView ProjectId, FStringView OplogId, FName TargetPlatformName);
private:
	TUniquePtr<FArchive> OplogMarker;
};

} // namespace UE

#endif // UE_WITH_ZEN

#undef UE_API
