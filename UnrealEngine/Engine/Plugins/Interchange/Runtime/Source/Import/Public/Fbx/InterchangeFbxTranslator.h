// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/DeveloperSettings.h"
#include "Animation/InterchangeAnimationPayloadInterface.h"
#include "CoreMinimal.h"
#include "InterchangeTranslatorBase.h"
#include "InterchangeDispatcher.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Texture/InterchangeTexturePayloadInterface.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#if WITH_EDITOR
#include "InterchangeFbxParser.h"
#endif //WITH_EDITOR

#include "InterchangeFbxTranslator.generated.h"

/* Fbx translator class support import of texture, material, static mesh, skeletal mesh, */

UENUM(BlueprintType)
enum class EInterchangeCoordinateSystemPolicy : uint8
{
	MatchUpForwardAxes UMETA(DisplayName = "Match Up and Forward Axes", Tooltip = "The Up and Front axes in the FBX are mapped to the Up and Forward axes in UEFN.\nAfter import, the model will have the same apparent orientation in UEFN as itn oes in the FBX"),
	MatchUpAxis UMETA(DisplayName = "Match Up Axis", Tooltip = "The Up axis in the FBX is mapped to the Up axis in UEFN.\nAfter import, the model will have the same apparent vertical axis in UEFN as it does in the FBX, but its Forward and Left orientations may not match the FBX."),
	KeepXYZAxes UMETA(DisplayName = "Keep XYZ Axes", Tooltip = "The X, Y, and Z axes in the FBX are mapped directly to UEFN's internal X, Y, and Z axes, only flipping the Y axis to change from right - handed to left - handed coordinates.\nThis applies the least change to the data, but is least likely to match UEFN's Left, Up, and Forward axis conventions."),
};


UCLASS(BlueprintType, editinlinenew, MinimalAPI)
class UInterchangeFbxTranslatorSettings : public UInterchangeTranslatorSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Transient, Category = "Fbx Translator", meta = (EditCondition = "bUsingLUFCoordinateSystem", EditConditionHides, ToolTip = "Select strategy to map FBX coordinates system to UE coordinates system"))
	EInterchangeCoordinateSystemPolicy CoordinateSystemPolicy = EInterchangeCoordinateSystemPolicy::MatchUpForwardAxes;

	/** Whether to convert FBX scene axis system to Unreal axis system. */
	UPROPERTY(EditAnywhere, Category = "Fbx Translator", meta = (EditCondition = "!bUsingLUFCoordinateSystem", EditConditionHides))
	bool bConvertScene = true;

	/** Whether to force the front axis to be align with X instead of -Y default. */
	UPROPERTY(EditAnywhere, Category = "Fbx Translator", meta = (EditCondition = "bConvertScene && !bUsingLUFCoordinateSystem", EditConditionHides))
	bool bForceFrontXAxis = false;

	/** Whether to convert the scene from FBX unit to UE unit (centimeter). */
	UPROPERTY(EditAnywhere, Category = "Fbx Translator")
	bool bConvertSceneUnit = true;

	/** Whether to keep the name space from FBX name. */
	UPROPERTY(EditAnywhere, Category = "Fbx Translator")
	bool bKeepFbxNamespace = false;

	/** Whether to use the uFBX parser. */
	UPROPERTY(EditAnywhere, Category = "Fbx Translator", meta = (EditCondition = "bDisplayUfbxParser", EditConditionHides, DisplayName="Use ufbx translator (Experimental)", Tooltip = "If enabled, the ufbx SDK is used instead of the FBX SDK. This feature is experimental"))
	bool bUseUfbxParser = false;

	/* Use to enable or not the UEFN UI */
	UPROPERTY(Transient, VisibleDefaultsOnly, Category = InternalOnly, meta = (EditCondition = "false", EditConditionHides))
	bool bUsingLUFCoordinateSystem;

	/* Use to enable or not the UEFN UI */
	UPROPERTY(Transient, VisibleDefaultsOnly, Category = InternalOnly, meta = (EditCondition = "false", EditConditionHides))
	bool bDisplayUfbxParser = true;

	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

UCLASS(MinimalAPI, BlueprintType)
class UInterchangeFbxTranslator : public UInterchangeTranslatorBase
, public IInterchangeTexturePayloadInterface
, public IInterchangeMeshPayloadInterface
, public IInterchangeAnimationPayloadInterface
{
	GENERATED_BODY()
public:
	INTERCHANGEIMPORT_API UInterchangeFbxTranslator();

	static INTERCHANGEIMPORT_API void CleanUpTemporaryFolder();

	/** Begin UInterchangeTranslatorBase API*/
	INTERCHANGEIMPORT_API virtual bool IsThreadSafe() const override;
	INTERCHANGEIMPORT_API virtual EInterchangeTranslatorType GetTranslatorType() const override;
	INTERCHANGEIMPORT_API virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const override;
	INTERCHANGEIMPORT_API virtual TArray<FString> GetSupportedFormats() const override;
	INTERCHANGEIMPORT_API virtual bool Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const override;
	INTERCHANGEIMPORT_API virtual void ReleaseSource() override;
	INTERCHANGEIMPORT_API virtual void ImportFinish() override;
	INTERCHANGEIMPORT_API virtual UInterchangeTranslatorSettings* GetSettings() const override;
	INTERCHANGEIMPORT_API virtual void SetSettings(const UInterchangeTranslatorSettings* InterchangeTranslatorSettings) override;
	/** End UInterchangeTranslatorBase API*/


	//////////////////////////////////////////////////////////////////////////
	/* IInterchangeTexturePayloadInterface Begin */

	/**
	 * Once the translation is done, the import process need a way to retrieve payload data.
	 * This payload will be use by the factories to create the asset.
	 *
	 * @param InSourceData - The source data containing the data to translate
	 * @param PayloadKey - The key to retrieve the a particular payload contain into the specified source data.
	 * @return a PayloadData containing the imported data. The TOptional will not be set if there is an error.
	 */
	INTERCHANGEIMPORT_API virtual TOptional<UE::Interchange::FImportImage> GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& AlternateTexturePath) const override;

	/* IInterchangeTexturePayloadInterface End */


	//////////////////////////////////////////////////////////////////////////
	/* IInterchangeMeshPayloadInterface Begin */

	/**
	 * Once the translation is done, the import process need a way to retrieve payload data.
	 * This payload will be use by the factories to create the asset.
	 *
	 * @param SourceData - The source data containing the data to translate
	 * @param PayloadKey - The key to retrieve the a particular payload contain into the specified source data.
	 * @return a PayloadData containing the imported data. The TOptional will not be set if there is an error.
	 */
	UE_DEPRECATED(5.6, "Deprecated. Use GetMeshPayloadData(const FInterchangeMeshPayLoadKey&) instead.")
	virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(const FInterchangeMeshPayLoadKey& PayLoadKey, const FTransform& MeshGlobalTransform) const override
	{
		using namespace UE::Interchange;
		FAttributeStorage Attributes;
		Attributes.RegisterAttribute(UE::Interchange::FAttributeKey{ MeshPayload::Attributes::MeshGlobalTransform }, MeshGlobalTransform);
		return GetMeshPayloadData(PayLoadKey, Attributes);
	}
	INTERCHANGEIMPORT_API virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(const FInterchangeMeshPayLoadKey& PayLoadKey, const UE::Interchange::FAttributeStorage& PayloadAttributes) const override;
	///* IInterchangeMeshPayloadInterface End */

	//////////////////////////////////////////////////////////////////////////
	/* IInterchangeAnimationPayloadInterface Begin */
	
	virtual bool PreferGroupingBoneAnimationQueriesTogether() const override
	{
		return true;
	}

	INTERCHANGEIMPORT_API virtual TArray<UE::Interchange::FAnimationPayloadData> GetAnimationPayloadData(const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries) const override;
	
	/* IInterchangeAnimationPayloadInterface End */
private:
	INTERCHANGEIMPORT_API FString CreateLoadFbxFileCommand(const FString& FbxFilePath, const bool bConvertScene, const bool bForceFrontXAxis, const bool bConvertSceneUnit, const bool bKeepFbxNamespace) const;

	INTERCHANGEIMPORT_API FString CreateFetchMeshPayloadFbxCommand(const FString& FbxPayloadKey, const FTransform& MeshGlobalTransform) const;

	INTERCHANGEIMPORT_API FString CreateFetchPayloadFbxCommand(const FString& FbxPayloadKey) const;

	INTERCHANGEIMPORT_API FString CreateFetchAnimationBakeTransformPayloadFbxCommand(const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries) const;
	
	//Dispatcher is mutable since it is create during the Translate operation
	//We do not want to allocate the dispatcher and start the InterchangeWorker process
	//in the constructor because Archetype, CDO and registered translators will
	//never translate a source.
	mutable TUniquePtr<UE::Interchange::FInterchangeDispatcher> Dispatcher;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UInterchangeFbxTranslatorSettings> CacheFbxTranslatorSettings = nullptr;

	//If true this translator will use the dispatcher (InterchangeWorker program) to translate and return payloads.
	//If false, this translator will not use the dispatcher
	bool bUseWorkerImport = false;
#if WITH_EDITOR
	mutable UE::Interchange::FInterchangeFbxParser FbxParser;
#endif //WITH_EDITOR
	FString ResultFolder;
	mutable uint64 StartTime = 0;
	mutable FString FileName;
	// #interchange_debug
	mutable uint64 MeshPayloadTime = 0;
	mutable uint64 MeshMeshCount = 0;
	mutable uint64 MeshTriangleCount = 0;
	mutable uint64 MeshVertexCount = 0;
};

