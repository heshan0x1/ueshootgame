// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAF/AbstractSkeleton/AbstractSkeletonSetBinding.h"

#include "UAF/AbstractSkeleton/AbstractSkeletonSetCollection.h"

// Bone Bindings

#include UE_INLINE_GENERATED_CPP_BY_NAME(AbstractSkeletonSetBinding)

bool UAbstractSkeletonSetBinding::AddBoneToSet(const FName BoneName, const FName SetName)
{
	if (!SetCollection->HasSet(SetName) && SetName != NAME_None)
	{
		return false; // Set does not exist
	}

	if (IsBoneInSet(BoneName))
	{
		return false; // Already exists in a set
	}

	FAbstractSkeleton_BoneBinding& Binding = BoneBindings.AddDefaulted_GetRef();
	Binding.BoneName = BoneName;
	Binding.SetName = SetName;

	return true;
}

bool UAbstractSkeletonSetBinding::RemoveBoneFromSet(const FName BoneName)
{
	const int32 BindingIndex = BoneBindings.IndexOfByPredicate([&](const FAbstractSkeleton_BoneBinding& Binding)
	{
		return Binding.BoneName == BoneName;
	});

	if (BindingIndex != INDEX_NONE)
	{
		BoneBindings.RemoveAtSwap(BindingIndex);
		return true;
	}
	else
	{
		return false;
	}
}

bool UAbstractSkeletonSetBinding::IsBoneInSet(const FName BoneName) const
{
	return BoneBindings.ContainsByPredicate([&](const FAbstractSkeleton_BoneBinding& Binding)
		{
			return Binding.BoneName == BoneName;
		});
}

bool UAbstractSkeletonSetBinding::IsBoneInSet(const FName BoneName, const FName SetName) const
{
	return BoneBindings.ContainsByPredicate([&](const FAbstractSkeleton_BoneBinding& Binding)
		{
			return Binding.BoneName == BoneName && Binding.SetName == SetName;
		});
}

FName UAbstractSkeletonSetBinding::GetBoneSet(const FName BoneName)
{
	const FAbstractSkeleton_BoneBinding* const Binding = BoneBindings.FindByPredicate([&](const FAbstractSkeleton_BoneBinding& Binding)
		{
			return Binding.BoneName == BoneName;
		});

	return Binding ? Binding->SetName : NAME_None;
}

const TConstArrayView<FAbstractSkeleton_BoneBinding> UAbstractSkeletonSetBinding::GetBoneBindings() const
{
	return BoneBindings;
}

// Attribute Bindings

bool UAbstractSkeletonSetBinding::AddAttributeToSet(const FAnimationAttributeIdentifier Attribute, const FName SetName)
{
	if (!SetCollection)
	{
		return false;
	}

	if (!SetCollection->HasSet(SetName) && SetName != NAME_None)
	{
		return false; // Set does not exist
	}

	if (IsAttributeInSet(Attribute))
	{
		return false; // Already exists in a set
	}

	if (IsAttributeInSet(Attribute, NAME_None))
	{
		FAbstractSkeleton_AttributeBinding* UnboundAttribute = AttributeBindings.FindByPredicate([Attribute, SetName](const FAbstractSkeleton_AttributeBinding& Binding)
		{
			return Binding.Attribute == Attribute && Binding.SetName == NAME_None;
		});

		if (ensure(UnboundAttribute))
		{
			UnboundAttribute->SetName = SetName;
			return true;
		}
		else
		{
			return false;
		}
	}

	FAbstractSkeleton_AttributeBinding& Binding = AttributeBindings.AddDefaulted_GetRef();
	Binding.Attribute = Attribute;
	Binding.SetName = SetName;

	return true;
}

bool UAbstractSkeletonSetBinding::RemoveAttributeFromSet(const FAnimationAttributeIdentifier Attribute)
{
	const int32 BindingIndex = AttributeBindings.IndexOfByPredicate([&](const FAbstractSkeleton_AttributeBinding& Binding)
	{
		return Binding.Attribute == Attribute;
	});

	if (BindingIndex != INDEX_NONE)
	{
		AttributeBindings.RemoveAtSwap(BindingIndex);
		return true;
	}
	else
	{
		return false;
	}
}

bool UAbstractSkeletonSetBinding::IsAttributeInSet(const FAnimationAttributeIdentifier Attribute) const
{
	return AttributeBindings.ContainsByPredicate([&](const FAbstractSkeleton_AttributeBinding& Binding)
		{
			return Binding.Attribute.GetName() == Attribute.GetName() && Binding.Attribute.GetBoneName() == Attribute.GetBoneName() && Binding.SetName != NAME_None;
		});
}

bool UAbstractSkeletonSetBinding::IsAttributeInSet(const FAnimationAttributeIdentifier Attribute, const FName SetName) const
{
	return AttributeBindings.ContainsByPredicate([&](const FAbstractSkeleton_AttributeBinding& Binding)
		{
			return Binding.Attribute.GetName() == Attribute.GetName() && Binding.Attribute.GetBoneName() == Attribute.GetBoneName() && Binding.SetName == SetName;
		});
}

const TConstArrayView<FAbstractSkeleton_AttributeBinding> UAbstractSkeletonSetBinding::GetAttributeBindings() const
{
	return AttributeBindings;
}

bool UAbstractSkeletonSetBinding::SetSkeleton(const TObjectPtr<USkeleton> InSkeleton)
{
	Skeleton = InSkeleton;

	BoneBindings.Empty();
	AttributeBindings.Empty();

	return true;
}

TObjectPtr<USkeleton> UAbstractSkeletonSetBinding::GetSkeleton() const
{
	return Skeleton;
}

bool UAbstractSkeletonSetBinding::SetSetCollection(const TObjectPtr<const UAbstractSkeletonSetCollection> InSetCollection)
{
	SetCollection = InSetCollection;

	BoneBindings.Empty();
	AttributeBindings.Empty();

	return true;
}

TObjectPtr<const UAbstractSkeletonSetCollection> UAbstractSkeletonSetBinding::GetSetCollection() const
{
	return SetCollection;
}
