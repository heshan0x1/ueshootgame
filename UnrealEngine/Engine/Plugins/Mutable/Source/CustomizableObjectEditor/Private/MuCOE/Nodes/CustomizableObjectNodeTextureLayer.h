// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeTextureLayer.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class FArchive;
class UCustomizableObjectNodeRemapPins;
class UEdGraph;
class UObject;
struct FPropertyChangedEvent;


UENUM()
enum ECustomizableObjectTextureLayerEffect : int
{
	COTLE_MODULATE			= 0 UMETA(DisplayName = "MODULATE"), 
	COTLE_MULTIPLY				UMETA(DisplayName = "MULTIPLY"), 
	COTLE_SOFTLIGHT				UMETA(DisplayName = "SOFTLIGHT"),
	COTLE_HARDLIGHT				UMETA(DisplayName = "HARDLIGHT"),
	COTLE_DODGE					UMETA(DisplayName = "DODGE"),
	COTLE_BURN					UMETA(DisplayName = "BURN"),
	COTLE_SCREEN				UMETA(DisplayName = "SCREEN"),
	COTLE_OVERLAY				UMETA(DisplayName = "OVERLAY"),
	COTLE_ALPHA_OVERLAY			UMETA(DisplayName = "LIGHTEN"),
	COTLE_NORMAL_COMBINE		UMETA(DisplayName = "BLEND NORMALS")
};


USTRUCT()
struct FCustomizableObjectTextureLayer
{
	GENERATED_USTRUCT_BODY()

	FCustomizableObjectTextureLayer()
	{
		Effect = COTLE_SOFTLIGHT;
	}

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TEnumAsByte<enum ECustomizableObjectTextureLayerEffect> Effect;
};


UCLASS(MinimalAPI)
class UCustomizableObjectNodeTextureLayer : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UE_API UCustomizableObjectNodeTextureLayer();

	/**  */
	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TArray<FCustomizableObjectTextureLayer> Layers;

	// UObject interface.
	UE_API void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual void Serialize(FArchive& Ar) override;
	
	// Begin EdGraphNode interface
	UE_API FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API FLinearColor GetNodeTitleColor() const override;
	UE_API FText GetTooltipText() const override;
	UE_API void PrepareForCopying() override;
	UE_API void PostPasteNode() override;

	// UCustomizableObjectNode interface
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	UE_API virtual bool CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const override;
		
	UEdGraphPin* BasePin() const
	{
		return FindPin(TEXT("Base"));
	}

	UE_API UEdGraphPin* OutputPin() const;

	UEdGraphPin* LayerPin(int32 Index) const
	{
		FString PinName = FString::Printf(TEXT("Layer %d "), Index);
		return FindPin(PinName);
	}

	UEdGraphPin* MaskPin(int32 Index) const
	{
		FString PinName = FString::Printf(TEXT("Mask %d "), Index);
		return FindPin(PinName);
	}

	int32 GetNumLayers() const
	{
		int32 Count = 0;

		for (UEdGraphPin* Pin : GetAllNonOrphanPins())
		{
			if (Pin->GetName().StartsWith(TEXT("Layer ")))
			{
				Count++;
			}
		}

		return Count;
	}
protected:
	// This struct will store the information of each textue layer node when its destroyed, to be able to recover the UCustomizableObjectNodeTextureLayer::Layers
	// data in a post paste operation. Otherwise, this information is lost.
	struct TextureLayerLayersData
	{
		FString Name; // Texture layer node name
		TArray<FCustomizableObjectTextureLayer> Layers;
		UEdGraph* Graph; // Graph that contained this node
	};
	static UE_API int32 GetIndexInArrayDestroyedNodes(UCustomizableObjectNodeTextureLayer* Node);
	static UE_API TArray<TextureLayerLayersData> ArrayDestroyedNodes;

private:
	UPROPERTY()
	FEdGraphPinReference OutputPinReference;
};

#undef UE_API
