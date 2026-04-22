// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IOSTargetPlatformSettings.cpp: Implements the FIOSTargetPlatformSettings class.
=============================================================================*/

#include "IOSTargetPlatformSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/App.h"

#if WITH_ENGINE
#include "Engine/Texture.h"
#include "TextureResource.h"
#endif

/* FIOSTargetPlatformSettings structors
 *****************************************************************************/

FIOSTargetPlatformSettings::FIOSTargetPlatformSettings(bool bInIsTVOS, bool bInIsVisionOS)
	// override the ini name up in the base classes, which will go into the FTargetPlatformInfo
	: TTargetPlatformSettingsBase(nullptr, bInIsVisionOS ? TEXT("VisionOS") : nullptr)
	, bIsTVOS(bInIsTVOS)
	, bIsVisionOS(bInIsVisionOS)
{
#if WITH_ENGINE
	TextureLODSettings = nullptr; // TextureLODSettings are registered by the device profile.
	StaticMeshLODSettings.Initialize(this);
#endif // #if WITH_ENGINE
}

// Using GetConfigSystem instead of GetPlatformValueVariable here because RenderUtils calls this too early.
bool FIOSTargetPlatformSettings::GetIsDistanceField() const
{
	static TOptional<bool> bDistanceField;
	if (!bDistanceField.IsSet())
	{
		bDistanceField = false;
#if WITH_ENGINE
		static IConsoleVariable* CvarDistanceFields = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DistanceFields"));
		if (CvarDistanceFields != nullptr)
		{
			bDistanceField = CvarDistanceFields->GetInt() != 0;
		}

		GetConfigSystem()->GetBool(TEXT("/Script/Engine.RendererSettings"), TEXT("r.DistanceFields"), bDistanceField.GetValue(), GEngineIni);
#endif
	}
	return bDistanceField.GetValue();
}

int32 FIOSTargetPlatformSettings::GetMobileShadingPath() const
{
	static TOptional<int32> MobileShadingPath;
	if (!MobileShadingPath.IsSet())
	{
		MobileShadingPath = 0;
#if WITH_ENGINE
		static IConsoleVariable* CvarShadingPath = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.ShadingPath"));
		if (CvarShadingPath != nullptr)
		{
			MobileShadingPath = CvarShadingPath->GetPlatformValueVariable(*IniPlatformName())->GetInt();
		}
#endif
	}
	return MobileShadingPath.GetValue();
}

bool FIOSTargetPlatformSettings::GetIsMobileForwardEnableClusteredReflections() const
{
	static TOptional<bool> bMobileForwardEnableClusteredReflections;
	if (!bMobileForwardEnableClusteredReflections.IsSet())
	{
		bMobileForwardEnableClusteredReflections = false;
#if WITH_ENGINE
		static IConsoleVariable* CvarFwdEnableClusteredReflections = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.Forward.EnableClusteredReflections"));
		if (CvarFwdEnableClusteredReflections != nullptr)
		{
			bMobileForwardEnableClusteredReflections = CvarFwdEnableClusteredReflections->GetPlatformValueVariable(*IniPlatformName())->GetInt() != 0;
		}
#endif
	}
	return bMobileForwardEnableClusteredReflections.GetValue();
}

bool FIOSTargetPlatformSettings::UsesDistanceFields() const
{
	return GetIsDistanceField();
}

FIOSTargetPlatformSettings::~FIOSTargetPlatformSettings()
{
}

/* ITargetPlatform interface
 *****************************************************************************/
static bool SupportsMetal()
{
	// default to NOT supporting metal
	bool bSupportsMetal = false;
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetal"), bSupportsMetal, GEngineIni);
	return bSupportsMetal;
}

// MetalMRT (Multiple Render Target) represent the SM5 variant of metal for IOS 
static bool SupportsMetalMRT()
{
	// default to NOT supporting metal MRT
	bool bSupportsMetalMRT = false;
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetalMRT"), bSupportsMetalMRT, GEngineIni);
	return bSupportsMetalMRT;
}

static bool SupportsA8Devices()
{
    // default to NOT supporting A8 devices
    bool bSupportAppleA8 = false;
    GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportAppleA8"), bSupportAppleA8, GEngineIni);
    return bSupportAppleA8;
}

bool FIOSTargetPlatformSettings::SupportsFeature( ETargetPlatformFeatures Feature ) const
{
	switch (Feature)
	{
		case ETargetPlatformFeatures::Packaging:
		case ETargetPlatformFeatures::DeviceOutputLog:
			return true;

		case ETargetPlatformFeatures::MobileRendering:
		case ETargetPlatformFeatures::LowQualityLightmaps:
			return SupportsMetal();
			
		case ETargetPlatformFeatures::DeferredRendering:
		case ETargetPlatformFeatures::HighQualityLightmaps:
			return SupportsMetalMRT();

		case ETargetPlatformFeatures::DistanceFieldAO:
			return UsesDistanceFields();

		case ETargetPlatformFeatures::NormalmapLAEncodingMode:
		{
			static IConsoleVariable* CompressorCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("cook.ASTCTextureCompressor"));
			const bool bUsesARMCompressor = (CompressorCVar ? (CompressorCVar->GetInt() != 0) : false);
			return bUsesARMCompressor;
		}

		case ETargetPlatformFeatures::SupportsMultipleConnectionTypes:
			return true;

		default:
			break;
	}
	
	return TTargetPlatformSettingsBase<FIOSPlatformProperties>::SupportsFeature(Feature);
}

void FIOSTargetPlatformSettings::GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const
{
	static FName NAME_SF_METAL_ES3_1_IOS(TEXT("SF_METAL_ES3_1_IOS"));
	static FName NAME_SF_METAL_SIM(TEXT("SF_METAL_SIM"));
	static FName NAME_SF_METAL_SM5_IOS(TEXT("SF_METAL_SM5_IOS"));
	static FName NAME_SF_METAL_ES3_1_TVOS(TEXT("SF_METAL_ES3_1_TVOS"));
	static FName NAME_SF_METAL_SM5_TVOS(TEXT("SF_METAL_SM5_TVOS"));

	if (bIsTVOS)
	{
		if (SupportsMetalMRT())
		{
			OutFormats.AddUnique(NAME_SF_METAL_SM5_TVOS);
		}

		// because we are currently using IOS settings, we will always use metal, even if Metal isn't listed as being supported
		// however, if MetalMRT is specific and Metal is set to false, then we will just use MetalMRT
		if (SupportsMetal() || !SupportsMetalMRT())
		{
			OutFormats.AddUnique(NAME_SF_METAL_ES3_1_TVOS);
		}
	}
	else
	{
		if (SupportsMetal())
		{
			OutFormats.AddUnique(NAME_SF_METAL_ES3_1_IOS);

			bool bEnableSimulatorSupport = false;
			GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bEnableSimulatorSupport"), bEnableSimulatorSupport, GEngineIni);
			if (bEnableSimulatorSupport)
			{
				OutFormats.AddUnique(NAME_SF_METAL_SIM);
			}
		}

		if (SupportsMetalMRT())
		{
			OutFormats.AddUnique(NAME_SF_METAL_SM5_IOS);
		}
	}
}

void FIOSTargetPlatformSettings::GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const
{
	GetAllPossibleShaderFormats(OutFormats);
}

#if WITH_ENGINE

void FIOSTargetPlatformSettings::GetReflectionCaptureFormats( TArray<FName>& OutFormats ) const
{
	const bool bMobileDeferredShading = (GetMobileShadingPath() == 1);

	if (SupportsMetalMRT() || bMobileDeferredShading || GetIsMobileForwardEnableClusteredReflections())
	{
		OutFormats.Add(FName(TEXT("FullHDR")));
	}

	OutFormats.Add(FName(TEXT("EncodedHDR")));
}

const UTextureLODSettings& FIOSTargetPlatformSettings::GetTextureLODSettings() const
{
	return *TextureLODSettings;
}

#endif // WITH_ENGINE

