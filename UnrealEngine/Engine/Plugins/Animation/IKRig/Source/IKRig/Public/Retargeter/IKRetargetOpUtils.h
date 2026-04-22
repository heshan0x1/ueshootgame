// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "IKRetargetOps.h"
#include "IKRetargetSettings.h"
#include "Chaos/Character/CharacterGroundConstraintContainer.h"
#include "Rig/IKRigDefinition.h"
#include "Retargeter/IKRetargetChainMapping.h"

namespace IKRetargetOpUtils
{
	/**
	 * This is a convenience function for retarget ops that need to keep an
	 * array of custom chain settings in-sync with the chains in and IK Rig asset.
	 *
	 * NOTE: to use this, pass in an array of custom types T and ensure they
	 *
	 * 1. Implement these functions:
	 * FName GetName()
	 * void SetName(FName InChainName)
	 *
	 * 2. Provide a constructor that takes the chain name
	 */
	template<typename T>
	void SynchronizeChainSettingsWithIKRig(
		TArray<T>& InOutChainSettings,
		const FIKRetargetOpBase* InRetargetOp,
		const bool bSkipUnmappedChains,
		const bool bSkipNonIKChains)
	{
		// NOTE: this function early returns if there's no valid chains found: we do NOT clear the settings
		// this allows users to clear and reassign a different rig and potentially retain/restore compatible settings
		
		if (!InRetargetOp)
		{
			return;
		}

		const UIKRigDefinition* TargetIKRig = InRetargetOp->GetCustomTargetIKRig();
		if (!TargetIKRig)
		{
			return;
		}

		const TArray<FBoneChain>& TargetChains = TargetIKRig->GetRetargetChains();
		if (TargetChains.IsEmpty())
		{
			return;
		}

		const FRetargetChainMapping* ChainMapping = InRetargetOp->GetChainMapping();
		if (bSkipUnmappedChains && !ChainMapping)
		{
			return;
		}

		// find the target chains that children ops should deal with
		TArray<FName> RequiredTargetChains;
		for (const FBoneChain& TargetChain : TargetChains)
		{
			if (bSkipNonIKChains && TargetChain.IKGoalName == NAME_None)
			{
				continue; // skip non-IK chains
			}

			if (!bSkipUnmappedChains)
			{
				const FName SourceChain = ChainMapping->GetChainMappedTo(TargetChain.ChainName, ERetargetSourceOrTarget::Target);
				if (bSkipUnmappedChains && SourceChain == NAME_None)
				{
					continue; // skip unmapped chains
				}
			}
			
			RequiredTargetChains.Add(TargetChain.ChainName);
		}

		if (RequiredTargetChains.IsEmpty())
		{
			return;
		}

		// remove chains that are not required
		InOutChainSettings.RemoveAll([&RequiredTargetChains](const T& InChainSettings)
		{
			return !RequiredTargetChains.Contains(InChainSettings.GetName());
		});
    	
		// add any required chains not already present
		for (FName RequiredTargetChain : RequiredTargetChains)
		{
			bool bFound = false;
			for (const T& ChainSettings : InOutChainSettings)
			{
				if (ChainSettings.GetName() == RequiredTargetChain)
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				InOutChainSettings.Emplace(RequiredTargetChain);
			}
		}
	};

	/**
	 * This is a convenience function for retarget ops that need to respond to an IK Rig chain being renamed.
	 *
	 * NOTE: to use this, pass in an array of custom types T and ensure they implement these functions:
	 * FName GetName()
	 * void SetName(FName InChainName) */
	template<typename T>
	void OnRetargetChainRenamed(TArray<T>& InOutChainSettings, const FName InOldChainName, const FName InNewChainName)
	{
		for (T& ChainSettings : InOutChainSettings)
		{
			if (ChainSettings.GetName() == InOldChainName)
			{
				ChainSettings.SetName(InNewChainName);
			}
		}
	};
	
}
