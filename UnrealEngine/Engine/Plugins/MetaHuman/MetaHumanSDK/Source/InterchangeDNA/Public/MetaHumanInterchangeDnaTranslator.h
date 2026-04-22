// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangeTranslatorBase.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MetaHumanInterchangeDnaTranslator.generated.h"

#define UE_API INTERCHANGEDNA_API

DECLARE_LOG_CATEGORY_EXTERN(InterchangeDNATranslator, Log, All);

class IDNAReader;

class FDnaPayloadContextBase
{
public:
	virtual ~FDnaPayloadContextBase() {}
	virtual FString GetPayloadType() const = 0;
#if WITH_ENGINE
	virtual bool FetchMeshPayload(TSharedPtr<IDNAReader> InDNAReader, const FTransform& MeshGlobalTransform, UE::Interchange::FMeshPayloadData& OutMeshPayloadData) { return false; };
#endif
};

class FDnaMeshPayloadContext : public FDnaPayloadContextBase
{
public:
	virtual ~FDnaMeshPayloadContext() {}
	virtual FString GetPayloadType() const override { return TEXT("Mesh-PayloadContext"); }
	
	/** Populates mesh description attributes with  static mesh data from the DNA reader for specified mesh */
	INTERCHANGEDNA_API static void PopulateStaticMeshDescription(FMeshDescription& OutMeshDescription, const IDNAReader& InDNAReader, const int32 InMeshIndex);	
	
#if WITH_ENGINE
	virtual bool FetchMeshPayload(TSharedPtr<IDNAReader> InDNAReader, const FTransform& MeshGlobalTransform, UE::Interchange::FMeshPayloadData& OutMeshPayloadData) override;
#endif
	bool bIsSkinnedMesh = false;
	int32 DnaLodIndex;
	int32 DnaMeshIndex;
private:
	bool FetchMeshPayloadInternal(TSharedPtr<IDNAReader> InDNAReader
		, const FTransform& MeshGlobalTransform
		, FMeshDescription& OutMeshDescription
		, TArray<FString>& OutJointNames) const;
};

class FDnaMorphTargetPayloadContext : public FDnaPayloadContextBase
{
public:
	virtual ~FDnaMorphTargetPayloadContext() {}
	virtual FString GetPayloadType() const override { return TEXT("MorphTarget-PayloadContext"); }
#if WITH_ENGINE
	virtual bool FetchMeshPayload(TSharedPtr<IDNAReader> InDNAReader, const FTransform& MeshGlobalTransform, UE::Interchange::FMeshPayloadData& OutMeshPayloadData) override;
	int32 DnaMeshIndex;
	int32 DnaMorphTargetIndex;
	int32 DnaChannelIndex;
#endif
private:
	bool FetchMeshPayloadInternal(TSharedPtr<IDNAReader> InDNAReader
		, const FTransform& MeshGlobalTransform
		, FMeshDescription& OutMorphTargetMeshDescription) const;
};

UCLASS(MinimalAPI, BlueprintType)
class UMetaHumanInterchangeDnaTranslator : public UInterchangeTranslatorBase
	, public IInterchangeMeshPayloadInterface
{
	GENERATED_BODY()
public:
	UE_API UMetaHumanInterchangeDnaTranslator();

	/** Begin UInterchangeTranslatorBase API*/
	UE_API virtual bool IsThreadSafe() const override;
	UE_API virtual EInterchangeTranslatorType GetTranslatorType() const override;
	UE_API virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const override;
	UE_API virtual TArray<FString> GetSupportedFormats() const override;
	UE_API virtual bool Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const override;
	UE_API virtual void ReleaseSource() override;
	UE_API virtual void ImportFinish() override;
	/** End UInterchangeTranslatorBase API*/	

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
	UE_API virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(const FInterchangeMeshPayLoadKey& PayLoadKey, const UE::Interchange::FAttributeStorage& PayloadAttributes) const override;

	///* IInterchangeMeshPayloadInterface End */

private:

	TSharedPtr<IDNAReader> DNAReader;
	mutable TMap<FString, TSharedPtr<FDnaPayloadContextBase>> PayloadContexts;

	UE_API FString GetJointHierarchyName(TSharedPtr<IDNAReader> InDNAReader, int32 JointIndex) const;
	UE_API FString AddDNAMissingJoints(UInterchangeBaseNodeContainer& NodeContainer, const FString& InLastNodeId, FTransform& OutCombinedTransform) const;
	UE_API bool FetchMeshPayloadData(const FString& PayloadKey, const FTransform& MeshGlobalTransform, UE::Interchange::FMeshPayloadData& OutMeshPayloadData) const;

	static UE_API const TArray<FString> DNAMissingJoints;
};

#undef UE_API
