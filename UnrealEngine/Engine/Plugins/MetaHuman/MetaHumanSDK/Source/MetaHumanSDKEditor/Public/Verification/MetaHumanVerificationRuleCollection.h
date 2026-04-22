// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"

#include "MetaHumanVerificationRuleCollection.generated.h"

#define UE_API METAHUMANSDKEDITOR_API

class UMetaHumanAssetReport;

/**
 * Options for the Verification process
 */
USTRUCT(BlueprintType)
struct FMetaHumanVerificationOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = " MetaHuman SDK | Verification ")
	bool bVerbose = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = " MetaHuman SDK | Verification ")
	bool bTreatWarningsAsErrors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = " MetaHuman SDK | Verification ")
	bool bVerifyPackagingRules = true;
};

using FMetaHumansVerificationOptions UE_DEPRECATED(5.7, "Use FMetaHumanVerificationOptions instead.") = FMetaHumanVerificationOptions;

/**
 * A Rule which can be part of a MetaHuman verification test suite
 */
UCLASS(MinimalAPI, Abstract, Blueprintable)
class UMetaHumanVerificationRuleBase : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Applies the rule to the asset and updates the verification report
	 *
	 * @param ToVerify The root UObject of the asset that is being verified
	 * @param Report The report which should be updated with the results of the test
	 * @param Options Verification option flags to use when generating the report
	 */
	UFUNCTION(BlueprintNativeEvent, Category = " MetaHuman SDK | Verification ")
	UE_API void Verify(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options);

	virtual void Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const PURE_VIRTUAL(UMetaHumanVerificationRuleBase::Verify_Implementation,);
};

/**
 * A collection of Rules which make up a verification test for a class of MetaHuman asset compatibility, for example
 * groom compatibility, clothing compatibility, animation compatibility etc.
 */
UCLASS(MinimalAPI, BlueprintType)
class UMetaHumanVerificationRuleCollection : public UObject
{
	GENERATED_BODY()

public:
	/** Adds a rule to this collection */
	UFUNCTION(BlueprintCallable, Category = " MetaHuman SDK | Verification ")
	UE_API void AddVerificationRule(UMetaHumanVerificationRuleBase* Rule);

	/**
	 * Runs all registered rules against the Target. Compiles the results in OutReport.
	 *
	 * @param Target The root UObject of the asset that is being verified
	 * @param Report The report which should be updated with the results of the tests
	 * @param Options The options struct which may contain relevant options for the verification rule
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = " MetaHuman SDK | Verification ")
	UE_API UMetaHumanAssetReport* ApplyAllRules(const UObject* Target, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const;

private:
	UPROPERTY()
	TArray<TObjectPtr<UMetaHumanVerificationRuleBase>> Rules;
};

#undef UE_API
