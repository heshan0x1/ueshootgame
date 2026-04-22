// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/Object.h"
#include "Misc/NotNull.h"

#define UE_API METAHUMANCORETECH_API

enum class EMetaHumanImportDNAType : uint8
{
	Face,
	Body,
	Combined
};

struct FMetaHumanCommonDataUtils
{
	/** Returns a constructed path to face DNA file */
	static UE_API const FString GetFaceDNAFilesystemPath();
	
	/** Returns a constructed path to body DNA file */
	static UE_API const FString GetBodyDNAFilesystemPath();

	/** Returns a constructed path to combined DNA file */
	static UE_API const FString GetCombinedDNAFilesystemPath();

	/** Sets the specified post-processing anim BP on a skeletal mesh taking into account UE/UEFN routes */
	static UE_API void SetPostProcessAnimBP(TNotNull<class USkeletalMesh*> InSkelMesh, FStringView InPackageName);

	/** Returns a soft object ptr to a UObject of UControlRigBlueprint retrieved from asset registry of a specified face control rig */
	static UE_API TSoftObjectPtr<UObject> GetDefaultFaceControlRig(const FStringView InAssetPath);

	/** Returns a file system path to a dna file for specified import type */
	static UE_API FString GetArchetypeDNAPath(EMetaHumanImportDNAType InImportDNAType);

	// TODO: Remove duplication for common assets

	/** Returns the path to common face skeleton, stored in Animator plugin in long object path format */
	static UE_API FStringView GetAnimatorPluginFaceSkeletonPath();
	
	/** Returns the path to common face control board control rig, stored in Animator plugin in long object path format */
	static UE_API FStringView GetAnimatorPluginFaceControlRigPath();

	/** Returns the path to common face post-processing anim blueprint, stored in Animator plugin in long object path format */
	static UE_API FStringView GetAnimatorPluginFacePostProcessABPPath();

	/** Returns the path to common face skeleton, stored in Character plugin in long object path format */
	static UE_API FStringView GetCharacterPluginFaceSkeletonPath();

	/** Returns the path to body skeleton, stored in Character plugin in long object path format */
	static UE_API FStringView GetCharacterPluginBodySkeletonPath();

	/** Returns the path to common face control board control rig, stored in Character plugin in long object path format */
	static UE_API FStringView GetCharacterPluginFaceControlRigPath();

	/** Returns the path to common face post-processing anim blueprint, stored in Character plugin in long object path format */
	static UE_API FStringView GetCharacterPluginFacePostProcessABPPath();

	/** Returns the path to body post-processing anim blueprint, stored in Character plugin in long object path format */
	static UE_API FStringView GetCharacterPluginBodyPostProcessABPPath();
	
};

#undef UE_API
