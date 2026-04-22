// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/BulkData.h"

#include "MetaHumanConfig.generated.h"

#define UE_API METAHUMANCONFIG_API



class FMetaHumanConfig
{
public:

	/** Gets the config directory and user-friendly display name associated with some capture data */
	static UE_API bool GetInfo(class UCaptureData* InCaptureData, const FString& InComponent, FString& OutDisplayName);
	static UE_API bool GetInfo(class UCaptureData* InCaptureData, const FString& InComponent, UMetaHumanConfig*& OutConfig);
	static UE_API bool GetInfo(class UCaptureData* InCaptureData, const FString& InComponent, FString& OutDisplayName, UMetaHumanConfig*& OutConfig);
};



UENUM()
enum class EMetaHumanConfigType : uint8
{
	Unspecified,
	Solver,
	Fitting,
	PredictiveSolver,
};



/** MetaHuman Config Asset
*
*   Holds configuration info used by other MetaHuman components.
*
*/
UCLASS(MinimalAPI)
class UMetaHumanConfig : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Import")
	UE_API bool ReadFromDirectory(const FString& InPath);

	UE_API FString GetSolverTemplateData() const;
	UE_API FString GetSolverConfigData() const;
	UE_API FString GetSolverDefinitionsData() const;
	UE_API FString GetSolverHierarchicalDefinitionsData() const;
	UE_API FString GetSolverPCAFromDNAData() const;

	UE_API FString GetFittingTemplateData() const;
	UE_API FString GetFittingConfigData() const;
	UE_API FString GetFittingConfigTeethData() const;
	UE_API FString GetFittingIdentityModelData() const;
	UE_API FString GetFittingControlsData() const;

	UE_API TArray<uint8> GetPredictiveGlobalTeethTrainingData() const;
	UE_API TArray<uint8> GetPredictiveTrainingData() const;

	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Parameters")
	EMetaHumanConfigType Type = EMetaHumanConfigType::Unspecified;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	FString Name;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	FString Version;

	//~ Begin UObject Interface
	UE_API virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

private:

	UPROPERTY()
	int32 InternalVersion = 1; // To increase version dont adjust this default, but set InternalVersion in ReadFromDirectory

	FByteBulkData SolverTemplateDataCipherText;
	FByteBulkData SolverConfigDataCipherText;
	FByteBulkData SolverDefinitionsCipherText;
	FByteBulkData SolverHierarchicalDefinitionsCipherText;
	FByteBulkData SolverPCAFromDNACipherText;
	FByteBulkData FittingTemplateDataCipherText;
	FByteBulkData FittingConfigDataCipherText;
	FByteBulkData FittingConfigTeethDataCipherText;
	FByteBulkData FittingIdentityModelDataCipherText;
	FByteBulkData FittingControlsDataCipherText;
	FByteBulkData PredictiveGlobalTeethTrainingData;
	FByteBulkData PredictiveTrainingData;

	UE_API bool Encrypt(const FString& InPlainText, FByteBulkData& OutCipherText) const;
	UE_API bool Decrypt(const FByteBulkData& InCipherText, FString& OutPlainText) const;

	UE_API FString DecryptToString(const FByteBulkData& InCipherText) const;

	UE_API UMetaHumanConfig* GetBaseConfig() const;

	UE_API bool VerifyFittingConfig(const FString& InFittingTemplateDataJson, const FString& InFittingConfigDataJson, const FString& InFittingConfigTeethDataJson, 
		const FString& InFittingIdentityModelDataJson, const FString& InFittingControlsDataJson, FString& OutErrorString) const;

	UE_API bool VerifySolverConfig(const FString& InSolverTemplateDataJson, const FString& InSolverConfigDataJson, const FString& InSolverDefinitionsDataJson,
		const FString& InSolverHierarchicalDefinitionsDataJson, const FString& InSolverPCAFromDNADataJson, FString& OutErrorString) const;

};

#undef UE_API
