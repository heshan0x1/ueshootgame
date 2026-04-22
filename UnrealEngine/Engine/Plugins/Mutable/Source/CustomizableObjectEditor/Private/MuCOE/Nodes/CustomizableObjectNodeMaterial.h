// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeMaterialBase.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByName.h"

#include "CustomizableObjectNodeMaterial.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

enum class EMaterialParameterType : uint8;
namespace ENodeTitleType { enum Type : int; }

class FArchive;
class SGraphNode;
class SWidget;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UMaterial;
class UMaterialInterface;
class UObject;
class UTexture2D;
struct FCustomizableObjectNodeMaterialImage;
struct FCustomizableObjectNodeMaterialScalar;
struct FCustomizableObjectNodeMaterialVector;
struct FEdGraphPinReference;
struct FFrame;
struct FPropertyChangedEvent;


/** Custom remap pins by name action.
 *
 * Remap pins by Texture Parameter Id. */
UCLASS()
class UCustomizableObjectNodeMaterialRemapPinsByName : public UCustomizableObjectNodeRemapPinsByName
{
	GENERATED_BODY()
public:
	virtual bool Equal(const UCustomizableObjectNode& Node, const UEdGraphPin& OldPin, const UEdGraphPin& NewPin) const override;

	virtual void RemapPins(const UCustomizableObjectNode& Node, const TArray<UEdGraphPin*>& OldPins, const TArray<UEdGraphPin*>& NewPins, TMap<UEdGraphPin*, UEdGraphPin*>& PinsToRemap, TArray<UEdGraphPin*>& PinsToOrphan) override;

	bool HasSavedPinData(const UCustomizableObjectNode& Node, const UEdGraphPin &Pin) const;
};


/** Base class for all Material Parameters. */
UCLASS(MinimalAPI)
class UCustomizableObjectNodeMaterialPinDataParameter : public UCustomizableObjectNodePinData
{
	GENERATED_BODY()

public:

	/** Parameter id + layer index */
	UPROPERTY()
	FNodeMaterialParameterId MaterialParameterId;

	/** Texture Parameter Id. */
	UPROPERTY()
	FGuid ParameterId_DEPRECATED;

	/** Returns true if all properties are in its default state. */
	UE_API virtual bool IsDefault() const;
};


/** Node pin mode. All pins set to EPinMode::Default will use this this mode. */
UENUM()
enum class ENodePinMode
{
	Mutable UMETA(ToolTip = "All Material Texture FParameters go through Mutable."),
	Passthrough UMETA(ToolTip = "All Material Texture FParameters are not modified by Mutable.")
};


/** Image pin, pin mode. */
UENUM()
enum class EPinMode
{
	Default UMETA(DisplayName = "Node Defined", ToolTip = "Use node's \"Default Texture Parameter Mode\"."),
	Mutable UMETA(ToolTip = "The Material Texture FParameters goes through Mutable."),
	Passthrough UMETA(ToolTip = "The Material Texture FParameters is not modified by Mutable.")
};


/** Image Pin, UV Layout Mode. */
UENUM()
enum class EUVLayoutMode
{
	/** Does not override the UV Index specified in the Material. */
	FromMaterial,
	/* Texture should not be transformed by any layout. These textures will not be reduced automatically for LODs. */
	Ignore,
	/** User specified UV Index. */
	Index
};


/** Enum to FText. */
FText EPinModeToText(EPinMode PinMode);

UCLASS(MinimalAPI)
class UCustomizableObjectNodeMaterial : public UCustomizableObjectNodeMaterialBase
{
public:
	GENERATED_BODY()
	
	// UObject interface.
	UE_API virtual void Serialize(FArchive& Ar) override;
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FText GetTooltipText() const override;
	UE_API virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	UE_API virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	UE_API virtual void PostPasteNode() override;
	UE_API virtual bool CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const override;

	// UCustomizableObjectNode interface
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual bool CanPinBeHidden(const UEdGraphPin& Pin) const override;
	UE_API virtual bool HasPinViewer() const override;
	UE_API virtual UCustomizableObjectNodeRemapPinsByName* CreateRemapPinsDefault() const override;
	virtual bool ProvidesCustomPinRelevancyTest() const override { return true; }
	UE_API virtual bool IsPinRelevant(const UEdGraphPin* Pin) const override;
	UE_API virtual bool CustomRemovePin(UEdGraphPin& Pin) override;
	UE_API virtual bool IsNodeOutDatedAndNeedsRefresh() override;
	UE_API virtual FString GetRefreshMessage() const override;
	UE_API virtual TSharedPtr<IDetailsView> CustomizePinDetails(const UEdGraphPin& Pin) const override;
	UE_API virtual TArray<FString> GetEnableTags(TArray<const UCustomizableObjectNodeMacroInstance*>* MacroContext = nullptr) override;
	UE_API virtual TArray<FString>* GetEnableTagsArray() override;
	UE_API virtual FString GetInternalTagDisplayName() override;
	
	// UCustomizableObjectNodeMaterialBase interface
	UE_API virtual TArray<UCustomizableObjectLayout*> GetLayouts() const override;
	UE_API virtual UEdGraphPin* OutputPin() const override;
	UE_API virtual UMaterialInterface* GetMaterial() const override;
	UE_API virtual int32 GetNumParameters(EMaterialParameterType Type) const override;
	UE_API virtual FNodeMaterialParameterId GetParameterId(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual FName GetParameterName(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual int32 GetParameterLayerIndex(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual FText GetParameterLayerName(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual bool HasParameter(const FNodeMaterialParameterId& ParameterId) const override;
	UE_API virtual UEdGraphPin* GetParameterPin(EMaterialParameterType Type, int32 ParameterIndex) const override;
	UE_API virtual UEdGraphPin* GetParameterPin(const FNodeMaterialParameterId& ParameterId) const override;
	UE_API virtual bool IsImageMutableMode(int32 ImageIndex) const override;
	UE_API virtual bool IsImageMutableMode(const UEdGraphPin& Pin) const override;
	UE_API virtual UTexture2D* GetImageReferenceTexture(int32 ImageIndex) const override;
	UE_API virtual UTexture2D* GetImageValue(int32 ImageIndex) const override;
	UE_API virtual int32 GetImageUVLayout(int32 ImageIndex) const override;
	UE_API virtual UEdGraphPin* GetMeshPin() const override;
	UE_API virtual UEdGraphPin* GetMaterialAssetPin() const override;
	UE_API virtual UEdGraphPin* GetEnableTagsPin() const override;
	UE_API virtual const UCustomizableObjectNodeMaterial* GetMaterialNode() const override;
	UE_API virtual bool RealMaterialDataHasChanged() const override;
	UE_API virtual FPostImagePinModeChangedDelegate* GetPostImagePinModeChangedDelegate() override;
	
	// Own Interface

	UE_API void SetMaterial(UMaterialInterface* InMaterial);

	static UE_API bool HasParameter(const UMaterialInterface* InMaterial, const FNodeMaterialParameterId& ParameterId);
	static UE_API int32 GetParameterLayerIndex(const UMaterialInterface* InMaterial, EMaterialParameterType Type, int32 ParameterIndex);

	UPROPERTY(EditAnywhere, Category = Tags)
	TArray<FString> Tags;

private:
	/** Set the Pin Mode of a Texture Parameter Pin. */
	UE_API void SetImagePinMode(UEdGraphPin& Pin, EPinMode PinMode) const;
	
	/** Delegate called when a Texture Parameter Pin Mode changes. */
	FPostImagePinModeChangedDelegate PostImagePinModeChangedDelegate;

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, Category=CustomizableObject, DisplayName = "Default Texture Parameter Mode", Meta = (ToolTip = "All Mateiral Texture FParameters set to \"Node Defined\" will use this mode."))
	ENodePinMode TextureParametersMode = ENodePinMode::Passthrough;

	UPROPERTY()
	int32 MeshComponentIndex_DEPRECATED = 0;

public:
	/** Selects which Mesh component of the Instance this material belongs to */
	UPROPERTY()
	FName MeshComponentName_DEPRECATED;

private:

	/** Last static or skeletal mesh connected. Used to remove the callback once disconnected. */
	TWeakObjectPtr<UCustomizableObjectNode> LastMeshNodeConnected;

	/** List of material parameter types that are actually relevant to mutable. */
	static UE_API const TArray<EMaterialParameterType> ParameterTypes;

	/** Relates a Parameter id (key) (and layer if is a layered material) to a Pin (value). Only used to improve performance.
	  * If a deprecated pin and a non-deprecated pin have the same Parameter id, this the non-deprecated one prevails. */
	UPROPERTY()
	TMap<FNodeMaterialParameterId, FEdGraphPinReference> PinsParameterMap;

	UPROPERTY()
	FEdGraphPinReference EnableTagsPinRef;
	
	/** Create the pin data of the given parameter type. */
	UE_API UCustomizableObjectNodeMaterialPinDataParameter* CreatePinData(EMaterialParameterType Type, int32 ParameterIndex);

	/** Allocate a pin for each parameter of the given type. */
	UE_API void AllocateDefaultParameterPins(EMaterialParameterType Type);

	/** Set the default Material from the connected static or skeletal mesh. */
	UE_API void SetDefaultMaterial();

	/** Connected NodeStaticMesh or NodeSkeletalMesh Mesh UPROPERTY changed callback function. Sets the default material. */
	UFUNCTION()
	UE_API void MeshPostEditChangeProperty(FPostEditChangePropertyDelegateParameters& Parameters);

	/** Format pin name. */
	UE_API FName GetPinName(EMaterialParameterType Type, int32 ParameterIndex) const;

	/** Returns the texture coordinate of the given Material Expression. Returns -1 if not found. */
	static UE_API int32 GetExpressionTextureCoordinate(UMaterial* Material, const FGuid &ImageId);

	/** Return the Pin Category given the node NodePinMode. */
	static UE_API FName NodePinModeToImagePinMode(ENodePinMode NodePinMode);

	/** Return the Pin Category given a PinMode. */
	UE_API FName GetImagePinMode(EPinMode PinMode) const;

	/** Return the Pin Category given a Pin. */
	UE_API FName GetImagePinMode(const UEdGraphPin& Pin) const;

	/** Get the UV Layout Index defined in the Material. */
	UE_API int32 GetImageUVLayoutFromMaterial(int32 ImageIndex) const;
	
	// Deprecated properties
	/** Set all pins to Mutable mode. Even so, each pin can override its behaviour. */
	UPROPERTY()
	bool bDefaultPinModeMutable_DEPRECATED = false;
	
	UPROPERTY()
	TArray<FCustomizableObjectNodeMaterialImage> Images_DEPRECATED;
	
	UPROPERTY()
	TArray<FCustomizableObjectNodeMaterialVector> VectorParams_DEPRECATED;

	UPROPERTY()
	TArray<FCustomizableObjectNodeMaterialScalar> ScalarParams_DEPRECATED;

	UPROPERTY()
	TMap<FGuid, FEdGraphPinReference> PinsParameter_DEPRECATED;
};


/** Additional data for a Material Texture Parameter pin. */
UCLASS(MinimalAPI)
class UCustomizableObjectNodeMaterialPinDataImage : public UCustomizableObjectNodeMaterialPinDataParameter
{
	GENERATED_BODY()

	friend void UCustomizableObjectNodeMaterial::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion);
	
public:
	// UObject interface
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual bool CanEditChange(const FProperty* InProperty) const override;

	// UCustomizableObjectNodePinData interface
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;

	// NodePinDataParameter interface
	/** Virtual function used to copy pin data when remapping pins. */
	UE_API virtual void Copy(const UCustomizableObjectNodePinData& Other) override;
	
	// UCustomizableObjectNodeMaterialPinParameter interface
	UE_API virtual bool IsDefault() const override;

	// Own interface
	/** Constructor parameters. Should always be called after a NewObject. */
	UE_API void Init(UCustomizableObjectNodeMaterial& InNodeMaterial);

	UE_API EPinMode GetPinMode() const;

	UE_API void SetPinMode(EPinMode InPinMode);
	
private:
	/** Image pin mode. If is not default, overrides the defined node behaviour. */
	UPROPERTY(EditAnywhere, Category = NoCategory, DisplayName = "Texture Parameter Mode")
	EPinMode PinMode = EPinMode::Default;

public:
	constexpr static int32 UV_LAYOUT_IGNORE = -1;

	UPROPERTY(EditAnywhere, Category = NoCategory)
	EUVLayoutMode UVLayoutMode = EUVLayoutMode::FromMaterial;
	
	/** Index of the UV channel that will be used with this image. It is necessary to apply the proper layout transformations to it. */
	UPROPERTY(EditAnywhere, Category = NoCategory, meta = (EditCondition = "UVLayoutMode == EUVLayoutMode::Index", EditConditionHides))
	int32 UVLayout = -2;

	/** Reference Texture used to decide the texture properties of the mutable-generated textures
	* connected to this material. If null, it will try to be guessed at compile time from
	* the graph. */
	UPROPERTY(EditAnywhere, Category = NoCategory) // Required to be EditAnywhere for the selector to work.
	TObjectPtr<UTexture2D> ReferenceTexture = nullptr;

private:
	UPROPERTY()
	TObjectPtr<UCustomizableObjectNodeMaterial> NodeMaterial = nullptr;
};


/** Additional data for a Material Vector Parameter pin. */
UCLASS(MinimalAPI)
class UCustomizableObjectNodeMaterialPinDataVector : public UCustomizableObjectNodeMaterialPinDataParameter
{
	GENERATED_BODY()
};


/** Additional data for a Material Float Parameter pin. */
UCLASS(MinimalAPI)
class UCustomizableObjectNodeMaterialPinDataScalar : public UCustomizableObjectNodeMaterialPinDataParameter
{
	GENERATED_BODY()
};

#undef UE_API
