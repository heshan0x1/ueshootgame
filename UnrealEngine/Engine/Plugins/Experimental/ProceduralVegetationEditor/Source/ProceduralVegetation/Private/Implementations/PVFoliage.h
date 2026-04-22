// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumRange.h"

#include "GeometryCollection/ManagedArrayCollection.h"

#include "PVFoliage.generated.h"

struct FPVFloatRamp;

UENUM(BlueprintType)
enum class EPhyllotaxyType : uint8
{
	Alternate UMETA(DisplayName = "Alternate"),
	Opposite UMETA(DisplayName = "Opposite"),
	Decussate UMETA(DisplayName = "Decussate"),
	Whorled UMETA(DisplayName = "Whorled"),
	Spiral UMETA(DisplayName = "Spiral")
};

UENUM(BlueprintType)
enum class EPhyllotaxyFormation : uint8
{
	Distichous UMETA(DisplayName = "Distichous"),
	Tristichous UMETA(DisplayName = "Tristichous"),
	Pentastichous UMETA(DisplayName = "Pentastichous"),
	Octastichous UMETA(DisplayName = "Octastichous"),
	Parastichous UMETA(DisplayName = "Parastichous")
};

struct FDistributionSettings
{
	const float EthyleneThreshold;
	const bool OverrideDistribution;
	const float InstanceSpacing;
	const FPVFloatRamp* InstanceSpacingRamp;
	const float InstanceSpacingRampEffect;
	const int32 MaxPerBranch;
};

struct FScaleSettings
{
	const float BaseScale;
	const float BranchScaleImpact;
	const float MinScale;
	const float MaxScale;
	const float RandomScaleMin;
	const float RandomScaleMax;
	const FPVFloatRamp* ScaleRamp;
};

struct FVectorSettings
{
	const bool OverrideAxilAngle;
	const float AxilAngle;
	const FPVFloatRamp* AxilAngleRamp;
	const float AxilAngleRampUpperValue;
	const float AxilAngleRampEffect;
};

struct FPhyllotaxySettings
{
	const bool OverridePhyllotaxy;
	const EPhyllotaxyType PhyllotaxyType;
	const EPhyllotaxyFormation PhyllotaxyFormation;
	const int32 MinimumNodeBuds;
	const int32 MaximumNodeBuds;
	const float PhyllotaxyAdditionalAngle;
};

USTRUCT()
struct FPVFoliageDistributionConditionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=""))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(ClampMin=-1.0f, ClampMax=1.0f, UIMin=-1.0f, UIMax=1.0f, Tooltip=""))
	float Offset = 0.0f;

	FString ToString() const
	{
		return FString::Printf(TEXT("Weight = %f, Offset = %f"), Weight, Offset);
	}
};

UENUM()
enum class EFoliageDistributionCondition : uint8
{
	Light,
	Scale,
	UpAlignment,
	Tip,
	Health,
	Count UMETA(Hidden),
	None UMETA(Hidden),
};

ENUM_RANGE_BY_COUNT(EFoliageDistributionCondition, EFoliageDistributionCondition::Count)


USTRUCT()
struct FPVFoliageDistributionConditions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=""))
	float CutoffThreshold = 0.3f;

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(ClampMin=1, ClampMax=10, UIMin=0, UIMax=10, Tooltip=""))
	int32 MinimumCandidates = 1;
	
	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(PinHiddenByDefault, InlineEditConditionToggle))
	bool bActivateLight = false;

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(EditCondition="bActivateLight", Tooltip=""))
	FPVFoliageDistributionConditionSettings Light;

	UPROPERTY()
	bool bActivateScale = false;

	UPROPERTY()
	FPVFoliageDistributionConditionSettings Scale;

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(PinHiddenByDefault, InlineEditConditionToggle))
	bool bActivateUpAlignment = false;

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(EditCondition="bActivateUpAlignment", Tooltip=""))
	FPVFoliageDistributionConditionSettings UpAlignment;

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(PinHiddenByDefault, InlineEditConditionToggle))
	bool bActivateTip = false;
	
	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(EditCondition="bActivateTip", Tooltip=""))
	FPVFoliageDistributionConditionSettings Tip;

	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(PinHiddenByDefault, InlineEditConditionToggle))
	bool bActivateHealth = false;
	
	UPROPERTY(EditAnywhere, Category="ConditionSettings", meta=(EditCondition="bActivateHealth", Tooltip=""))
	FPVFoliageDistributionConditionSettings Health;

	bool HasActiveCondition() const
	{
		for (EFoliageDistributionCondition Condition : TEnumRange<EFoliageDistributionCondition>())
		{
			if (IsActiveCondition(Condition))
			{
				return true;
			}
		}
		return false;
	}

	bool IsActiveCondition(const EFoliageDistributionCondition InCondition) const 
	{
		switch (InCondition)
		{
		case EFoliageDistributionCondition::Light:
			return bActivateLight && Light.Weight > 0.0f;
			
		case EFoliageDistributionCondition::Scale:
			return bActivateScale && Scale.Weight > 0.0f; 
			
		case EFoliageDistributionCondition::UpAlignment:
			return bActivateUpAlignment && UpAlignment.Weight > 0.0f;
			
		case EFoliageDistributionCondition::Tip:
			return bActivateTip && Tip.Weight > 0.0f;

		case EFoliageDistributionCondition::Health:
			return bActivateHealth && Health.Weight > 0.0f;

		default:
			return false;
		}
	}

	void SetConditionState(const EFoliageDistributionCondition InCondition, const bool InNewState) 
	{
		switch (InCondition)
		{
		case EFoliageDistributionCondition::Light:
			bActivateLight = InNewState;
			break;
			
			
		case EFoliageDistributionCondition::Scale:
			bActivateScale = InNewState;
			break;
			
		case EFoliageDistributionCondition::UpAlignment:
			bActivateUpAlignment = InNewState;
			break;
			
		case EFoliageDistributionCondition::Tip:
			bActivateTip = InNewState;
			break;

		case EFoliageDistributionCondition::Health:
			bActivateHealth = InNewState;
			break;

			default:
			check(false);
		}
	}

	FPVFoliageDistributionConditionSettings* GetSettings(const EFoliageDistributionCondition InCondition)
	{
		switch (InCondition)
		{
			case EFoliageDistributionCondition::Light:
				return &Light;
				
			case EFoliageDistributionCondition::Scale:
				return &Scale;
			
				
			case EFoliageDistributionCondition::UpAlignment:
				return &UpAlignment;
				
			case EFoliageDistributionCondition::Tip:
				return &Tip;

			case EFoliageDistributionCondition::Health:
				return &Health;

			default:
			return nullptr;
		}
	}

	bool GetSettings(const EFoliageDistributionCondition InCondition, FPVFoliageDistributionConditionSettings& OutSettings) const
	{
		switch (InCondition)
		{
			case EFoliageDistributionCondition::Light:
			{
				OutSettings =  Light;
				return true;
			}
			
			case EFoliageDistributionCondition::Scale:
			{
				OutSettings = Scale;
				return true;
			}
			
			case EFoliageDistributionCondition::UpAlignment:
			{
				OutSettings = UpAlignment;
				return true;
			}
			
			case EFoliageDistributionCondition::Tip:
			{
				OutSettings = Tip;
				return true;
			}

			case EFoliageDistributionCondition::Health:
			{
				OutSettings = Health;
				return true;
			}
		}

		return false;
	}
};

struct FMiscSettings
{
	const int32 RandomSeed;
};


USTRUCT()
struct FPVFoliageAttributes
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Info", meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=""))
	float Light = 0.0f;

	UPROPERTY()
	float Scale = 0.0f;

	UPROPERTY(EditAnywhere, Category="Info", meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=""))
	float UpAlignment = 0.0f;

	UPROPERTY(EditAnywhere, Category="Info", meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip=""))
	float Health = 0.0f;

	UPROPERTY(EditAnywhere, Category="Info", meta=(Tooltip=""))
	bool Tip = false;

	float GetAttributeValue(const EFoliageDistributionCondition InCondition) const
	{
		switch (InCondition)
		{
			case EFoliageDistributionCondition::Light:
			return Light;
			
			case EFoliageDistributionCondition::Scale:
			return Scale;
			
			case EFoliageDistributionCondition::UpAlignment:
			return UpAlignment;
			
			case EFoliageDistributionCondition::Tip:
			return Tip ? 1.0f : 0.0f;

			case EFoliageDistributionCondition::Health:
			return Health;

			default:
			return 0.0f;
		}
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("Light = %f, Scale = %f, UpAlignment = %f, Health = %f, Tip = %s"), Light, Scale, UpAlignment, Health, Tip ? TEXT("True") : TEXT("False"));
	}
};

USTRUCT()
struct FPVFoliageInfo
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Info")
	TSoftObjectPtr<UObject> Mesh;

	UPROPERTY(EditAnywhere, Category="Info", meta=(ShowInnerProperties, FullyExpand="true", Tooltip=""))
	FPVFoliageAttributes Attributes;

	bool IsValid() const
	{
		// Asset is loaded and pointer is valid
		// Path is valid, but asset is NOT loaded yet
		return Mesh.IsValid() || Mesh.ToSoftObjectPath().IsValid(); 
	}
};

struct FPVFoliage
{
	static void DistributeFoliage(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
	                              const FDistributionSettings& DistributionSettings, const FScaleSettings& ScaleSettings,
	                              const FVectorSettings& VectorSettings, const FPhyllotaxySettings& PhyllotaxySettings,
	                              const FMiscSettings& MiscSettings, const FPVFoliageDistributionConditions& DistributionConditions);
};
