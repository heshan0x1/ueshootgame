// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDNaniteAssemblyUtils.h"

#include "USDErrorUtils.h"
#include "USDIntegrationUtils.h"
#include "USDLog.h"
#include "USDTypesConversion.h"
#include "UnrealUSDWrapper.h"

#include "USDMemory.h"

#include "UsdWrappers/UsdRelationship.h"
#include "UsdWrappers/UsdStage.h"

#include "Math/Transform.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usdGeom/pointInstancer.h"
#include "pxr/usd/usdGeom/primvarsAPI.h"
#include "pxr/usd/usdSkel/skeleton.h"
#include "pxr/usd/usdSkel/root.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "USDIncludesEnd.h"

#include <string>

namespace UsdToUnreal::NaniteAssemblyUtils
{
	FMeshEntry::FMeshEntry(
		EMeshCategory InCategory,
		const UE::FSdfPath& InPrimPath,
		const UE::FSdfPath& InSkelBindingPath,
		const TArray<FTransform>& InTransformStack,
		const TArray<FPrimPrototypeEntry>& InPrototypeStack)
		: Category(InCategory)
		, PrimPath(InPrimPath)
		, SkelBindingPath(InSkelBindingPath)
		, TransformStack(InTransformStack)
		, PrototypeStack(InPrototypeStack)
	{

	}

	void FMeshEntry::Print() const
	{
		UE_LOG(LogUsd, Log, TEXT("// -----------------------------------------------------------------------------------"));
		UE_LOG(LogUsd, Log, TEXT("// FMeshEntry "));
		UE_LOG(LogUsd, Log, TEXT("// .. catg: %d"), Category);
		UE_LOG(LogUsd, Log, TEXT("// .. path: %s"), *PrimPath.GetString());
		UE_LOG(LogUsd, Log, TEXT("// ..  uid: %s"), *NodeUid);
		for (int32 Index = 0; Index < TransformStack.Num(); ++Index)
		{
			const FTransform& Xf = TransformStack[Index];
			const FVector  T = Xf.GetLocation();
			const FRotator R = Xf.GetRotation().Rotator();
			const FVector  S = Xf.GetScale3D();

			UE_LOG(LogUsd, Log, TEXT("// ..   xf: [%d]  T: %s  R: %s S: %s"), Index, *T.ToString(), *R.ToString(), *S.ToString());
		}
		for (int32 Index = 0; Index < PrototypeStack.Num(); ++Index)
		{
			const FPrimPrototypeEntry& Proto = PrototypeStack[Index];
			UE_LOG(LogUsd, Log, TEXT("// ..   pi: [%d] %s"), Index, *Proto.PointInstancerPrimPath.GetString());
			UE_LOG(LogUsd, Log, TEXT("//           `-> %s"), *Proto.PrototypePrimPath.GetString());
		}
	}

	FNaniteAssemblyTraversalResult::FNaniteAssemblyTraversalResult(
		ENaniteAssemblyMeshType InMeshType,
		const UE::FSdfPath& InAssemblyRootPrimPath)
		: AssemblyMeshType(InMeshType)
		, AssemblyRootPrimPath(InAssemblyRootPrimPath)
	{

	}

	bool FNaniteAssemblyTraversalResult::AddEntry(const TSharedPtr<FMeshEntry>& Entry)
	{
		const UE::FSdfPath& Path = Entry->PrimPath;

		if (Path.IsPrimPath() 
			&& Path.HasPrefix(AssemblyRootPrimPath) 
			&& !EntryPaths.Contains(Path)
			&& Entry->Category != EMeshCategory::None
			&& Entry->TransformStack.Num() > 0)
		{
			Entries.Add(Entry);
			EntryPaths.Add(Path);
			return true;
		}

		return false;
	}

	bool FNaniteAssemblyTraversalResult::IsMeshType(ENaniteAssemblyMeshType MeshType) const
	{
		return AssemblyMeshType == MeshType;
	}

	TSet<UE::FSdfPath> FNaniteAssemblyTraversalResult::GetTopLevelPointInstancerPaths() const
	{
		TSet<UE::FSdfPath> TopLevelPointInstancerPaths;

		for (const TSharedPtr<FMeshEntry>& Entry : GetEntriesForCategory(EMeshCategory::Part))
		{
			if (!Entry->PrototypeStack.IsEmpty())
			{
				const FPrimPrototypeEntry& FirstPrototype = Entry->PrototypeStack[0];
				if (!FirstPrototype.PointInstancerPrimPath.IsEmpty())
				{
					TopLevelPointInstancerPaths.Add(FirstPrototype.PointInstancerPrimPath);
				}
			}
		}

		return TopLevelPointInstancerPaths;
	}
	                                     
	bool FNaniteAssemblyTraversalResult::CheckAndGetNativeInstancedPartMeshEntries(
		TArray<TSharedPtr<FMeshEntry>>& OutEntries,
		TMap<FString, int32>& OutMeshUidToPartIndexTable,
		TArray<TArray<FString>>& OutPartUids) const
	{
		OutEntries.Empty();
		OutMeshUidToPartIndexTable.Empty();
		OutPartUids.Empty();

		// Native instances don't have an explicit prototype index, so for each
		// unique mesh uid we generate an index as we go along.
		int32 Index = 0;
		for (const TSharedPtr<FMeshEntry>& Entry : GetEntriesForCategory(EMeshCategory::Part))
		{
			// Pointinstancer prototype, skip
			if (!Entry->PrototypeStack.IsEmpty())
			{
				continue;
			}

			if (!ensure(!Entry->NodeUid.IsEmpty() && Entry->TransformStack.Num() == 1))
			{
				return false;
			}

			if (!OutMeshUidToPartIndexTable.Contains(Entry->NodeUid))
			{
				OutPartUids.Add({ Entry->NodeUid });
				OutMeshUidToPartIndexTable.Add(Entry->NodeUid, Index++);
			}
			OutEntries.Add(Entry);
		}
		
		return true;
	}

	bool FNaniteAssemblyTraversalResult::CheckAndGetMeshEntriesByPrototypeIndex(
		const UE::FSdfPath& PointInstancerPrimPath,
		TMap<int32, TArray<TSharedPtr<FMeshEntry>>>& OutMeshEntriesByPrototypeIndex,
		int32& OutPrototypeCount) const
	{
		OutMeshEntriesByPrototypeIndex.Empty();
		OutPrototypeCount = INDEX_NONE;

		// Gather part mesh entries by pointinstancer prototype index
		for (const TSharedPtr<FMeshEntry>& Entry : GetEntriesForCategory(EMeshCategory::Part))
		{
			// Regular non-pointinstancer prim, skip
			if (Entry->PrototypeStack.IsEmpty())
			{
				continue;
			}

			if (Entry->PrototypeStack.Num() > 1)
			{
				// TODO handle (expand) nested pointinstancers
				continue;
			}

			// Does this entry originate from the requested top-level pointinstancer?
			const FPrimPrototypeEntry& FirstProto = Entry->PrototypeStack[0];
			if (FirstProto.PointInstancerPrimPath != PointInstancerPrimPath)
			{
				continue;
			}

			// Guard against invalid prototype count/index data
			if (!ensure(FirstProto.PrototypeCount > FirstProto.PrototypeIndex))
			{
				return false;
			}

			// Set the prototype count, or if set already, validate that this entry agrees with
			// the current count set by some previous entry.
			if (OutPrototypeCount == INDEX_NONE)
			{
				OutPrototypeCount = FirstProto.PrototypeCount;
			}
			else if (!ensure(OutPrototypeCount == FirstProto.PrototypeCount))
			{
				return false;
			}
	
			// Lastly validate that we have a node uid and that the transform stack is correct for the prototype
			// stack - the transform stack should have one extra element because the transform leading up to the
			// top-level pointinstancer must be included as well (even if it's just the identity transform).
			if (!ensure(!Entry->NodeUid.IsEmpty() && Entry->TransformStack.Num() == Entry->PrototypeStack.Num() + 1))
			{
				return false;
			}

			// Add the entry
			OutMeshEntriesByPrototypeIndex.FindOrAdd(FirstProto.PrototypeIndex).Add(Entry);
		}
		return true;
	}

	bool FNaniteAssemblyTraversalResult::GetRemappedPartsForTopLevelPointInstancer(
		const UE::FUsdPrim& PointInstancerPrim,
		int32 IndexStartOffset,
		FPrototypesToPartsRemappingInfo& OutRemappingInfo) const
	{
		// Split meshes in PointInstancer prototypes into separate part slots if a non-identity transform is authored. 
		// A part uasset’s transform can’t be baked in since it may be reused across several uniquely transformed 
		// prototype subtrees, so per-subtree transforms must be applied to the final instance transform in the Nanite 
		// assembly node.
		//
		// Example: a PointInstancer with two prototypes and three USD meshes produces three uassets — A and B in 
		// prototype[0], and C in prototype[1].
		//
		// If A and B share the identity transform, we submit them together:
		//
		// Builder
		//  - parts[0] = {/A.uasset, /B.uasset}
		//  - parts[1] = {/C.uasset}
		//
		// If A or B has a unique transform, each gets its own part:
		//
		// Builder
		//  - parts[0] = {/A.uasset}
		//  - parts[1] = {/B.uasset}
		//  - parts[2] = {/C.uasset}
		//
		// This requires expanding protoIndices/transform arrays and remapping indices to the new part layout. 
		// OutRemappingInfo stores the mapping from original prototype indices to new part index ranges, e.g.:
		//   TMap<int32, FInt32Range> = { 0: [0:2), 1: [2:3) }
		//
		// Lastly, when multiple PointInstancers are merged into one assembly description, an IndexStartOffset is added 
		// to ensure adjusted protoIndices still point to the correct parts.

		TMap<int32, TArray<TSharedPtr<FMeshEntry>>> MeshEntriesByPrototypeIndex;
		int32 PrototypeCount = 0;
		if (!CheckAndGetMeshEntriesByPrototypeIndex(PointInstancerPrim.GetPrimPath(), MeshEntriesByPrototypeIndex, PrototypeCount))
		{
			return false;
		}

		TArray<TArray<FString>> PartUids;
		TArray<FTransform> LocalTransforms;
		TMap<int32, FInt32Range> OriginalPrototypeIndexToNewPartRangeTable;

		bool bAllPrototypeMeshesHaveLocalIdentityTransform = true;

		for (int32 PrototypeIndex = 0; PrototypeIndex < PrototypeCount; ++PrototypeIndex)
		{
			int32 StartOfRange = PartUids.Num() + IndexStartOffset;
			int32 EndOfRange = StartOfRange;

			auto AddPartUids = [&PartUids, &LocalTransforms, &EndOfRange](
					const TArray<FString>& InPartUids, 
					const FTransform& LocalTransform = FTransform())
				{
					PartUids.Add(InPartUids);
					LocalTransforms.Add(LocalTransform);
					EndOfRange++;
				};

			auto FinalizeRangeForCurrentIndex = [PrototypeIndex, &OriginalPrototypeIndexToNewPartRangeTable, &StartOfRange, &EndOfRange]() -> bool
				{
					if (!ensure(EndOfRange > StartOfRange))
					{
						return false;
					}
					OriginalPrototypeIndexToNewPartRangeTable.Add(
						PrototypeIndex,
						FInt32Range(TRangeBound<int32>::Inclusive(StartOfRange), TRangeBound<int32>::Exclusive(EndOfRange))
					);
					return true;
				};

			if (!MeshEntriesByPrototypeIndex.Contains(PrototypeIndex))
			{
				// No meshes available for this index, however we must add an empty slot to keep the ranges intact
				AddPartUids({});
				if (FinalizeRangeForCurrentIndex())
				{
					continue;
				}
				return false;
			}

			// Partition entries by transform, into identity and non-identity groups
			TArray<TSharedPtr<FMeshEntry>> IdentityEntries;
			TArray<TSharedPtr<FMeshEntry>> NonIdentityEntries;
			TArray<FTransform> NonIdentityTransforms;
			for (const TSharedPtr<FMeshEntry>& Entry : MeshEntriesByPrototypeIndex.FindChecked(PrototypeIndex))
			{
				const FTransform& LeafTransform = Entry->TransformStack.Last();
				if (LeafTransform.Equals(FTransform::Identity, SMALL_NUMBER))
				{
					IdentityEntries.Add(Entry);
				}
				else
				{
					NonIdentityEntries.Add(Entry);
					NonIdentityTransforms.Add(LeafTransform);
					bAllPrototypeMeshesHaveLocalIdentityTransform = false;
				}
			}

			// Identity parts can share a single slot
			if (IdentityEntries.Num() > 0)
			{
				AddPartUids(ConvertMeshEntryArrayToMeshUidArray(IdentityEntries));
			}

			// Non-identity parts each requires it's own slot
			for (int32 Index = 0; Index < NonIdentityEntries.Num(); ++Index)
			{
				const TSharedPtr<FMeshEntry>& Entry = NonIdentityEntries[Index];
				AddPartUids({ Entry->NodeUid }, NonIdentityTransforms[Index]);
			}

			if (!FinalizeRangeForCurrentIndex())
			{
				return false;
			}
		}

		if(!ensure(PartUids.Num() == LocalTransforms.Num()))
		{
			return false;
		}

		if (bAllPrototypeMeshesHaveLocalIdentityTransform)
		{
			// No remapping occurred, and so empty the table to signal that the PartUid array can be used as-is
			LocalTransforms.Empty();
			OriginalPrototypeIndexToNewPartRangeTable.Empty();
			if (!ensure(PartUids.Num() == PrototypeCount))
			{
				return false;
			}
		}

		OutRemappingInfo.PartUids = MoveTemp(PartUids);
		OutRemappingInfo.LocalTransforms = MoveTemp(LocalTransforms);
		OutRemappingInfo.OriginalPrototypeIndexToNewPartRangeTable = MoveTemp(OriginalPrototypeIndexToNewPartRangeTable);
		OutRemappingInfo.OriginalPrototypeCount = PrototypeCount;

		return true;
	}

	bool FNaniteAssemblyTraversalResult::HasEntriesForCategory(EMeshCategory Category) const
	{
		for (const TSharedPtr<FMeshEntry>& Entry : Entries)
		{
			if (Entry->Category == Category)
			{
				return true;
			}
		}
		return false;
	}

	TArray<TSharedPtr<FMeshEntry>> FNaniteAssemblyTraversalResult::GetEntriesForCategory(EMeshCategory Category) const
	{
		TArray<TSharedPtr<FMeshEntry>> EntriesForCategory;
		EntriesForCategory.Reserve(Entries.Num());
		for (const TSharedPtr<FMeshEntry>& Entry : Entries)
		{
			if (Entry->Category == Category)
			{
				EntriesForCategory.Add(Entry);
			}
		}
		return EntriesForCategory;
	}

	TArray<FString> FNaniteAssemblyTraversalResult::GetMeshUidsForCategory(EMeshCategory Category) const
	{
		TArray<FString> MeshUidsForCategory;
		for (const TSharedPtr<FMeshEntry>& Entry : Entries)
		{
			if (Entry->Category == Category && ensure(!Entry->NodeUid.IsEmpty()))
			{
				MeshUidsForCategory.Add(Entry->NodeUid);
			}
		}
		return MeshUidsForCategory;
	}

	TArray<FString> FNaniteAssemblyTraversalResult::ConvertMeshEntryArrayToMeshUidArray(const TArray<TSharedPtr<FMeshEntry>>& Entries)
	{
		TArray<FString> MeshUids;
		MeshUids.Reserve(Entries.Num());
		for (const TSharedPtr<FMeshEntry>& Entry : Entries)
		{
			MeshUids.Add(Entry->NodeUid);
		}

		return MeshUids;
	}

	FJointBindingHelper::FJointBindingHelper(const FString& InSkelIdentifier, const TArray<FString>& InJointNames, float InTime)
		: SkelIdentifier(InSkelIdentifier)
		, JointNames(InJointNames)
		, Time(InTime)
	{
		for (int32 JointIndex = 0; JointIndex < JointNames.Num(); ++JointIndex)
		{
			JointIndexByJointNameTable.Add(JointNames[JointIndex], JointIndex);
		}
	}

	bool FJointBindingHelper::Get(const UE::FUsdPrim& Prim, TArray<int32>& OutJointIndices, TArray<float>& OutJointWeights, int32& OutElementSize) const
	{
		return GetJointBindingData(
			Prim,
			SkelIdentifier,
			JointNames,
			Time,
			OutJointIndices,
			OutJointWeights,
			OutElementSize,
			&JointIndexByJointNameTable);
	}

	template <class T>
	bool ApplyMaskToArray(std::vector<bool> const& Mask, pxr::VtArray<T>* DataArray, const int ElementSize = 1)
	{
		// XXX Temp replacement/fix for OpenUSD UsdGeomPointInstancer::ApplyMaskToArray implementation
		// which (as of 25.5) does not correctly handle element sizes greater than 1.
		// 
		if (!DataArray)
		{
			return false;
		}

		size_t MaskSize = Mask.size();
		if (MaskSize == 0 || DataArray->size() == static_cast<size_t>(ElementSize))
		{
			return true;
		}
		else if ((MaskSize * ElementSize) != DataArray->size())
		{
			return false;
		}

		T* BeginData = DataArray->data();
		T* CurrData = BeginData;
		size_t NumPreserved = 0;
		for (size_t i = 0; i < MaskSize; ++i)
		{
			if (Mask[i])
			{
				for (int j = 0; j < ElementSize; ++j)
				{
					*CurrData = BeginData[(i * ElementSize) + j];
					++CurrData;
				}
				NumPreserved += ElementSize;
			}
		}
		if (NumPreserved < DataArray->size())
		{
			DataArray->resize(NumPreserved);
		}
		return true;
	}

	bool GetTokenOrStringAttributeValue(const pxr::UsdAttribute& Attribute, pxr::TfToken& OutValue)
	{
		if (!Attribute)
		{
			return false;
		}

		if (pxr::VtValue Value; Attribute.Get(&Value, pxr::UsdTimeCode::EarliestTime()))
		{
			if (Value.IsHolding<pxr::TfToken>())
			{
				OutValue = Value.UncheckedGet<pxr::TfToken>();
				return true;
			}
			else if (Value.IsHolding<std::string>())
			{
				OutValue = pxr::TfToken(Value.UncheckedGet<std::string>());
				return true;
			}
		}
		return false;
	}

	bool GetJointBindingData(
		const UE::FUsdPrim& InPrim,
		const FString& SkelIdentifier,
		const TArray<FString>& SkelJointNames,
		float Time,
		TArray<int32>& OutJointIndices,
		TArray<float>& OutJointWeights,
		int32& OutElementSize,
		const TMap<FString, int32>* JointIndexByJointNameTable)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UsdToUnreal::NaniteAssemblyRootType);

		// The NaniteAssemblySkelBindingAPI requires one or two primvars to be authored on the prim:
		//
		//  primvars:unreal:naniteAssembly:bindJoints (token array) <- required
		//  primvars:unreal:naniteAssembly:bindJointWeights (float array) <- optional (assumed default of 1.0f)
		//
		// We're basically returning the attribute values of these two arrays; however, the names specified by the
		// bindJoints primvar must be converted into an array of indices relative to the names in the given
		// SkelJointNames array (the Nanite assembly API ultimately wants these indices, not names).
		//
		// We also return the indices primvar element size, which represents the number of bindings per instance. It
		// must be an exact multiple of the number of instances. For singular (non-pointinstancer) prims representing
		// a single Nanite instance/node, the element size is not really that important since the length of the
		// indices/weights arrays implicitly defines the number of elements. But for pointinstancers, the element size
		// is important and tells us how many bindings to associate with each point instance.

		if (!UsdUtils::PrimHasSchema(InPrim, UnrealIdentifiers::NaniteAssemblySkelBindingAPI))
		{
			return false;
		}

		OutJointIndices.Empty();
		OutJointWeights.Empty();
		OutElementSize = INDEX_NONE;

		pxr::VtArray<pxr::TfToken> BindJoints;
		pxr::VtArray<float> BindJointWeights;
		int32 ElementSize = INDEX_NONE;
		{
			FScopedUsdAllocs UsdAllocs;

			pxr::UsdPrim Prim{ InPrim };

			const pxr::UsdGeomPrimvarsAPI Primvars(InPrim);

			pxr::UsdGeomPointInstancer PointInstancer = pxr::UsdGeomPointInstancer(Prim);
			const size_t NumInstances = PointInstancer ? PointInstancer.GetInstanceCount(Time) : 1;

			// Retrieve the array of bind joints and weights on the prim. Weights are optional.

			pxr::UsdGeomPrimvar BindJointsPrimvar = Primvars.GetPrimvar(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints);
			if (!BindJointsPrimvar)
			{
				USD_LOG_WARNING(
					TEXT("Prim '%s' has schema '%s' applied, but attempt to get primvar '%s' failed.")
					, *UsdToUnreal::ConvertPath(Prim.GetPath())
					, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblySkelBindingAPI)
					, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
				);
				return false;
			}

			// Get the bind joints primvar element size.
			ElementSize = static_cast<int32>(BindJointsPrimvar.GetElementSize());
			if (ElementSize <= 0)
			{
				ElementSize = 1;

				USD_LOG_WARNING(
					TEXT("Ignoring invalid element size (%d) for primvar '%s.%s', defaulting to (1)")
					, ElementSize
					, *UsdToUnreal::ConvertPath(Prim.GetPath())
					, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
				);
			}

			//  Get the bind joints primvar value.
			BindJointsPrimvar.Get(&BindJoints, Time);
			if (BindJoints.empty())
			{
				if (NumInstances == 0)
				{
					// No instances, so no bindings are expected either.
					OutElementSize = ElementSize;
					return true;
				}
				else
				{
					USD_LOG_WARNING(
						TEXT("Prim '%s' with schema '%s' has empty primvar '%s' which does not match the number of instances (%d).")
						, *UsdToUnreal::ConvertPath(Prim.GetPath())
						, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblySkelBindingAPI)
						, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
						, NumInstances
					);
					return false;
				}
			}

			// Get bind joint weights, if authored.
			if (pxr::UsdGeomPrimvar BindJointWeightsPrimvar = Primvars.GetPrimvar(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJointWeights))
			{
				BindJointWeightsPrimvar.Get(&BindJointWeights, Time);
			}

			// Pointinstancer requires a (computed) mask to be applied to the joint names/weights in 
			// order to discard invisible and inactive points.
			if (PointInstancer)
			{
				// Verify the element size before applying the mask, since a correct element size is needed in order to apply
				// the mask correctly.
				//
				// If the number of bindings is greater than the instance count multiplied by the authored element size, it's
				// possible we are picking up the default primvar element size of 1 because the user forgot to author it. Rather
				// than fail, check if the available bindings are an exact multiple of the instance count, and, if so, use that
				// as the element size instead.
	
				const int32 NumBindings = BindJoints.size();
				if (NumInstances > 0 && ElementSize == 1 && NumBindings > NumInstances * ElementSize)
				{
					if (NumBindings % NumInstances == 0)
					{
						ElementSize = NumBindings / NumInstances;

						USD_LOG_WARNING(
							TEXT("Using inferred element size (%d) for Nanite assembly pointinstancer primvar '%s.%s'")
							, ElementSize
							, *UsdToUnreal::ConvertPath(Prim.GetPath())
							, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
						);
					}
					else
					{
						USD_LOG_WARNING(
							TEXT("Element size (%d) for Nanite assembly pointinstancer primvar '%s.%s' is invalid for the "
							     "number of bindings (%d) compared to the number of point instances (%d)")
							, ElementSize
							, *UsdToUnreal::ConvertPath(Prim.GetPath())
							, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
							, NumBindings
							, NumInstances
						);
						return false;
					}
				}

				// Remove masked joints and weights. Note that attempting to mask an incorrectly sized array will fail because
				// the input data must be the correct element-size multiple of the mask size. If we do fail, we must return
				// false here because it is likely the binding array will be a different/incorrect length relative to the
				// transform and indices arrays anyway.
		
				std::vector<bool> Mask = PointInstancer.ComputeMaskAtTime(Time);
				if (!ApplyMaskToArray(Mask, &BindJoints, ElementSize))
				{
					USD_LOG_WARNING(
						TEXT("Failed to apply mask data (invisibleIds, inactiveIds) to Nanite Assembly PointInstancer primvar '%s.%s'")
						, *UsdToUnreal::ConvertPath(Prim.GetPath())
						, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
					);
					return false;
				}
				if (!BindJointWeights.empty())
				{
					if (!ApplyMaskToArray(Mask, &BindJointWeights, ElementSize))
					{
						USD_LOG_WARNING(
							TEXT("Failed to apply mask data (invisibleIds, inactiveIds) to Nanite Assembly PointInstancer primvar '%s.%s'")
							, *UsdToUnreal::ConvertPath(Prim.GetPath())
							, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
						);
						return false;
					}
				}
			}
			else // regular non-pointinstancer prim
			{
				if (ElementSize != BindJoints.size())
				{
					// quietly fix the element size since for the non-pointinstancer case it must be the size of the bind joints array.
					ElementSize = static_cast<int32>(BindJoints.size());
				}
			}
		}

		// Map to accelerate finding joint index by name
		TMap<FString, int32>* JointIndexTable;
		TMap<FString, int32> ComputedJointIndexTable;

		if (JointIndexByJointNameTable) // user provided lookup table
		{
			JointIndexTable = const_cast<TMap<FString, int32>*>(JointIndexByJointNameTable);
		}
		else // No table provided so generate one
		{	
			JointIndexTable = &ComputedJointIndexTable;
			for (int32 JointIndex = 0; JointIndex < SkelJointNames.Num(); ++JointIndex)
			{
				ComputedJointIndexTable.Add(SkelJointNames[JointIndex], JointIndex);
			}
		}

		TSet<FString> InvalidJointNames;

		// Populate the outputs.

		OutElementSize = ElementSize;
		OutJointIndices.Reserve(BindJoints.size());
		OutJointWeights.Reserve(BindJoints.size());

		for (size_t BindJointIndex = 0; BindJointIndex < BindJoints.size(); ++BindJointIndex)
		{
			const FString BindJoint = UsdToUnreal::ConvertToken(BindJoints[BindJointIndex]);

			// We will default the binding index to the first joint in the skeleton if we can't find the joint name in the
			// primvar. Note that we can't filter or skip these invalid entries because doing so would cause the resulting
			// binding array to be a different size than the transforms and indices arrays for the pointinstancer case.
			constexpr int32 FirstJointIndex = 0;
			int32 JointIndex = FirstJointIndex;

			if (const int32* JointIndexFromSkeleton = JointIndexTable->Find(BindJoint))
			{
				JointIndex = *JointIndexFromSkeleton;
			}
			else
			{
				InvalidJointNames.Add(BindJoint);
			}

			const float JointWeight = BindJointIndex < BindJointWeights.size() ? BindJointWeights[BindJointIndex] : 1.0f;

			OutJointIndices.Add(JointIndex);
			OutJointWeights.Add(JointWeight);
		}

		// Lastly, report missing joints

		if(!InvalidJointNames.IsEmpty())
		{
			USD_LOG_WARNING(
				TEXT("Failed to find (%d) joint names from parent skeleton '%s' while processing Nanite assembly skel binding on prim '%s'"
					 " - the corresponding assembly part will instead be (incorrectly) parented to the first bone in the skeleton.")
				, InvalidJointNames.Num()
				, *SkelIdentifier
				, *InPrim.GetPrimPath().GetString()
			);
			USD_LOG_WARNING(TEXT("The invalid joint names were:"));
			for (const FString& JointName : InvalidJointNames)
			{
				USD_LOG_WARNING(TEXT("... %s"), *JointName);
			}
		}

		return true;
	}

	ENaniteAssemblyMeshType GetNaniteAssemblyMeshType(const UE::FUsdPrim& NaniteAssemblyRootPrim)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetNaniteAssemblyMeshType);

		// The NaniteAssemblyRootAPI schema requires attribute unreal:naniteAssembly:meshType, authored 
		// to "staticMesh" or "skeletalMesh".

		FScopedUsdAllocs Allocs;

		pxr::UsdPrim Prim{ NaniteAssemblyRootPrim };

		if (!UsdUtils::PrimHasSchema(Prim, UnrealIdentifiers::NaniteAssemblyRootAPI))
		{
			return ENaniteAssemblyMeshType::None;
		}

		pxr::UsdAttribute MeshTypeAttr = Prim.GetAttribute(UnrealIdentifiers::UnrealNaniteAssemblyMeshType);
		if (!MeshTypeAttr)
		{
			USD_LOG_WARNING(
				TEXT("Schema '%s' is applied to prim '%s' but required attribute '%s' is missing or invalid.")
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyRootAPI)
				, *UsdToUnreal::ConvertPath(Prim.GetPath())
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::UnrealNaniteAssemblyMeshType)
			);
			return ENaniteAssemblyMeshType::None;;
		}

		pxr::TfToken MeshType;
		if (!GetTokenOrStringAttributeValue(MeshTypeAttr, MeshType))
		{
			USD_LOG_WARNING(
				TEXT("Schema '%s' is applied to prim '%s' but required attribute '%s' is not a valid type (expected token or string).")
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyRootAPI)
				, *UsdToUnreal::ConvertPath(Prim.GetPath())
				, *UsdToUnreal::ConvertToken(MeshTypeAttr.GetName())
			);
			return ENaniteAssemblyMeshType::None;
		}

		if (UnrealIdentifiers::NaniteAssemblyStaticMesh == MeshType)
		{
			return ENaniteAssemblyMeshType::StaticMesh;
		}

		if (UnrealIdentifiers::NaniteAssemblySkeletalMesh == MeshType)
		{
			return ENaniteAssemblyMeshType::SkeletalMesh;
		}

		USD_LOG_WARNING(
			TEXT("Schema '%s' is applied to prim '%s' but attribute '%s' has unsupported value '%s'. Authored value must be one of '%s' or '%s'.")
			, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyRootAPI)
			, *UsdToUnreal::ConvertPath(Prim.GetPath())
			, *UsdToUnreal::ConvertToken(MeshTypeAttr.GetName())
			, *UsdToUnreal::ConvertToken(MeshType)
			, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyStaticMesh)
			, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblySkeletalMesh)
		);

		return ENaniteAssemblyMeshType::None;
	}

	bool GetExternalAssetReference(const UE::FUsdPrim& InPrim, FString& OutAssetReference)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetExternalAssetReference);

		// The NaniteAssemblyExternalRefAPI schema allows the user to utilize an explicit unreal asset, via attribute
		// 'unreal:naniteAssembly:meshAssetPath'.

		FScopedUsdAllocs Allocs;

		pxr::UsdPrim Prim{ InPrim };

		if (!UsdUtils::PrimHasSchema(Prim, UnrealIdentifiers::NaniteAssemblyExternalRefAPI))
		{
			return false;
		}

		// Default output value is an empty string. Note: from here on we always return true, even if there's an issue
		// obtaining the mesh asset path value. This is so the caller knows to halt traversal, since the user presumably
		// intends to substitute the USD scene geometry with this external reference, irrespective of whether there is
		// an authoring error in the schema's attributes.
		OutAssetReference = {};

		pxr::UsdAttribute MeshAssetPathAttr = Prim.GetAttribute(UnrealIdentifiers::UnrealNaniteAssemblyMeshAssetPath);
		if (!MeshAssetPathAttr)
		{
			USD_LOG_WARNING(
				TEXT("Schema '%s' is applied to prim '%s' but required attribute '%s' is missing or invalid.")
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyExternalRefAPI)
				, *UsdToUnreal::ConvertPath(Prim.GetPath())
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::UnrealNaniteAssemblyMeshAssetPath)
			);
			return true;
		}

		pxr::TfToken MeshAssetPath;
		if (!GetTokenOrStringAttributeValue(MeshAssetPathAttr, MeshAssetPath))
		{
			USD_LOG_WARNING(
				TEXT("Schema '%s' is applied to prim '%s' but required attribute '%s' is not a valid type (expected token or string).")
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyExternalRefAPI)
				, *UsdToUnreal::ConvertPath(Prim.GetPath())
				, *UsdToUnreal::ConvertToken(MeshAssetPathAttr.GetName())
			);
			return true;
		}

		OutAssetReference = UsdToUnreal::ConvertToken(MeshAssetPath);

		if (OutAssetReference.IsEmpty())
		{
			USD_LOG_WARNING(
				TEXT("Schema '%s' is applied to prim '%s' but attribute value for '%s' is empty.")
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyExternalRefAPI)
				, *UsdToUnreal::ConvertPath(Prim.GetPath())
				, *UsdToUnreal::ConvertToken(MeshAssetPathAttr.GetName())
			);
		}
		return true;
	}

	UE::FUsdPrim GetBaseSkeletonPrimForSkeletalMeshAssembly(const UE::FUsdPrim& NaniteAssemblyRootPrim)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetBaseSkeletonForSkeletalMeshAssembly);

		// When the NaniteAssemblyRootAPI meshType is "skeletalMesh", it requires the relationship
		// "unreal:naniteAssembly:skeleton" to point at a valid UsdSkelSkeleton prim (the UsdGeomMesh meshes bound to
		// this skeleton implicitly become the 'base' meshes for the skeletal mesh Nanite assembly).

		UE::FUsdRelationship SkeletonRel = NaniteAssemblyRootPrim.GetRelationship(*UsdToUnreal::ConvertToken(UnrealIdentifiers::UnrealNaniteAssemblySkeleton));
		if (!SkeletonRel)
		{
			USD_LOG_WARNING(
				TEXT("%s prim '%s' is missing required relationship '%s'")
				, * UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyRootAPI)
				, *NaniteAssemblyRootPrim.GetPrimPath().GetString()
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::UnrealNaniteAssemblySkeleton)
			);
			return {};
		}

		TArray<UE::FSdfPath> Targets;
		if (!SkeletonRel.GetTargets(Targets) || Targets.IsEmpty())
		{
			USD_LOG_WARNING(
				TEXT("%s prim '%s' does not specify a target skeleton via relationshp '%s'")
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyRootAPI)
				, *NaniteAssemblyRootPrim.GetPrimPath().GetString()
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::UnrealNaniteAssemblySkeleton)
			);
			return {};
		}

		const UE::FSdfPath& SkeletonPath = Targets[0];
		UE::FUsdPrim SkeletonPrim = NaniteAssemblyRootPrim.GetStage().GetPrimAtPath(SkeletonPath);
		if (!SkeletonPrim || !SkeletonPrim.IsA(TEXT("Skeleton")))
		{
			USD_LOG_WARNING(
				TEXT("Ignoring invalid or missing skeleton prim '%s' specified by %s prim '%s'")
				, *SkeletonPath.GetString()
				, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblyRootAPI)
				, *NaniteAssemblyRootPrim.GetPrimPath().GetString()
			);
			return {};
		}

		return SkeletonPrim;
	}

	bool GetPointInstancerProtoIndices(const UE::FUsdPrim& PointInstancerPrim, float Time, TArray<int32>& OutProtoIndices)
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdGeomPointInstancer PointInstancer(PointInstancerPrim);
		if (!PointInstancer)
		{
			return false;
		}

		if (const pxr::UsdAttribute ProtoIndicesAttr = PointInstancer.GetProtoIndicesAttr())
		{
			pxr::VtArray<int> ProtoIndicesArr;
			if (ProtoIndicesAttr.Get(&ProtoIndicesArr, Time))
			{
				std::vector<bool> Mask = PointInstancer.ComputeMaskAtTime(Time);
				if (!ApplyMaskToArray(Mask, &ProtoIndicesArr))
				{
					USD_LOG_WARNING(
						TEXT("Failed to apply mask data (invisibleIds, inactiveIds) to attribute protoIndices for Nanite assembly pointinstancer '%s'.")
						, *UsdToUnreal::ConvertPath(PointInstancerPrim.GetPrimPath())
						, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
					);
					return false;
				}

				if (ProtoIndicesArr.empty())
				{
					return false;
				}

				FScopedUnrealAllocs UnrealAllocs;

				TArray<int32> ProtoIndices;
				OutProtoIndices.Empty();
				OutProtoIndices.Reserve(static_cast<int32>(ProtoIndicesArr.size()));
				for (size_t Index = 0; Index < ProtoIndicesArr.size(); ++Index)
				{
					OutProtoIndices.Add(static_cast<int32>(ProtoIndicesArr[Index]));
				}
				return true;
			}
		}

		USD_LOG_WARNING(
			TEXT("Failed to get pointinstancer protoIndices for prim '%s'")
			, *PointInstancerPrim.GetPrimPath().GetString()
		);
		return false;
	}

    bool GetPointInstancerTransforms(const UE::FUsdPrim& PointInstancerPrim, const FTransform& ParentTransform, float Time, TArray<FTransform>& OutTransforms)
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdGeomPointInstancer PointInstancer(PointInstancerPrim);
		if (!PointInstancer)
		{
			return false;
		}

		pxr::VtMatrix4dArray UsdInstanceTransforms;

		// We don't want the prototype root prims' transforms to be included here, as they'll already be baked into the
		// meshes themselves. Note also that the default for ComputeInstanceTransformsAtTime is ApplyMask, which is what we
		// want.
		if (!PointInstancer.ComputeInstanceTransformsAtTime(&UsdInstanceTransforms, Time, Time, pxr::UsdGeomPointInstancer::ExcludeProtoXform))
		{
			USD_LOG_WARNING(
				TEXT("Failed to compute transforms for pointinstancer prim '%s' at time (%f)")
				, *PointInstancerPrim.GetPrimPath().GetString()
				, Time
			);
			return false;
		}

		if (UsdInstanceTransforms.empty())
		{
			return false;
		}

		{
			FScopedUnrealAllocs UnrealAllocs;

			FUsdStageInfo StageInfo{ PointInstancerPrim.GetStage() };
			TArray<FTransform> InstanceTransforms;
			OutTransforms.Empty();
			OutTransforms.Reserve(static_cast<int32>(UsdInstanceTransforms.size()));
			for (const pxr::GfMatrix4d& UsdMatrix : UsdInstanceTransforms)
			{
				OutTransforms.Add(UsdToUnreal::ConvertMatrix(StageInfo, UsdMatrix) * ParentTransform);
			}
			return true;
		}
	}

	TArray<UE::FSdfPath> GetPointInstancerPrototypePaths(const UE::FUsdPrim& PointInstancerPrim)
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdGeomPointInstancer PointInstancer(PointInstancerPrim);
		if (!PointInstancer)
		{
			return {};
		}

		const pxr::UsdRelationship& Prototypes = PointInstancer.GetPrototypesRel();

		pxr::SdfPathVector UsdPrototypePaths;
		Prototypes.GetTargets(&UsdPrototypePaths);
		{
			FScopedUnrealAllocs UnrealAllocs;

			TArray<UE::FSdfPath> PrototypePaths;
			PrototypePaths.Reserve(static_cast<int32>(UsdPrototypePaths.size()));
			for (const pxr::SdfPath& UsdPath : UsdPrototypePaths)
			{
				PrototypePaths.Add(UE::FSdfPath{ UsdPath });
			}
			return PrototypePaths;
		}
	}

	FNaniteAssemblyPointInstancerData GetPointInstancerData(
		const UE::FUsdPrim& PointInstancerPrim,
		const UsdToUnreal::FUsdMeshConversionOptions& Options,
		const FNaniteAssemblyTraversalResult& TraversalResult,
		const FString& SkelIdentifier,
		const TArray<FString>& JointNames,
		int32 PartIndexStartOffset)
	{
		if (!ensure(PointInstancerPrim && PointInstancerPrim.IsA(TEXT("PointInstancer"))))
		{
			return {};
		}

		const float Time = static_cast<float>(Options.TimeCode.GetValue());

		// Get masked prototype indices and instance transforms for the given time.
		TArray<int32> ProtoIndices;
		const bool bHasProtoIndices = GetPointInstancerProtoIndices(PointInstancerPrim, Time, ProtoIndices);

		TArray<FTransform> Transforms;
		const bool bHasTransforms = GetPointInstancerTransforms(PointInstancerPrim, Options.AdditionalTransform, Time, Transforms);

		if (!(bHasProtoIndices && bHasTransforms))
		{
			return {};
		}
		else if (ProtoIndices.Num() != Transforms.Num())
		{
			UE_LOG(LogUsd, Warning
				, TEXT("Invalid Nanite assembly pointinstancer '%s' with different transforms and "
					   "prototype indices array lengths (%d vs %d)")
				, *PointInstancerPrim.GetPrimPath().GetString()
				, ProtoIndices.Num()
				, Transforms.Num()
			);
			return {};
		}

		FPrototypesToPartsRemappingInfo ProtoRemappingInfo;
		if (!TraversalResult.GetRemappedPartsForTopLevelPointInstancer(PointInstancerPrim, PartIndexStartOffset, ProtoRemappingInfo))
		{
			return {};
		}

		// Check the local array of available prototypes. Any indices that targeted non-existent prototypes we will 
		// invalidate explicitly, so as to ensure they don't just happen to index into some other unrelated, yet 
		// completely valid part index once subsequent descriptions have been added.
		// 
		int32 NumInvalidPrototypeIndices = 0;
		for (int32& Index : ProtoIndices)
		{
			if (Index >= 0 && Index <= ProtoRemappingInfo.OriginalPrototypeCount)
			{
				Index += PartIndexStartOffset;
			}
			else
			{
				Index = INDEX_NONE;
				NumInvalidPrototypeIndices++;
			}
		}

		if (NumInvalidPrototypeIndices > 0)
		{
			// all points invalid so exit
			if (NumInvalidPrototypeIndices == ProtoIndices.Num())
			{
				UE_LOG(LogUsd, Warning
					, TEXT("Ignoring Nanite assembly pointinstancer '%s' - all (%d) instance points referred to non-existant prototype paths.")
					, *PointInstancerPrim.GetPrimPath().GetString()
					, NumInvalidPrototypeIndices
				);
				return {};
			}
			// some points invalid so keep going
			else
			{
				UE_LOG(LogUsd, Warning
					, TEXT("(%d) point(s) in Nanite assembly pointinstancer '%s' refer to one or more invalid prototypes - some instances will be missing.")
					, NumInvalidPrototypeIndices
					, *PointInstancerPrim.GetPrimPath().GetString()
				);
			}
		}

		// If this is a skeletal mesh assembly (i.e we have joint names), the pointinstancer must have a primvar that
		// binds each pointinstancer point to one or more bones in the supplied joint names array.
		// Note also - for the static mesh assembly case it is expected for these arrays to be present but empty.

		TArray<int32> JointIndices;
		TArray<float> JointWeights;
		TArray<int32> NumInfluencesPerInstance;

		if (!JointNames.IsEmpty())
		{
			// Get the masked joint indices and weights primvar data.

			int32 ElementSize;
			bool bHasBindingData = UsdToUnreal::NaniteAssemblyUtils::GetJointBindingData(PointInstancerPrim,
				SkelIdentifier, JointNames, Time, JointIndices, JointWeights, ElementSize);

			if (ensure(
				bHasBindingData
				&& ElementSize > 0
				&& JointIndices.Num() > 0
				&& JointIndices.Num() == JointWeights.Num()
				&& JointIndices.Num() / ElementSize == ProtoIndices.Num()))
			{
				NumInfluencesPerInstance.Init(ElementSize, ProtoIndices.Num());
			}
			else
			{
				// Invalid binding data, can't continue.
				return {};
			}
		}

		constexpr bool bIsValid = true;
		return { MoveTemp(ProtoIndices),
				 MoveTemp(Transforms),
				 MoveTemp(ProtoRemappingInfo),
				 MoveTemp(JointIndices),
				 MoveTemp(JointWeights),
				 MoveTemp(NumInfluencesPerInstance),
				 bIsValid };

	}
} // namespace UsdToUnreal

#endif // #if USE_USD_SDK