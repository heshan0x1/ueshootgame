// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMesh/SkeletalMeshEditingInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkeletalMeshEditingInterface)

TSharedPtr<ISkeletalMeshNotifier> ISkeletalMeshEditingInterface::GetNotifier()
{
	if (!Notifier)
	{
		Notifier = MakeShared<FSkeletalMeshToolNotifier>(TWeakInterfacePtr<ISkeletalMeshEditingInterface>(this));
	}
	
	return Notifier;
}



bool ISkeletalMeshEditingInterface::NeedsNotification() const
{
	return Notifier && Notifier->Delegate().IsBound(); 
}

TWeakObjectPtr<USkeletonModifier> ISkeletalMeshEditingInterface::GetModifier() const
{
	return nullptr;
}

FSkeletalMeshToolNotifier::FSkeletalMeshToolNotifier(TWeakInterfacePtr<ISkeletalMeshEditingInterface> InInterface)
	: ISkeletalMeshNotifier()
	, Interface(InInterface)
{}

void FSkeletalMeshToolNotifier::HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
{
	if (Interface.IsValid())
	{
		Interface->HandleSkeletalMeshModified(BoneNames, InNotifyType);
	}
}
