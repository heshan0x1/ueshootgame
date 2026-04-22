// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/InterchangeAnimationPayloadInterface.h"
#include "Audio/InterchangeAudioPayloadInterface.h"
#include "Groom/InterchangeGroomPayloadInterface.h"
#include "InterchangeTranslatorBase.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"
#include "Texture/InterchangeBlockedTexturePayloadInterface.h"
#include "Texture/InterchangeTexturePayloadInterface.h"
#include "Volume/InterchangeVolumePayloadInterface.h"

#include "USDStageOptions.h"

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeUsdTranslator.generated.h"

enum class EUsdInterpolationType : uint8;
namespace UE::InterchangeUsdTranslator::Private
{
	class UInterchangeUSDTranslatorImpl;
}

UCLASS(BlueprintType, editinlinenew, MinimalAPI)
class UInterchangeUsdTranslatorSettings : public UInterchangeTranslatorSettings
{
	GENERATED_BODY()

public:

	/** Only import geometry prims with these specific purposes from the USD file */
	UE_DEPRECATED(5.6, "This option is no longer a translator setting and has moved to the InterchangeUSDPipeline")
	UPROPERTY()
	int32 GeometryPurpose;

	/** Specifies which set of shaders to use when parsing USD materials, in addition to the universal render context. */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	FName RenderContext;

	/** Specifies which material purpose to use when parsing USD material bindings, in addition to the "allPurpose" fallback */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	FName MaterialPurpose;

	/** Describes how to interpolate between a timeSample value and the next */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	EUsdInterpolationType InterpolationType;

	/** Whether to use the specified StageOptions instead of the stage's own settings */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	bool bOverrideStageOptions;

	/** Custom StageOptions to use for the stage */
	UPROPERTY(EditAnywhere, Category = "USD Translator", meta = (EditCondition = bOverrideStageOptions))
	FUsdStageOptions StageOptions;

	/**
	 * Whether to convert USD prim attributes into translated node user attributes, ultimately ending up imported as asset and actor metadata.
	 *
	 * Note that AttributeRegexFilter may be needed in order to select only the desired metadata, as otherwise regular attributes like 
	 * 'points' and 'faceVertexCounts' will also get translated into Interchange metadata, which could be expensive.
	 */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	bool bTranslatePrimAttributes;

	/** Regex to match against USD prim attribute names to decide whether to convert them into translated node user attributes */
	UPROPERTY(EditAnywhere, Category = "USD Translator", meta = (EditCondition = bTranslatePrimAttributes))
	FString AttributeRegexFilter;

	/** Whether to convert USD prim metadata into translated node user attributes, if they pass the regex filter. See MetadataRegexFilter. */
	UPROPERTY(EditAnywhere, Category = "USD Translator")
	bool bTranslatePrimMetadata;

	/**
	 * Regex to match against the full USD prim metadata key to decide whether to convert them into translated node user attributes.
	 *
	 * Note that nested dictionaries are flattened using the ":" character as separator. So a myMetadata key inside the customData
	 * dictionary will end up with the full key 'customData:myMetadata', and could be selected by a regex filter 'customData:my',
	 * or just 'custom', or 'my', etc.
	 */
	UPROPERTY(EditAnywhere, Category = "USD Translator", meta = (EditCondition = bTranslatePrimMetadata))
	FString MetadataRegexFilter;

public:
	UInterchangeUsdTranslatorSettings();
};

UCLASS(BlueprintType)
class UInterchangeUSDTranslator
	: public UInterchangeTranslatorBase
	, public IInterchangeMeshPayloadInterface
	, public IInterchangeTexturePayloadInterface
	, public IInterchangeBlockedTexturePayloadInterface
	, public IInterchangeAnimationPayloadInterface
	, public IInterchangeVolumePayloadInterface
	, public IInterchangeGroomPayloadInterface
	, public IInterchangeAudioPayloadInterface
{
	GENERATED_BODY()

public:
	UInterchangeUSDTranslator();

	/** Begin UInterchangeTranslatorBase API*/
	virtual EInterchangeTranslatorType GetTranslatorType() const override;
	virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const override;
	virtual TArray<FString> GetSupportedFormats() const override;
	virtual bool Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const override;
	virtual void ImportFinish() override;
	virtual UInterchangeTranslatorSettings* GetSettings() const override;
	virtual void SetSettings(const UInterchangeTranslatorSettings* InterchangeTranslatorSettings) override;
	/** End UInterchangeTranslatorBase API*/

	TFuture<TOptional<UE::Interchange::FAnimationPayloadData>> ResolveAnimationPayloadQuery(
		const UE::Interchange::FAnimationPayloadQuery& PayloadQuery
	) const;

	/** Begin Interchange payload interfaces */
    UE_DEPRECATED(5.6, "Deprecated. Use GetMeshPayloadData(const FInterchangeMeshPayLoadKey&) instead.")
	virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(
		const FInterchangeMeshPayLoadKey& PayLoadKey,
		const FTransform& MeshGlobalTransform
	) const override
    {
		using namespace UE::Interchange;
		UE::Interchange::FAttributeStorage Attributes;
		Attributes.RegisterAttribute(UE::Interchange::FAttributeKey{ MeshPayload::Attributes::MeshGlobalTransform }, MeshGlobalTransform);
        return GetMeshPayloadData(PayLoadKey, Attributes);
    }

    virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(
        const FInterchangeMeshPayLoadKey& PayLoadKey,
		const UE::Interchange::FAttributeStorage& PayloadAttributes) const override;

	virtual TOptional<UE::Interchange::FImportImage> GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& AlternateTexturePath)
		const override;

	virtual TOptional<UE::Interchange::FImportBlockedImage> GetBlockedTexturePayloadData(
		const FString& PayloadKey,
		TOptional<FString>& AlternateTexturePath
	) const override;

	virtual TArray<UE::Interchange::FAnimationPayloadData> GetAnimationPayloadData(
		const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries
	) const override;

	virtual TOptional<UE::Interchange::FVolumePayloadData> GetVolumePayloadData(const UE::Interchange::FVolumePayloadKey& PayloadKey) const override;

	virtual TOptional<UE::Interchange::FGroomPayloadData> GetGroomPayloadData(const FInterchangeGroomPayloadKey& PayloadKey) const override;
	/** End Interchange payload interfaces */

private:
	mutable TUniquePtr<UE::InterchangeUsdTranslator::Private::UInterchangeUSDTranslatorImpl> Impl;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UInterchangeUsdTranslatorSettings> TranslatorSettings = nullptr;
};
