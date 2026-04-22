// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// #include "SubobjectDataHandle.h"
#include "DataStorage/Handles.h"
#include "StructUtils/InstancedStruct.h"

#include "SubobjectDataAdapterBase.generated.h"

class AActor;
class UActorComponent;
class UBlueprint;
class UObject;
struct FSubobjectData;
struct FSubobjectDataHandle;

struct FTedsRowHandle;

#define UE_API SUBOBJECTDATAINTERFACE_API

struct
FSubobjectDataAdapterHandle
{
	friend class USubobjectDataSubsystem;
private:
	uint64 Opaque = 0;
};

struct
FSubobjectDataSubsystemAdapterHandle
{
	friend class USubobjectDataSubsystem;
private:
	uint64 Opaque = 0;
};

USTRUCT()
struct
FSubobjectDataSubsystemContextDataBase
{
	GENERATED_BODY()
};

// An implementation of the ContextData that supports containing a single UObject
// This is to provide interoperability with the existing ContextObject used in APIs of the SubobjectDataSubsystem
USTRUCT()
struct
FSubobjectDataSubsystemContextData_SingleUObjectContextObject : public FSubobjectDataSubsystemContextDataBase
{
	GENERATED_BODY()

	UE_API FSubobjectDataSubsystemContextData_SingleUObjectContextObject();
	UE_API explicit FSubobjectDataSubsystemContextData_SingleUObjectContextObject(UObject* ContextObject);

	UPROPERTY()
	TWeakObjectPtr<UObject> Object;
};

USTRUCT()
struct
FSubobjectDataSubsystemContextData_TedsRow : public FSubobjectDataSubsystemContextDataBase
{
	GENERATED_BODY()

	UE_API FSubobjectDataSubsystemContextData_TedsRow();
	UE_API explicit FSubobjectDataSubsystemContextData_TedsRow(UE::Editor::DataStorage::RowHandle InRowHandle);
	UE_API explicit FSubobjectDataSubsystemContextData_TedsRow(const FTedsRowHandle& InRowHandle);

	UPROPERTY()
	FTedsRowHandle RowHandle;
};

class FSubobjectDataSubsystemAdapterBase
{
public:
	UE_API virtual ~FSubobjectDataSubsystemAdapterBase();
	UE_API virtual void GatherSubobjectData(const TInstancedStruct<FSubobjectDataSubsystemContextDataBase>& ContextData, TArray<FSubobjectDataHandle>& OutArray, TFunctionRef<FSubobjectDataHandle()> CreateSubobjectData) const;
	UE_API virtual int32 DeleteSubobjects(const FSubobjectDataHandle& ContextHandle, const TArray<FSubobjectDataHandle>& SubobjectsToDelete) const;
};


class FSubobjectDataAdapterBase
{
public:
	UE_API virtual ~FSubobjectDataAdapterBase();

	UE_API virtual bool CanEdit(const FSubobjectData& Data) const;
	UE_API virtual bool CanDelete(const FSubobjectData& Data) const;
	UE_API virtual bool CanDuplicate(const FSubobjectData& Data) const;
	UE_API virtual bool CanCopy(const FSubobjectData& Data) const;
	UE_API virtual bool CanReparent(const FSubobjectData& Data) const;
	UE_API virtual bool CanRename(const FSubobjectData& Data) const;

	UE_API virtual const UObject* GetObject(const FSubobjectData& Data, bool bEventIfPendingKill = false) const;
	UE_API virtual const UObject* GetObjectForBlueprint(const FSubobjectData& Data, UBlueprint* Blueprint) const;
	UE_API virtual const UActorComponent* GetComponentTemplate(const FSubobjectData& Data, bool bEvenIfPendingKill = false) const;
	UE_API virtual const UActorComponent* FindComponentInstanceInActor(const FSubobjectData& Data, const AActor* InActor) const;
	UE_API virtual UActorComponent* FindMutableComponentInstanceInActor(const FSubobjectData& Data, const AActor* InActor) const;

	UE_API virtual UBlueprint* GetBlueprint(const FSubobjectData& Data) const;
	UE_API virtual UBlueprint* GetBlueprintBeingEdited(const FSubobjectData& Data) const;

	UE_API virtual bool IsInstancedComponent(const FSubobjectData& Data) const;
	UE_API virtual bool IsInstancedActor(const FSubobjectData& Data) const;
	UE_API virtual bool IsNativeComponent(const FSubobjectData& Data) const;
	UE_API virtual bool IsBlueprintInheritedComponent(const FSubobjectData& Data) const;
	UE_API virtual bool IsInheritedComponent(const FSubobjectData& Data) const;
	UE_API virtual bool IsSceneComponent(const FSubobjectData& Data) const;
	UE_API virtual bool IsRootComponent(const FSubobjectData& Data) const;
	UE_API virtual bool IsDefaultSceneRoot(const FSubobjectData& Data) const;
	UE_API virtual bool SceneRootHasDefaultName(const FSubobjectData& Data) const;
	UE_API virtual bool IsComponent(const FSubobjectData& Data) const;
	UE_API virtual bool IsChildActor(const FSubobjectData& Data) const;
	UE_API virtual bool IsChildActorSubtreeObject(const FSubobjectData& Data) const;
	UE_API virtual bool IsRootActor(const FSubobjectData& Data) const;
	UE_API virtual bool IsActor(const FSubobjectData& Data) const;
	UE_API virtual bool IsInstancedInheritedComponent(const FSubobjectData& Data) const;
	UE_API virtual bool IsAttachedTo(const FSubobjectData& Data, const FSubobjectDataHandle& InHandle) const;
	UE_API virtual FString GetDisplayString(const FSubobjectData& Data, bool bShowNativeComponentNames = true) const;
	UE_API virtual FText GetDragDropDisplayText(const FSubobjectData& Data) const;

	UE_API virtual FText GetDisplayNameContextModifiers(const FSubobjectData& Data, bool bShowNativeComponentNames = true) const;
	
	UE_API virtual FText GetDisplayName(const FSubobjectData& Data) const;

	UE_API virtual FName GetVariableName(const FSubobjectData& Data) const;

	// Sockets for attaching in the viewport
	UE_API virtual FText GetSocketName(const FSubobjectData& Data) const;
	UE_API virtual FName GetSocketFName(const FSubobjectData& Data) const;
	UE_API virtual bool HasValidSocket(const FSubobjectData& Data) const;
	UE_API virtual void SetSocketName(FSubobjectData& Data, FName InNewName) const;
	UE_API virtual void SetupAttachment(FSubobjectData& Data, FName SocketName, const FSubobjectDataHandle& AttachParentHandle) const;
	
	UE_API virtual FSubobjectDataHandle FindChildByObject(const FSubobjectData& Data, UObject* ContextObject) const;
	UE_API virtual FText GetAssetName(const FSubobjectData& Data) const;
	UE_API virtual FText GetAssetPath(const FSubobjectData& Data) const;
	UE_API virtual bool IsAssetVisible(const FSubobjectData& Data) const;
	UE_API virtual FText GetMobilityToolTipText(const FSubobjectData& Data) const;
	UE_API virtual FText GetComponentEditorOnlyTooltipText(const FSubobjectData& Data) const;
	UE_API virtual FText GetIntroducedInToolTipText(const FSubobjectData& Data) const;

	UE_API virtual FText GetActorDisplayText(const FSubobjectData& Data) const;
};

#undef UE_API