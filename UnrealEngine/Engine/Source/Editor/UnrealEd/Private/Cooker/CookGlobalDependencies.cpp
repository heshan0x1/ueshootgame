// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/CookGlobalDependencies.h"

#include "CookOnTheSide/CookLog.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "CoreGlobals.h"
#include "Hash/Blake3.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Serialization/UnversionedPropertySerializationInternal.h"
#include "UObject/CoreRedirects.h"

namespace UE::Cook
{
	extern FGuid CookIncrementalVersion;
	
	// This hash is calculated per platform from different settings and guids
	static TMap<const ITargetPlatform*, FBlake3Hash> GlobalHash;

	void CalculateGlobalDependenciesHash(const ITargetPlatform* Platform, const UCookOnTheFlyServer& COTFS)
	{
		UE::ConfigAccessTracking::FIgnoreScope Ignore;

		FBlake3 Hasher;

		Hasher.Update(&CookIncrementalVersion, sizeof(CookIncrementalVersion));

		UE_LOG(LogCook, Display, TEXT("CalculateGlobalDependenciesHash(%s):"), *Platform->PlatformName());
		{
			bool bValue = false;
			GConfig->GetBool(TEXT("Core.System"), TEXT("CanStripEditorOnlyExportsAndImports"), bValue, GEngineIni);
			Hasher.Update(&bValue, sizeof(bValue));
			UE_LOG(LogCook, Display, TEXT("\t[Engine]:Core.System.CanStripEditorOnlyExportsAndImports = %s"), bValue ? TEXT("True") : TEXT("False"));
		}

		{
			bool bValue = false;
			GConfig->GetBool(TEXT("Core.System"), TEXT("CanSkipEditorReferencedPackagesWhenCooking"), bValue, GEngineIni);
			Hasher.Update(&bValue, sizeof(bValue));
			UE_LOG(LogCook, Display, TEXT("\t[Engine]:Core.System.CanSkipEditorReferencedPackagesWhenCooking = %s"), bValue ? TEXT("True") : TEXT("False"));
		}

		{
			bool bCanUseUnversionedPropertySerialization = CanUseUnversionedPropertySerialization(Platform);
			Hasher.Update(&bCanUseUnversionedPropertySerialization, sizeof(bCanUseUnversionedPropertySerialization));
			UE_LOG(LogCook, Display, TEXT("\tCanUseUnversionedPropertySerialization = %s"), bCanUseUnversionedPropertySerialization ? TEXT("True") : TEXT("False"));
		}

		{
			TArray<FString> AssetList;
			GConfig->GetArray(TEXT("CookSettings"), TEXT("AutomaticOptionalInclusionAssetType"), AssetList, GEditorIni);
			for (const FString& AssetType : AssetList)
			{
				Hasher.Update(*AssetType, AssetType.NumBytesWithoutNull());
			}
			UE_LOG(LogCook, Display, TEXT("\t[Editor]:CookSettings.AutomaticOptionalInclusionAssetType = %s"), *FString::Join(AssetList, TEXT(", ")));
		}

		{
			bool bIsUnversioned = COTFS.IsCookFlagSet(ECookInitializationFlags::Unversioned);
			Hasher.Update(&bIsUnversioned, sizeof(bIsUnversioned));
			UE_LOG(LogCook, Display, TEXT("\tIsCookFlagSet(ECookInitializationFlags::Unversioned) = %s"), bIsUnversioned ? TEXT("True") : TEXT("False"));
		}

		{
			FBlake3 GlobalRedirectHasher;
			FCoreRedirects::AppendHashOfGlobalRedirects(GlobalRedirectHasher);

			FBlake3Hash GlobalRedirectHash = GlobalRedirectHasher.Finalize();
			Hasher.Update(&GlobalRedirectHash, sizeof(GlobalRedirectHash));

			FString GlobalRedirectHashString = LexToString(GlobalRedirectHash);
			UE_LOG(LogCook, Display, TEXT("\tFCoreRedirects::AppendHashOfGlobalRedirects = %s"), *GlobalRedirectHashString);
		}

		FBlake3Hash Hash = Hasher.Finalize();
		UE_LOG(LogCook, Display, TEXT("\tGlobalDependenciesHash(%s) = %s"), *Platform->PlatformName(), *LexToString(Hash));
		GlobalHash.Add(Platform, Hash);
	}

	FBlake3Hash GetGlobalDependenciesHash(const ITargetPlatform* Platform)
	{
		FBlake3Hash* Hash = GlobalHash.Find(Platform);
		if (!Hash)
		{
			UE_LOG(LogCook, Error, TEXT("CalculateGlobalDependenciesHash was not called for platform %s"), *Platform->PlatformName());
			return FBlake3Hash();
		}

		return *Hash;
	}
}
