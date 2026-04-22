// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Verse
{
class IEngineEnvironment;

class VerseVM
{
public:
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	COREUOBJECT_API static void Startup();
	COREUOBJECT_API static void Shutdown();
#endif // WITH_VERSE_VM
	COREUOBJECT_API static IEngineEnvironment* GetEngineEnvironment();
	COREUOBJECT_API static void SetEngineEnvironment(IEngineEnvironment* Environment);
	COREUOBJECT_API static bool IsUHTGeneratedVerseVNIObject(UObject* Object);
};

// The Verse VM has its own version for FORCEINLINE. The Verse VM is careful about using this
// in places where inlining measurably improves performance.
#if UE_BUILD_DEBUG
#define V_FORCEINLINE inline
#elif PLATFORM_COMPILER_CLANG
#define V_FORCEINLINE inline __attribute__((always_inline))
#else
#define V_FORCEINLINE __forceinline
#endif // UE_BUILD_DEBUG

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#define V_AUTORTFM_OPEN AUTORTFM_OPEN
#else
#define V_AUTORTFM_OPEN
#endif

} // namespace Verse
