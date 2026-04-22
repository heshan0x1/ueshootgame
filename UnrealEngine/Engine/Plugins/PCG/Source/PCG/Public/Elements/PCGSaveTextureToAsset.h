// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGAssetExporter.h"
#include "PCGContext.h"
#include "PCGSettings.h"

#include "PCGSaveTextureToAsset.generated.h"

class UPCGBaseTextureData;
class UTexture2D;

/** Save the input texture to a UTexture2D asset (format is always BGRA8). Outputs a soft object path attribute for the saved texture. Editor only. */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGSaveTextureToAssetSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SaveTextureToAsset")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGSaveTextureToAssetElement", "NodeTitle", "Save Texture to Asset"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGSaveTextureToAssetElement", "NodeTooltip", "Save the input texture to a UTexture2D asset (format is always BGRA8). Outputs a soft object path attribute for the saved texture. Note: This node is editor only."); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::InputOutput; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data, meta = (ShowOnlyInnerProperties, PCG_Overridable))
	FPCGAssetExporterParameters ExporterParams;
};

struct FPCGSaveTextureToAssetContext : public FPCGContext
{
public:
	virtual ~FPCGSaveTextureToAssetContext();

protected:
	virtual void AddExtraStructReferencedObjects(FReferenceCollector& Collector) override;

public:
	TObjectPtr<const UPCGBaseTextureData> InputTextureData = nullptr;
	TObjectPtr<UTexture2D> ExportedTexture = nullptr;
	uint8* RawReadbackData = nullptr;
	int32 ReadbackWidth = INDEX_NONE;
	int32 ReadbackHeight = INDEX_NONE;
	bool bUpdatedReadbackTextureResource = false;
	bool bReadbackDispatched = false;
	bool bReadbackComplete = false;
};

class FPCGSaveTextureToAssetElement : public IPCGElementWithCustomContext<FPCGSaveTextureToAssetContext>
{
public:
	/** Creating asset and initializing texture resource must occur on main thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;

	bool ReadbackInputTexture(FPCGSaveTextureToAssetContext* InContext) const;
};
