// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubobjectDataAdapterBase.h"

#include "SubobjectDataHandle.h"

#define LOCTEXT_NAMESPACE "FSubobjectDataBehaviorBase"

FSubobjectDataSubsystemContextData_SingleUObjectContextObject::FSubobjectDataSubsystemContextData_SingleUObjectContextObject()
{
}

FSubobjectDataSubsystemContextData_SingleUObjectContextObject::FSubobjectDataSubsystemContextData_SingleUObjectContextObject(UObject* ContextObject)
	: Object(ContextObject)
{
}

FSubobjectDataSubsystemContextData_TedsRow::FSubobjectDataSubsystemContextData_TedsRow()
{
}

FSubobjectDataSubsystemContextData_TedsRow::FSubobjectDataSubsystemContextData_TedsRow(UE::Editor::DataStorage::RowHandle InRowHandle)
{
	RowHandle = InRowHandle;
}

FSubobjectDataSubsystemContextData_TedsRow::FSubobjectDataSubsystemContextData_TedsRow(const FTedsRowHandle& InRowHandle)
	: RowHandle(InRowHandle)
{
}

FSubobjectDataSubsystemAdapterBase::~FSubobjectDataSubsystemAdapterBase() = default;

void FSubobjectDataSubsystemAdapterBase::GatherSubobjectData(const TInstancedStruct<FSubobjectDataSubsystemContextDataBase>& ContextData, TArray<FSubobjectDataHandle>& OutArray, TFunctionRef<FSubobjectDataHandle()> CreateSubobjectData) const
{
}

int32 FSubobjectDataSubsystemAdapterBase::DeleteSubobjects(
	const FSubobjectDataHandle& ContextHandle,
	const TArray<FSubobjectDataHandle>& SubobjectsToDelete) const
{
	return 0;
}

FSubobjectDataAdapterBase::~FSubobjectDataAdapterBase() = default;

bool FSubobjectDataAdapterBase::CanEdit(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::CanDelete(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::CanDuplicate(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::CanCopy(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::CanReparent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::CanRename(const FSubobjectData& Data) const
{
	return false;
}

const UObject* FSubobjectDataAdapterBase::GetObject(const FSubobjectData& Data, bool bEventIfPendingKill) const
{
	return nullptr;
}

const UObject* FSubobjectDataAdapterBase::GetObjectForBlueprint(const FSubobjectData& Data, UBlueprint* Blueprint) const
{
	return nullptr;
}

const UActorComponent* FSubobjectDataAdapterBase::GetComponentTemplate(const FSubobjectData& Data, bool bEvenIfPendingKill) const
{
	return nullptr;
}

const UActorComponent* FSubobjectDataAdapterBase::FindComponentInstanceInActor(const FSubobjectData& Data, const AActor* InActor) const
{
	return nullptr;
}

UActorComponent* FSubobjectDataAdapterBase::FindMutableComponentInstanceInActor(const FSubobjectData& Data, const AActor* InActor) const
{
	return nullptr;
}

UBlueprint* FSubobjectDataAdapterBase::GetBlueprint(const FSubobjectData& Data) const
{
	return nullptr;
}

UBlueprint* FSubobjectDataAdapterBase::GetBlueprintBeingEdited(const FSubobjectData& Data) const
{
	return nullptr;
}

bool FSubobjectDataAdapterBase::IsInstancedComponent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsInstancedActor(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsNativeComponent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsBlueprintInheritedComponent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsInheritedComponent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsSceneComponent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsRootComponent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsDefaultSceneRoot(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::SceneRootHasDefaultName(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsComponent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsChildActor(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsChildActorSubtreeObject(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsRootActor(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsActor(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsInstancedInheritedComponent(const FSubobjectData& Data) const
{
	return false;
}

bool FSubobjectDataAdapterBase::IsAttachedTo(const FSubobjectData& Data, const FSubobjectDataHandle& InHandle) const
{
	return false;
}

FString FSubobjectDataAdapterBase::GetDisplayString(const FSubobjectData& Data, bool bShowNativeComponentNames) const
{
	return TEXT("");
}

FText FSubobjectDataAdapterBase::GetDragDropDisplayText(const FSubobjectData& Data) const
{
	return FText();
}

FText FSubobjectDataAdapterBase::GetDisplayNameContextModifiers(const FSubobjectData& Data,
	bool bShowNativeComponentNames) const
{
	return FText();
}

FText FSubobjectDataAdapterBase::GetDisplayName(const FSubobjectData& Data) const
{
	return FText();
}

FName FSubobjectDataAdapterBase::GetVariableName(const FSubobjectData& Data) const
{
	return FName();
}

FText FSubobjectDataAdapterBase::GetSocketName(const FSubobjectData& Data) const
{
	return FText();
}

FName FSubobjectDataAdapterBase::GetSocketFName(const FSubobjectData& Data) const
{
	return FName();
}

bool FSubobjectDataAdapterBase::HasValidSocket(const FSubobjectData& Data) const
{
	return false;
}

void FSubobjectDataAdapterBase::SetSocketName(FSubobjectData& Data, FName InNewName) const
{
}

void FSubobjectDataAdapterBase::SetupAttachment(FSubobjectData& Data, FName SocketName, const FSubobjectDataHandle& AttachParentHandle) const
{
}

FSubobjectDataHandle FSubobjectDataAdapterBase::FindChildByObject(const FSubobjectData& Data, UObject* ContextObject) const
{
	return FSubobjectDataHandle();
}

FText FSubobjectDataAdapterBase::GetAssetName(const FSubobjectData& Data) const
{
	return FText();
}

FText FSubobjectDataAdapterBase::GetAssetPath(const FSubobjectData& Data) const
{
	return FText();
}

bool FSubobjectDataAdapterBase::IsAssetVisible(const FSubobjectData& Data) const
{
	return false;
}

FText FSubobjectDataAdapterBase::GetMobilityToolTipText(const FSubobjectData& Data) const
{
	return FText();
}

FText FSubobjectDataAdapterBase::GetComponentEditorOnlyTooltipText(const FSubobjectData& Data) const
{
	return FText();
}

FText FSubobjectDataAdapterBase::GetIntroducedInToolTipText(const FSubobjectData& Data) const
{
	return FText();
}

FText FSubobjectDataAdapterBase::GetActorDisplayText(const FSubobjectData& Data) const
{
	return FText();
}

#undef LOCTEXT_NAMESPACE