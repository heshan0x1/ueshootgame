// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshEditorUtils.h"

#include "ContextObjectStore.h"
#include "EditorModeManager.h"
#include "InteractiveToolsContext.h"
#include "ISkeletonEditorModule.h"
#include "SkeletalMeshEditingCache.h"
#include "SkeletalMeshModelingToolsEditorMode.h"
#include "SkeletonModifier.h"
#include "SReferenceSkeletonTree.h"
#include "Widgets/Docking/SDockTab.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkeletalMeshEditorUtils)

bool UE::SkeletalMeshEditorUtils::RegisterEditorContextObject(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		const USkeletalMeshEditorContextObject* Found = ToolsContext->ContextObjectStore->FindContext<USkeletalMeshEditorContextObject>();
		if (Found)
		{
			return true;
		}
		
		USkeletalMeshEditorContextObject* ContextObject = NewObject<USkeletalMeshEditorContextObject>(ToolsContext->ToolManager);
		if (ensure(ContextObject))
		{
			ContextObject->Register(ToolsContext->ToolManager);
			return true;
		}
	}
	return false;
}

bool UE::SkeletalMeshEditorUtils::UnregisterEditorContextObject(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		USkeletalMeshEditorContextObject* Found = ToolsContext->ContextObjectStore->FindContext<USkeletalMeshEditorContextObject>();
		if (Found != nullptr)
		{
			Found->Unregister(ToolsContext->ToolManager);
			ToolsContext->ContextObjectStore->RemoveContextObject(Found);
		}
		return true;
	}
	return false;
}

USkeletalMeshEditorContextObject* UE::SkeletalMeshEditorUtils::GetEditorContextObject(UInteractiveToolsContext* ToolsContext)
{
	return ToolsContext->ContextObjectStore->FindContext<USkeletalMeshEditorContextObject>();
}

UE::SkeletalMeshEditorUtils::FSkeletalMeshNotifierBindScope::FSkeletalMeshNotifierBindScope(
	TWeakPtr<ISkeletalMeshNotifier> InNotifierA,
	TWeakPtr<ISkeletalMeshNotifier> InNotifierB
	) : NotifierA(InNotifierA)
	, NotifierB(InNotifierB)
{

	DelegateToRemoveFromNotifierA = InNotifierA.Pin()->Delegate().AddLambda(
	[InNotifierB](const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
	{
		if (InNotifierB.IsValid())
		{
			InNotifierB.Pin()->HandleNotification(BoneNames, InNotifyType);
		}
	});
	
	DelegateToRemoveFromNotifierB = InNotifierB.Pin()->Delegate().AddLambda(
	[InNotifierA](const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
	{
		if (InNotifierA.IsValid())
		{
			InNotifierA.Pin()->HandleNotification(BoneNames, InNotifyType);
		}
	});
}

UE::SkeletalMeshEditorUtils::FSkeletalMeshNotifierBindScope::~FSkeletalMeshNotifierBindScope()
{
	if (DelegateToRemoveFromNotifierA.IsValid() && NotifierA.IsValid())
	{
		check(NotifierA.Pin()->Delegate().Remove(DelegateToRemoveFromNotifierA));
	}

	if (DelegateToRemoveFromNotifierB.IsValid() && NotifierB.IsValid())
	{
		check(NotifierB.Pin()->Delegate().Remove(DelegateToRemoveFromNotifierB));
	}
}



void USkeletalMeshEditorContextObject::Register(UInteractiveToolManager* InToolManager)
{
	if (ensure(!bRegistered) == false)
	{
		return;
	}

	InToolManager->GetContextObjectStore()->AddContextObject(this);
	bRegistered = true;
}

void USkeletalMeshEditorContextObject::Unregister(UInteractiveToolManager* InToolManager)
{
	ensure(bRegistered);
	
	InToolManager->GetContextObjectStore()->RemoveContextObject(this);

	EditorBindings.Reset();
	TreeBindings.Reset();
	
	bRegistered = false;
}

void USkeletalMeshEditorContextObject::Init(USkeletalMeshModelingToolsEditorMode* InEditorMode)
{
	EditorMode = InEditorMode;
	
	EditorBindings.Reset();
	TreeBindings.Reset();
}

EMeshLODIdentifier USkeletalMeshEditorContextObject::GetEditingLOD()
{
	if (!EditorMode.IsValid())
	{
		return EMeshLODIdentifier::Default;
	}
	return EditorMode->GetEditingLOD();
}

void USkeletalMeshEditorContextObject::HideSkeleton()
{
	if (!EditorMode.IsValid())
	{
		return;
	}
	EditorMode->HideSkeletonForTool();
}

void USkeletalMeshEditorContextObject::ShowSkeleton()
{
	if (!EditorMode.IsValid())
	{
		return;
	}
	EditorMode->ShowSkeletonForTool();
}

void USkeletalMeshEditorContextObject::ToggleBoneManipulation(bool bEnable)
{
	if (!EditorMode.IsValid())
	{
		return;
	}
	EditorMode->ToggleBoneManipulation(bEnable);
}

const TArray<FTransform>& USkeletalMeshEditorContextObject::GetComponentSpaceBoneTransforms(UToolTarget* InToolTarget)
{
	return EditorMode->GetCurrentEditingCache()->GetComponentSpaceBoneTransforms();
}


FName USkeletalMeshEditorContextObject::GetEditingMorphTarget()
{
	return EditorMode->GetEditingMorphTarget();
}

TMap<FName, float> USkeletalMeshEditorContextObject::GetMorphTargetWeights()
{
	return EditorMode->GetCurrentEditingCache()->GetMorphTargetWeights();
}

void USkeletalMeshEditorContextObject::NotifyMorphTargetEdited()
{
	return EditorMode->GetCurrentEditingCache()->HandleMorphTargetEdited( EditorMode->GetEditingMorphTarget() );
}

void USkeletalMeshEditorContextObject::BindTo(ISkeletalMeshEditingInterface* InEditingInterface)
{
	if (!InEditingInterface)
	{
		return;
	}
	
	BindEditor(InEditingInterface);
	BindRefSkeletonTree(InEditingInterface);
}

void USkeletalMeshEditorContextObject::UnbindFrom(ISkeletalMeshEditingInterface* InEditingInterface)
{
	if (!InEditingInterface)
	{
		return;
	}

	UnbindEditor(InEditingInterface);
	UnbindRefSkeletonTree(InEditingInterface);
}



USkeletalMeshEditorContextObject::FBindData USkeletalMeshEditorContextObject::BindInterfaceTo(
	ISkeletalMeshEditingInterface* InInterface,
	TSharedPtr<ISkeletalMeshNotifier> InOtherNotifier)
{
	FBindData BindData;
	BindData.BindScope.Reset(new UE::SkeletalMeshEditorUtils::FSkeletalMeshNotifierBindScope(InInterface->GetNotifier(), InOtherNotifier));
	
	return BindData;

}

void USkeletalMeshEditorContextObject::UnbindInterfaceFrom(
	FBindData& InOutBindData)
{
	InOutBindData.BindScope.Reset();
}

void USkeletalMeshEditorContextObject::BindEditor(ISkeletalMeshEditingInterface* InEditingInterface)
{
	if (!InEditingInterface || !EditorMode.IsValid())
	{
		return;
	}
	
	if (EditorBindings.Contains(InEditingInterface))
	{
		return;
	}

	TSharedPtr<ISkeletalMeshEditorBinding> Binding = EditorMode.Pin()->GetModeBinding();
	if (!Binding.IsValid())
	{
		return;
	}
	
	EditorBindings.Emplace(InEditingInterface, BindInterfaceTo(InEditingInterface, Binding->GetNotifier()));

	InEditingInterface->GetNotifier()->HandleNotification(Binding->GetSelectedBones(), ESkeletalMeshNotifyType::BonesSelected);
}

void USkeletalMeshEditorContextObject::UnbindEditor(ISkeletalMeshEditingInterface* InEditingInterface)
{
	if (FBindData* BindData = EditorBindings.Find(InEditingInterface))
	{
		BindData->BindScope.Reset();
	
		EditorBindings.Remove(InEditingInterface);
	}
}

void USkeletalMeshEditorContextObject::BindRefSkeletonTree(ISkeletalMeshEditingInterface* InEditingInterface)
{
	if (!InEditingInterface || !EditorMode.IsValid())
	{
		return;
	}
	
	const TWeakObjectPtr<USkeletonModifier> Modifier = InEditingInterface->GetModifier();
	if (!Modifier.IsValid())
	{
		return;
	}
	
	const TSharedPtr<FTabManager> TabManager = EditorMode->GetAssociatedTabManager();
	TSharedPtr<SDockTab> SkeletonTab = TabManager->FindExistingLiveTab(GetSkeletonTreeTabId());
	if (SkeletonTab.IsValid())
	{
		DefaultSkeletonWidget = SkeletonTab->GetContent();
	}
	else
	{
		SkeletonTab = TabManager->TryInvokeTab(GetSkeletonTreeTabId());
	}

	check(SkeletonTab.IsValid());

	SAssignNew(RefSkeletonWidget, SBorder)
		.Padding(4.f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
	[
		SAssignNew(RefSkeletonTree, SReferenceSkeletonTree)
			.Modifier(Modifier)
	];

	// switch between SSkeletonTree and SReferenceSkeletonTree
	SkeletonTab->SetContent(RefSkeletonWidget.ToSharedRef());

	TreeBindings.Emplace(InEditingInterface, BindInterfaceTo(InEditingInterface, RefSkeletonTree->GetNotifier()));

	// unbind notifications on the initial SSkeletonTree to avoid useless notifications
	if(const TSharedPtr<ISkeletalMeshEditorBinding> Binding = EditorMode->GetModeBinding())
	{
		const TArray<FName> SelectedBones = Binding->GetSelectedBones();
		
		if (FBindData* EditorBindData = EditorBindings.Find(InEditingInterface))
		{
			UnbindInterfaceFrom(*EditorBindData);
			EditorBindings.Remove(InEditingInterface);
			
			Binding->GetNotifier()->HandleNotification({}, ESkeletalMeshNotifyType::BonesSelected);
		}

		RefSkeletonTree->GetNotifier()->HandleNotification(SelectedBones, ESkeletalMeshNotifyType::BonesSelected);
	}
}

void USkeletalMeshEditorContextObject::UnbindRefSkeletonTree(ISkeletalMeshEditingInterface* InEditingInterface)
{
	if (FBindData* BindData = TreeBindings.Find(InEditingInterface))
	{
		TArray<FName> SelectedBones;
		if (RefSkeletonTree.IsValid())
		{
			RefSkeletonTree->GetSelectedBoneNames(SelectedBones);
			UnbindInterfaceFrom(*BindData);
		}

		if (EditorMode.IsValid())
		{
			const TSharedPtr<FTabManager> TabManager = EditorMode->GetAssociatedTabManager();
			if (TabManager.IsValid())
			{
				const TSharedPtr<SDockTab> SkeletonTab = TabManager->FindExistingLiveTab(GetSkeletonTreeTabId());
				if (SkeletonTab.IsValid())
				{
					if (SkeletonTab->GetContent() == RefSkeletonWidget)
					{
						SkeletonTab->SetContent(DefaultSkeletonWidget.IsValid() ? DefaultSkeletonWidget.ToSharedRef() : SNullWidget::NullWidget);
					}
				}	
			}

			// update the default skeleton widget with the new RefSkeleton and set current selection 
			if (const TSharedPtr<ISkeletalMeshEditorBinding> Binding = EditorMode->GetModeBinding())
			{
				TSharedPtr<ISkeletalMeshNotifier> Notifier = Binding->GetNotifier();
				Notifier->HandleNotification(SelectedBones, ESkeletalMeshNotifyType::HierarchyChanged);
				Notifier->HandleNotification(SelectedBones, ESkeletalMeshNotifyType::BonesSelected);
			}
		}
		
		RefSkeletonTree.Reset();
		RefSkeletonWidget.Reset();
		
		TreeBindings.Remove(InEditingInterface);
	}
}

const FName& USkeletalMeshEditorContextObject::GetSkeletonTreeTabId()
{
	static const FName SkeletonTreeId(TEXT("SkeletonTreeView"));
	return SkeletonTreeId;
}

TSharedPtr<ISkeletalMeshEditorBinding> USkeletalMeshEditorContextObject::GetBinding() const
{
	return EditorMode.IsValid() ? EditorMode->GetModeBinding() : nullptr;
}
