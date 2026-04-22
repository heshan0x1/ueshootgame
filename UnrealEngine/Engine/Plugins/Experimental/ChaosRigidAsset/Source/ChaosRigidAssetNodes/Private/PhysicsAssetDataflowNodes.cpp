// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsAssetDataflowNodes.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Misc/WildcardString.h"
#include "DataflowAttachment.h"
#include "Dataflow/DataflowDebugDrawInterface.h"
#include "Dataflow/DataflowDebugDrawObject.h"
#include "Animation/Skeleton.h"
#include "SkeletalDebugRendering.h"
#include "PhysicsAssetBuilder.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

FDataflowPhysicsAssetTerminalNode::FDataflowPhysicsAssetTerminalNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid /*= FGuid::NewGuid()*/)
	: FDataflowTerminalNode(InParam, InGuid)
{
	RegisterInputConnection(&State);
	RegisterInputConnection(&PhysicsAsset);
}

void FDataflowPhysicsAssetTerminalNode::SetAssetValue(TObjectPtr<UObject> Asset, UE::Dataflow::FContext& Context) const
{
	UPhysicsAsset* Target = Asset ? Cast<UPhysicsAsset>(Asset->GetOuter()) : nullptr;

	if(!Target)
	{
		Target = GetValue(Context, &PhysicsAsset);
	}

	if(Target)
	{
		const FPhysicsAssetDataflowState InState = GetValue(Context, &State);
		InState.DebugLog();

		UE::Chaos::RigidAsset::FPhysicsAssetBuilder::Make(InState.TargetSkeleton, InState.Bodies.ToArray(), InState.Constraints.ToArray())
			.SetTargetAsset(Target)
			.Build();
	}
}

void FDataflowPhysicsAssetTerminalNode::Evaluate(UE::Dataflow::FContext& Context) const
{

}

void UE::Dataflow::RegisterPhysicsAssetTerminalNode()
{
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowPhysicsAssetTerminalNode);
}

void UE::Dataflow::RegisterPhysicsAssetNodes()
{
	// Asset state
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowPhysicsAssetMakeState);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowPhysicsAssetAddBody);

	// Body Setup
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowMakeBody);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowSetBodyGeometry);

	// Joints
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowMakeJoint);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowAutoConstrainBodies);

	// Body Geometry
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowAggGeomAddShape);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowCreateGeometryForBones);

	// Bone Selections
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowNewBoneSelection);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowAppendBoneSelection);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowSelectBonesByName);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowSelectConnectedBones);

	RegisterBoneGeometryGeneratorNodes();
	RegisterConstraintGeneratorNodes();
}

FDataflowPhysicsAssetAddBody::FDataflowPhysicsAssetAddBody(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid /*= FGuid::NewGuid()*/)
	: FRigidDataflowNode(InParam, InGuid)
{
	AddInputs(Body, State);
	AddPassthroughs(State);
}

void FDataflowPhysicsAssetAddBody::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&State))
	{
		const USkeletalBodySetup* InBody = GetValue(Context, &Body);

		FPhysicsAssetDataflowState InState = GetValue(Context, &State);

		if(InBody)
		{
			InState.Bodies.Emplace(Cast<USkeletalBodySetup>(StaticDuplicateObject(InBody, GetTransientPackage())));
		}

		SetValue(Context, InState, &State);
	}
}

struct FDataflowDebugDrawBodySetupObject : public FDataflowDebugDrawBaseObject
{
	FDataflowDebugDrawBodySetupObject(IDataflowDebugDrawInterface::FDataflowElementsType& InDataflowElements, TObjectPtr<USkeletalBodySetup> InBody)
		: FDataflowDebugDrawBaseObject(InDataflowElements)
		, Body(InBody)
	{
	}

	virtual void PopulateDataflowElements() override
	{
	}

	virtual void DrawDataflowElements(FPrimitiveDrawInterface* PDI) override
	{
		if(!Body)
		{
			return;
		}

		const FKAggregateGeom& Geom = Body->AggGeom;
		const int32 NumGeoms = Geom.GetElementCount();

		for(int32 Index = 0; Index < NumGeoms; ++Index)
		{
			const FKShapeElem* Elem = Geom.GetElement(Index);
			Elem->DrawElemWire(PDI, Elem->GetTransform(), 1.0f, FColor::White);
		}
	}

	virtual FBox ComputeBoundingBox() const override
	{
		if(!Body)
		{
			return FBox(EForceInit::ForceInitToZero);
		}

		return Body->AggGeom.CalcAABB(FTransform::Identity);
	}

private:
	TObjectPtr<USkeletalBodySetup> Body;
};

struct FDataflowDebugDrawBoneSelectionObject : public FDataflowDebugDrawBaseObject
{
	FDataflowDebugDrawBoneSelectionObject(IDataflowDebugDrawInterface::FDataflowElementsType& InDataflowElements, FRigidAssetBoneSelection InSelection)
		: FDataflowDebugDrawBaseObject(InDataflowElements)
		, Selection(InSelection)
	{
	}

	virtual void PopulateDataflowElements() override
	{
	}

	virtual void DrawDataflowElements(FPrimitiveDrawInterface* PDI) override
	{
		if(!Selection.Mesh)
		{
			return;
		}

		FReferenceSkeleton& RefSkel = Selection.Mesh->GetRefSkeleton();

		const int32 NumBones = RefSkel.GetNum();
		TArray<FTransform> BoneTransforms;
		TArray<FBoneIndexType> RequiredBones;
		TArray<FLinearColor> BoneColors;
		TArray<int32> SelectedBones;
		TArray<TRefCountPtr<HHitProxy>> HitProxies;

		RefSkel.GetBoneAbsoluteTransforms(BoneTransforms);

		for(int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			RequiredBones.Add(BoneIndex);
		}

		for(const FRigidAssetBoneInfo& Bone : Selection.SelectedBones)
		{
			SelectedBones.Add(Bone.Index);
		}

		FSkelDebugDrawConfig DrawConfig;
		DrawConfig.bUseMultiColorAsDefaultColor = false;
		DrawConfig.BoneDrawMode = EBoneDrawMode::All;
		DrawConfig.BoneDrawSize = 1.f;
		DrawConfig.bAddHitProxy = false;
		DrawConfig.bForceDraw = true;
		DrawConfig.DefaultBoneColor = FLinearColor::Gray;
		DrawConfig.AffectedBoneColor = FLinearColor::Gray;
		DrawConfig.SelectedBoneColor = FLinearColor{ 1.0f, 0.75f, 0.1f };
		DrawConfig.ParentOfSelectedBoneColor = FLinearColor::Gray;

		SkeletalDebugRendering::DrawBones(
			PDI, FVector::Zero(),
			RequiredBones,
			RefSkel,
			BoneTransforms,
			SelectedBones,
			BoneColors,
			HitProxies,
			DrawConfig
		);
	}

	virtual FBox ComputeBoundingBox() const override
	{
		if(!Selection.Mesh)
		{
			return FBox(EForceInit::ForceInitToZero);
		}

		FReferenceSkeleton& RefSkel = Selection.Mesh->GetRefSkeleton();
		TArray<FTransform> BoneTransforms;

		RefSkel.GetBoneAbsoluteTransforms(BoneTransforms);

		FBox Result;

		for(const FTransform& Transform : BoneTransforms)
		{
			Result += Transform.GetTranslation();
		}

		return Result;
	}

private:
	FRigidAssetBoneSelection Selection;
};

struct FDataflowDebugDrawAggGeomObject : public FDataflowDebugDrawBaseObject
{
	FDataflowDebugDrawAggGeomObject(IDataflowDebugDrawInterface::FDataflowElementsType& InDataflowElements, const FKAggregateGeom* InGeom)
		: FDataflowDebugDrawBaseObject(InDataflowElements)
		, Geom(InGeom)
	{
	}

	void PopulateDataflowElements() override
	{
	}

	void DrawDataflowElements(FPrimitiveDrawInterface* PDI) override
	{
		if(!Geom)
		{
			return;
		}

		const int32 NumElems = Geom->GetElementCount();

		int32 ElementOffset = DataflowElements.Num();
		int32 ElementNum = NumElems;

		for(int32 Index = 0; Index < NumElems; ++Index)
		{
			const FKShapeElem* Elem = Geom->GetElement(Index);
			Elem->DrawElemWire(PDI, Elem->GetTransform(), 1.0f, FColor::Cyan);
		}
	}

	FBox ComputeBoundingBox() const override
	{
		if(!Geom)
		{
			return {};
		}

		return Geom->CalcAABB(FTransform::Identity);
	}

private:
	const FKAggregateGeom* Geom = nullptr;
};

#if WITH_EDITOR
bool FDataflowMakeBody::CanDebugDraw() const
{
	return true;
}

bool FDataflowMakeBody::CanDebugDrawViewMode(const FName& ViewModeName) const
{
	return true;
}

void FDataflowMakeBody::DebugDraw(UE::Dataflow::FContext& Context, IDataflowDebugDrawInterface& DataflowRenderingInterface, const FDebugDrawParameters& DebugDrawParameters) const
{
	if(Body)
	{
		TRefCountPtr<IDataflowDebugDrawObject> BodyDrawObject(MakeDebugDrawObject<FDataflowDebugDrawBodySetupObject>(DataflowRenderingInterface.ModifyDataflowElements(), Body));
		DataflowRenderingInterface.DrawObject(BodyDrawObject);
	}
}
#endif

void FDataflowMakeBody::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Body))
	{
		TObjectPtr<USkeletalBodySetup> NewSetup = CastChecked<USkeletalBodySetup>(StaticDuplicateObject(Body, GetTransientPackage()));

		NewSetup->BoneName = GetValue(Context, &BoneName);

		SetValue(Context, NewSetup, &Body);
	}
}

void FDataflowMakeBody::Register(const UE::Dataflow::FNodeParameters& InParam)
{
	// Construct the template object for this node
	Body = NewObject<USkeletalBodySetup>(GetOwner());

	AddInputs(BoneName);
	AddOutputs(Body);
}

void FDataflowMakeJoint::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Joint))
	{
		TObjectPtr<UPhysicsConstraintTemplate> NewJoint = CastChecked<UPhysicsConstraintTemplate>(StaticDuplicateObject(Joint, GetTransientPackage()));

		SetValue(Context, NewJoint, &Joint);
	}
}

void FDataflowMakeJoint::Register(const UE::Dataflow::FNodeParameters& InParam)
{
	// Construct the template object for this node
	Joint = NewObject<UPhysicsConstraintTemplate>(GetOwner());

	AddOutputs(Joint);
}

void FDataflowSelectBonesByName::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Selection))
	{
		FPhysicsAssetDataflowState InState = GetValue(Context, &State);

		TObjectPtr<USkeleton> Skeleton = InState.TargetSkeleton;
		TObjectPtr<USkeletalMesh> Mesh = InState.TargetMesh;

		if(!Skeleton)
		{
			Context.Error(TEXT("Attempted to make a selection without a Skeleton"), this);
			SetValue(Context, FRigidAssetBoneSelection{}, &Selection);
			return;
		}

		if(!Mesh)
		{
			Context.Error(TEXT("Attempted to make a selection without a SkeletalMesh"), this);
			SetValue(Context, FRigidAssetBoneSelection{}, &Selection);
			return;
		}

		const FWildcardString Pattern = SearchString;
		const FReferenceSkeleton RefSkel = Skeleton->GetReferenceSkeleton();

		FRigidAssetBoneSelection NewSelection;
		NewSelection.Skeleton = Skeleton;
		NewSelection.Mesh = Mesh;

		for(int32 BoneIndex = 0; BoneIndex < RefSkel.GetNum(); ++BoneIndex)
		{
			FName BoneName = RefSkel.GetBoneName(BoneIndex);

			if(Pattern.IsMatch(BoneName.ToString()))
			{
				NewSelection.SelectedBones.Emplace(BoneName, BoneIndex, RefSkel.GetDepthBetweenBones(BoneIndex, 0));
			}
		}

		NewSelection.SortBones();
		SetValue(Context, MoveTemp(NewSelection), &Selection);
	}
}

void FDataflowSelectBonesByName::Register(const UE::Dataflow::FNodeParameters& InParam)
{
	AddInputs(State);
	AddOutputs(Selection);
}

void FDataflowAppendBoneSelection::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Selection))
	{
		const FRigidAssetBoneSelection& InA = GetValue(Context, &A);
		const FRigidAssetBoneSelection& InB = GetValue(Context, &B);

		if((InA.Skeleton != InB.Skeleton) || (InA.Mesh != InB.Mesh))
		{
			Context.Error(TEXT("Attmepted to append selections from multiple skeletons or meshes"), this);
			SetValue(Context, FRigidAssetBoneSelection{}, &Selection);
			return;
		}

		FRigidAssetBoneSelection NewSelection = InA;

		for(const FRigidAssetBoneInfo& Bone : InB.SelectedBones)
		{
			NewSelection.SelectedBones.AddUnique(Bone);
		}

		NewSelection.SortBones();
		SetValue(Context, MoveTemp(NewSelection), &Selection);
	}
}

void FDataflowAppendBoneSelection::Register()
{
	AddInputs(A, B);
	AddOutputs(Selection);
}

void FDataflowSelectConnectedBones::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Selection))
	{
		const FRigidAssetBoneSelection& InSelection = GetValue(Context, &Selection);

		if(!InSelection.Skeleton)
		{
			Context.Error(TEXT("Attempted to make a selection without a Skeleton"), this);
			SafeForwardInput(Context, &Selection, &Selection);
			return;
		}

		const FReferenceSkeleton& RefSkel = InSelection.Skeleton->GetReferenceSkeleton();

		FRigidAssetBoneSelection NewSelection = InSelection;

		for(const FRigidAssetBoneInfo& Bone : InSelection.SelectedBones)
		{
			int32 CurrentIndex = Bone.Index;
			for(int32 UpCount = 0; UpCount < DistanceUp; ++UpCount)
			{
				int32 NewIndex = RefSkel.GetParentIndex(CurrentIndex);

				if(NewIndex == INDEX_NONE)
				{
					// Nothing more in this direction
					break;
				}

				if(!NewSelection.ContainsIndex(NewIndex))
				{
					NewSelection.SelectedBones.Emplace(RefSkel.GetBoneName(NewIndex), NewIndex, RefSkel.GetDepthBetweenBones(NewIndex, 0));
				}

				CurrentIndex = NewIndex;
			}

			int32 Level = 0;
			TArray<int32> BoneStack;
			BoneStack.Add(Bone.Index);

			while(Level < DistanceDown)
			{
				TArray<int32> NextBoneStack;
				while(BoneStack.Num() > 0)
				{
					const int32 Current = BoneStack.Pop(EAllowShrinking::No);

					TArray<int32> ChildBoneIndices;
					RefSkel.GetDirectChildBones(Current, ChildBoneIndices);

					for(const int32 ChildIndex : ChildBoneIndices)
					{
						if(!NewSelection.ContainsIndex(ChildIndex))
						{
							NewSelection.SelectedBones.Emplace(RefSkel.GetBoneName(ChildIndex), ChildIndex, RefSkel.GetDepthBetweenBones(ChildIndex, 0));
						}

						NextBoneStack.Push(ChildIndex);
					}
				}
				BoneStack = MoveTemp(NextBoneStack);

				++Level;
			}
		}

		NewSelection.SortBones();
		SetValue(Context, MoveTemp(NewSelection), &Selection);
	}
}

void FDataflowSelectConnectedBones::Register(const UE::Dataflow::FNodeParameters& InParam)
{
	AddInputs(Selection);
	AddPassthroughs(Selection);
}

#if WITH_EDITOR
bool FDataflowCreateGeometryForBones::CanDebugDraw() const
{
	return true;
}

bool FDataflowCreateGeometryForBones::CanDebugDrawViewMode(const FName& ViewModeName) const
{
	return true;
}

void FDataflowCreateGeometryForBones::DebugDraw(UE::Dataflow::FContext& Context, IDataflowDebugDrawInterface& DataflowRenderingInterface, const FDebugDrawParameters& DebugDrawParameters) const
{
	const FRigidAssetBoneSelection& InSelection = GetValue(Context, &Selection);
	TRefCountPtr<IDataflowDebugDrawObject> DrawObject(MakeDebugDrawObject<FDataflowDebugDrawBoneSelectionObject>(DataflowRenderingInterface.ModifyDataflowElements(), InSelection));
	DataflowRenderingInterface.DrawObject(DrawObject);

	TObjectPtr<UBoneGeometryGenerator> InGenerator = GetValue(Context, &Generator);
	if(InGenerator && InGenerator->CanDebugDraw())
	{
		InGenerator->DebugDraw(Context, DataflowRenderingInterface, DebugDrawParameters, InSelection);
	}
}
#endif

void FDataflowCreateGeometryForBones::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&State))
	{
		TObjectPtr<UBoneGeometryGenerator> InGenerator = GetValue(Context, &Generator);

		if(!InGenerator)
		{
			Context.Warning(TEXT("FDataflowCreateGeometryForBones: No method selected for body generation"), this);
			SafeForwardInput(Context, &State, &State);
			return;
		}

		const FRigidAssetBoneSelection& InSelection = GetValue(Context, &Selection);

		if(!InSelection.Skeleton || !InSelection.Mesh)
		{
			Context.Error(TEXT("Attempted to make geometry without a skeleton or mesh"), this);
			SafeForwardInput(Context, &State, &State);
			return;
		}

		TObjectPtr<USkeletalBodySetup> InTemplate = GetValue(Context, &TemplateBody);
		if(!InTemplate)
		{
			Context.Error(TEXT("Attempted to make a body without a template body setup"));
			SafeForwardInput(Context, &State, &State);
			return;
		}

		FPhysicsAssetDataflowState InState = GetValue(Context, &State);
		TArray<UE::Chaos::RigidAsset::FSimpleGeometry> Geoms = InGenerator->Build(InSelection);

		if(Geoms.Num() != InSelection.SelectedBones.Num())
		{
			Context.Error(TEXT("Failed to generate bodies for each bone in the selection"));
			SafeForwardInput(Context, &State, &State);
			return;
		}

		for(int32 Index = 0; Index < Geoms.Num(); ++Index)
		{
			const UE::Chaos::RigidAsset::FSimpleGeometry& Geom = Geoms[Index];
			const FRigidAssetBoneInfo& Bone = InSelection.SelectedBones[Index];

			TObjectPtr<USkeletalBodySetup> NewSetup = CastChecked<USkeletalBodySetup>(StaticDuplicateObject(InTemplate, GetTransientPackage()));

			if(!Geom.GeometryElement)
			{
				Context.Error(TEXT("Generator failed to create usable geometry"));
				SafeForwardInput(Context, &State, &State);
				return;
			}

			NewSetup->AggGeom.VisitShapeAndContainer(*Geom.GeometryElement.Get(), [] <typename ElemType> (const ElemType & ElemTyped, TArray<ElemType>&Container)
			{
				Container.Add(ElemTyped);
			});

			NewSetup->BoneName = Bone.Name;

			InState.Bodies.Add(NewSetup);
		}

		SetValue(Context, MoveTemp(InState), &State);
	}
}

void FDataflowCreateGeometryForBones::Register()
{
	AddInputs(Generator, TemplateBody, State, Selection);
	AddPassthroughs(State);
}

void FDataflowAutoConstrainBodies::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	TObjectPtr<UConstraintGenerator> InGenerator = GetValue(Context, &Generator);

	if(!InGenerator)
	{
		Context.Warning(TEXT("FDataflowAutoConstrainBodies: No method selected for constraint generation"), this);
		SafeForwardInput(Context, &State, &State);
		return;
	}

	const FRigidAssetBoneSelection& InSelection = GetValue(Context, &Selection);

	if(!InSelection.Skeleton || !InSelection.Mesh)
	{
		Context.Error(TEXT("Attempted to make constraints without a skeleton or mesh"), this);
		SafeForwardInput(Context, &State, &State);
		return;
	}

	TObjectPtr<UPhysicsConstraintTemplate> InTemplate = GetValue(Context, &TemplateConstraint);

	if(!InTemplate)
	{
		Context.Error(TEXT("Attempted to make a constraint without a template"));
		SafeForwardInput(Context, &State, &State);
		return;
	}

	FPhysicsAssetDataflowState InState = GetValue(Context, &State);
	TArray<TObjectPtr<UPhysicsConstraintTemplate>> GeneratedConstraints = InGenerator->Build(InTemplate, InSelection);

	InState.Constraints.Append(GeneratedConstraints);

	SetValue(Context, MoveTemp(InState), &State);
}

void FDataflowAutoConstrainBodies::Register()
{
	AddInputs(Generator, TemplateConstraint, State, Selection);
	AddPassthroughs(State);
}

#if WITH_EDITOR
bool FDataflowSetBodyGeometry::CanDebugDraw() const
{
	return true;
}

bool FDataflowSetBodyGeometry::CanDebugDrawViewMode(const FName& ViewModeName) const
{
	return true;
}

void FDataflowSetBodyGeometry::DebugDraw(UE::Dataflow::FContext& Context, IDataflowDebugDrawInterface& DataflowRenderingInterface, const FDebugDrawParameters& DebugDrawParameters) const
{
	if(DebugDrawParameters.bNodeIsSelected || DebugDrawParameters.bNodeIsPinned)
	{
		if(Body)
		{
			TRefCountPtr<IDataflowDebugDrawObject> BodyDrawObject(MakeDebugDrawObject<FDataflowDebugDrawBodySetupObject>(DataflowRenderingInterface.ModifyDataflowElements(), Body));
			DataflowRenderingInterface.DrawObject(BodyDrawObject);
		}
	}
}
#endif

void FDataflowSetBodyGeometry::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Body))
	{
		TObjectPtr<USkeletalBodySetup> InSetup = GetValue(Context, &Body);

		check(InSetup);

		TObjectPtr<USkeletalBodySetup> NewSetup = CastChecked<USkeletalBodySetup>(StaticDuplicateObject(InSetup, GetTransientPackage()));

		NewSetup->AggGeom = GetValue(Context, &Geometry);

		SetValue(Context, NewSetup, &Body);
	}
}

void FDataflowSetBodyGeometry::Register()
{
	AddInputs(Body, Geometry);
	AddPassthroughs(Body);
}

void FDataflowNewBoneSelection::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Selection))
	{
		TObjectPtr<USkeleton> InSkeleton = GetValue(Context, &Skeleton);
		TObjectPtr<USkeletalMesh> InMesh = GetValue(Context, &Mesh);

		// Unless overridden, get the skeleton from the currently edited asset
		if(!InSkeleton)
		{
			if(TObjectPtr<UPhysicsAsset> PhysAsset = GetAttachmentOwnerAs<UPhysicsAsset>(Context))
			{
				InSkeleton = PhysAsset->GetPreviewMesh() ? PhysAsset->GetPreviewMesh()->GetSkeleton() : nullptr;
			}
			else
			{
				Context.Error(TEXT("Attempted to make a selection without a Skeleton"), this);
				SetValue(Context, FRigidAssetBoneSelection{}, &Selection);
				return;
			}
		}

		// Unless overridden, pull the skeletal mesh from the selected skeleton
		if(!InMesh)
		{
			if(InSkeleton)
			{
				InMesh = InSkeleton->GetPreviewMesh();
			}
			else
			{
				Context.Error(TEXT("Attempted to make a selection without a Skeletal Mesh"), this);
				SetValue(Context, FRigidAssetBoneSelection{}, &Selection);
				return;
			}
		}

		if(!InMesh || !InSkeleton)
		{
			Context.Error(TEXT("Unable to identify a mesh or skeleton"), this);
			SetValue(Context, FRigidAssetBoneSelection{}, &Selection);
			return;
		}

		if(InSkeleton != InMesh->GetSkeleton())
		{
			Context.Error(TEXT("Specified mesh is not compatible with the specified skeleton"), this);
			SetValue(Context, FRigidAssetBoneSelection{}, &Selection);
			return;
		}

		FRigidAssetBoneSelection NewSelection;
		NewSelection.Skeleton = InSkeleton;
		NewSelection.Mesh = InMesh;

		SetValue(Context, MoveTemp(NewSelection), &Selection);
	}
}

void FDataflowNewBoneSelection::Register(const UE::Dataflow::FNodeParameters& InParam)
{
	AddInputs(Skeleton, Mesh);
	AddOutputs(Selection);
}

void FDataflowPhysicsAssetMakeState::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&State))
	{
		TObjectPtr<USkeletalMesh> InTarget = GetValue(Context, &TargetMesh);

		// If there's no override - pick from the context asset
		if(!InTarget)
		{
			if(TObjectPtr<UPhysicsAsset> PhysAsset = GetAttachmentOwnerAs<UPhysicsAsset>(Context))
			{
				InTarget = PhysAsset->GetPreviewMesh();

				if(!InTarget)
				{
					Context.Error(TEXT("Attempted to make a state without a valid mesh"), this);
					SetValue(Context, FPhysicsAssetDataflowState{}, &State);
					return;
				}
			}
			else
			{
				Context.Error(TEXT("Attempted to make a state without a valid skeleton"), this);
				SetValue(Context, FPhysicsAssetDataflowState{}, &State);
				return;
			}
		}

		SetValue(Context, FPhysicsAssetDataflowState{ InTarget->GetSkeleton(), InTarget }, &State);
	}
}

void FDataflowPhysicsAssetMakeState::Register()
{
	AddInputs(TargetMesh);
	AddOutputs(State);
}

#if WITH_EDITOR
bool FDataflowAggGeomAddShape::CanDebugDraw() const
{
	return true;
}

bool FDataflowAggGeomAddShape::CanDebugDrawViewMode(const FName& ViewModeName) const
{
	return true;
}

void FDataflowAggGeomAddShape::DebugDraw(UE::Dataflow::FContext& Context, IDataflowDebugDrawInterface& DataflowRenderingInterface, const FDebugDrawParameters& DebugDrawParameters) const
{
	TRefCountPtr<IDataflowDebugDrawObject> Obj(MakeDebugDrawObject<FDataflowDebugDrawAggGeomObject>(DataflowRenderingInterface.ModifyDataflowElements(), &AggGeom));
	DataflowRenderingInterface.DrawObject(Obj);
}
#endif

void FDataflowAggGeomAddShape::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::RigidAsset;

	if(Out->IsA(&AggGeom))
	{
		const int32 NumShapeInputs = Shapes.Num();

		const FKAggregateGeom& InGeom = GetValue(Context, &AggGeom);

		FKAggregateGeom NewAggGeom = InGeom;

		for(int32 Index = 0; Index < NumShapeInputs; ++Index)
		{
			const FSimpleGeometry& InShape = GetValue(Context, &Shapes[Index]);

			if(InShape.GeometryElement)
			{
				NewAggGeom.AddElement(*InShape.GeometryElement.Get());
			}
		}

		SetValue(Context, MoveTemp(NewAggGeom), &AggGeom);
	}
}

void FDataflowAggGeomAddShape::Register()
{
	AddInputs(AggGeom);
	AddPassthroughs(AggGeom);
}
