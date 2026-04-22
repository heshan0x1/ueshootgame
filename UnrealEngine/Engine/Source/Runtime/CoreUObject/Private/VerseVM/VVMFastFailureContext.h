// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "VerseVM/VVMFrame.h"
#include "VerseVM/VVMWriteBarrier.h"
#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMCell.h"

namespace Verse
{

struct VFastFailureContext : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	uint32 Suspensions = 0;
	bool bFailed = false;
	FOp* ThenPC = nullptr;
	FOp* FailurePC = nullptr;
	FOp* DonePC = nullptr;
	TWriteBarrier<VFastFailureContext> Parent = {};
	TWriteBarrier<VFrame> CapturedFrame = {};

	VFastFailureContext(FAllocationContext Context)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
	{
	}

	static VFastFailureContext& New(FAllocationContext Context)
	{
		return *new (Context.AllocateFastCell(sizeof(VFastFailureContext))) VFastFailureContext(Context);
	}
};

} // namespace Verse
#endif // WITH_VERSE_VM
