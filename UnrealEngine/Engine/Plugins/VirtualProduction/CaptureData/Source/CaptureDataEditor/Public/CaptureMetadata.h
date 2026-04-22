// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "CaptureMetadata.generated.h"

#define UE_API CAPTUREDATAEDITOR_API

USTRUCT(MinimalAPI, BlueprintType, Blueprintable)
struct FCaptureMetadataWindowOptions
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	bool bAllowEdit = true;
};

UCLASS(MinimalAPI, BlueprintType, Blueprintable)
class UCaptureMetadata
	: public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Metadata")
	static UE_API void SetCaptureMetadata(UObject* InObject, const UCaptureMetadata* InCaptureMetadata);

	UFUNCTION(BlueprintCallable, Category = "Metadata")
	static UE_API UCaptureMetadata* GetCaptureMetadata(const UObject* InObject);

	UFUNCTION(BlueprintCallable, Category = "Metadata")
	static UE_API void ClearCaptureMetadata(const UObject* InObject);

	UFUNCTION(BlueprintCallable, Category = "Metadata")
	static UE_API bool ShowCaptureMetadataObjects(const FText& InTitle, const TArray<UObject*>& InObjects, const FCaptureMetadataWindowOptions& InOptions);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn), Category = "Metadata")
	FString CameraId;

	bool IsEditable() const;
	FString GetOwnerName() const;

private:

	bool bIsEditable = true;
	FString OwnerName;

	virtual void PostInitProperties() override;
};

#undef UE_API