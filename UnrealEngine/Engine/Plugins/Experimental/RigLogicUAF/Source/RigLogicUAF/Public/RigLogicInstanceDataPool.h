// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigLogicInstanceData.h"

namespace UE::UAF
{
	class FRigLogicInstanceDataPool
	{
	public:
		TMap<TWeakObjectPtr<const USkeletalMesh>, TArray<TSharedPtr<FRigLogicInstanceData>>> Datas;

		mutable FCriticalSection PoolAccessCriticalSection;

		TSharedPtr<FRigLogicInstanceData> RequestData(const UE::UAF::FReferencePose* InReferencePose);
		void FreeData(TWeakObjectPtr<const USkeletalMesh> SkeletalMesh, TSharedPtr<FRigLogicInstanceData> InData);

		void GarbageCollect();
		void Log();
	};
} // namespace UE::UAF