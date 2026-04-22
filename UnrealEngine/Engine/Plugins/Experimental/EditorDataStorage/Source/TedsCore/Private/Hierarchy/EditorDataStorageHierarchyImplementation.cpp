// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDataStorageHierarchyImplementation.h"

#include "EditorDataStorageHierarchyColumns.h"

namespace UE::Editor::DataStorage
{
	// Interpretation for the opaque FHierarchyHandle
	struct FHierarchyHandleInterpretation
	{
		int32 Index;
		bool IsSet;
	};

	static int32 GetHandleIndex(const FHierarchyHandle& Handle)
	{
		const FHierarchyHandleInterpretation& Interp = reinterpret_cast<const FHierarchyHandleInterpretation&>(Handle);
		return Interp.IsSet ? Interp.Index : INDEX_NONE;
	}

	static FHierarchyHandle CreateHandle(int32 Index)
	{
		FHierarchyHandle Handle;
		
		FHierarchyHandleInterpretation& Interp = reinterpret_cast<FHierarchyHandleInterpretation&>(Handle);
		new (&Interp) FHierarchyHandleInterpretation();
		Interp.Index = Index;
		Interp.IsSet = true;
		
		return Handle;
	}

	static_assert(sizeof(FHierarchyHandleInterpretation) <= sizeof(FHierarchyHandle));
	static_assert(alignof(FHierarchyHandleInterpretation) <= alignof(FHierarchyHandle));
	static_assert(std::is_trivially_copyable_v<FHierarchyHandle>);
	static_assert(std::is_trivially_copyable_v<FHierarchyHandleInterpretation>);
	
	const FTedsHierarchyAccessInterface* FTedsHierarchyRegistrar::GetAccessInterface(FHierarchyHandle Handle) const
	{
		if (int32 Index = GetHandleIndex(Handle); Index != INDEX_NONE)
		{
			return RegisteredHierarchies[Index].AccessInterface.Get();
		}
		return nullptr;
	}

	void FTedsHierarchyRegistrar::ListHierarchyNames(TFunctionRef<void(const FName& HierarchyName)> Callback) const
	{
		for (int32 HierarchyIndex = 0; HierarchyIndex < RegisteredHierarchies.Num(); ++HierarchyIndex)
		{
			Callback(RegisteredHierarchies[HierarchyIndex].Name);
		}
	}

	FHierarchyHandle FTedsHierarchyRegistrar::RegisterHierarchy(ICoreProvider* InProvider, const FHierarchyRegistrationParams& RegistrationParams)
	{
		// Create an access interface
		
		// Generate the tags and HierarchyData column per Hierarchy type
		const UScriptStruct* HierarchyData = [InProvider, &RegistrationParams]()
		{
			FDynamicColumnDescription Desc
			{
				.TemplateType = FEditorDataHierarchyData_Template::StaticStruct(),
				.Identifier = RegistrationParams.Name
			};
			return InProvider->GenerateDynamicColumn(Desc);
		}();
		
		const UScriptStruct* ParentTag = [InProvider, &RegistrationParams]()
		{
			FDynamicColumnDescription Desc
			{
				.TemplateType = FEditorDataHierarchyParentTag_Template::StaticStruct(),
				.Identifier = RegistrationParams.Name
			};
			return InProvider->GenerateDynamicColumn(Desc);
		}();
		
		const UScriptStruct* ChildTag = [InProvider, &RegistrationParams]()
		{
			FDynamicColumnDescription Desc
			{
				.TemplateType = FEditorDataHierarchyChildTag_Template::StaticStruct(),
				.Identifier = RegistrationParams.Name
			};
			return InProvider->GenerateDynamicColumn(Desc);
		}();

		const UScriptStruct* UnresolvedParentColumn = [InProvider, &RegistrationParams]()
		{
			FDynamicColumnDescription Desc
			{
				.TemplateType = FEditorDataHierarchyUnresolvedParent_Template::StaticStruct(),
				.Identifier = RegistrationParams.Name
			};
			return InProvider->GenerateDynamicColumn(Desc);
		}();
		
		TUniquePtr<FTedsHierarchyAccessInterface> AccessInterface = [&]()
		{
			FTedsHierarchyAccessInterface::FConstructParams Params
			{
				.ChildTag = ChildTag,
				.ParentTag = ParentTag,
				.HierarchyData = HierarchyData,
				.UnresolvedParentColumn = UnresolvedParentColumn,
				.ParentChangedColumn = RegistrationParams.ParentChangedColumn
			};
			return MakeUnique<FTedsHierarchyAccessInterface>(Params);
		}();
		
		FRegisteredHierarchy RegisteredHierarchy
		{
			.Name = RegistrationParams.Name,
			.ChildTag = ChildTag,
			.ParentTag = ParentTag,
			.HierarchyData = HierarchyData,
			.UnresolvedParentColumn = UnresolvedParentColumn,
			.ParentChangedColumn = RegistrationParams.ParentChangedColumn,
			.AccessInterface = MoveTemp(AccessInterface)
		};

		int32 Index = RegisteredHierarchies.Num();
		RegisteredHierarchies.Emplace(MoveTemp(RegisteredHierarchy));

		FHierarchyHandle HierarchyHandle = CreateHandle(Index);
		
		ICoreProvider* DataStorage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		RegisterObservers(*DataStorage, HierarchyHandle);
		RegisterProcessors(*DataStorage, HierarchyHandle);
		
		return HierarchyHandle;
	}

	FHierarchyHandle FTedsHierarchyRegistrar::FindHierarchyByName(const FName& Name) const
	{
		for (int32 HierarchyIndex = 0; HierarchyIndex < RegisteredHierarchies.Num(); ++HierarchyIndex)
		{
			if (RegisteredHierarchies[HierarchyIndex].Name == Name)
			{
				return CreateHandle(HierarchyIndex);
			}
		}
		return FHierarchyHandle();
	}

	auto Hack_FindRowIndexInChunk = [](const ICommonQueryContext& Context, RowHandle Row) -> int32
	{
		int32 RowIndex = INDEX_NONE;
		TConstArrayView<RowHandle> RowHandes = Context.GetRowHandles();
		const int32 RowCount = RowHandes.Num();
		for (int32 CandidateRowIndex = 0; CandidateRowIndex < RowCount; ++CandidateRowIndex)
		{
			if (RowHandes[CandidateRowIndex] == Row)
			{
				RowIndex = CandidateRowIndex;
				break;
			}
		}
		return RowIndex;
	};

	void FTedsHierarchyRegistrar::RegisterObservers(ICoreProvider& DataStorage, const FHierarchyHandle& Handle)
	{
		using namespace UE::Editor::DataStorage::Queries;

		const FRegisteredHierarchy* Hierarchy = FindRegisteredHierarchy(Handle);
		checkf(Hierarchy, TEXT("Couldn't find hierarchy that was just registered, observers for the hierarchy could not be registered successfully"));

		QueryHandle Subquery = DataStorage.RegisterQuery(
				Select()
					.ReadWrite(Hierarchy->HierarchyData)
				.Compile());

		DataStorage.RegisterQuery(
		Select(
			TEXT("Remove child from parent's child array"),
			FObserver(FObserver::EEvent::Remove, Hierarchy->ChildTag),
			[this, Handle](IQueryContext& Context, RowHandle Row)
			{
				const FRegisteredHierarchy* Hierarchy = FindRegisteredHierarchy(Handle);
				
				FEditorDataHierarchyData_Template* ChildHierarchyDataComponent;
				{
					const int RowIndexInChunk = Hack_FindRowIndexInChunk(Context, Row);
					check(RowIndexInChunk != INDEX_NONE);
					void* HierarchyDataComponentArrayStart = Context.GetMutableColumn(Hierarchy->HierarchyData);
					uint8* HierarchyDataComponentObjectAddress = static_cast<uint8*>(HierarchyDataComponentArrayStart) + (RowIndexInChunk * Hierarchy->HierarchyData->GetStructureSize());
					ChildHierarchyDataComponent = reinterpret_cast<FEditorDataHierarchyData_Template*>(HierarchyDataComponentObjectAddress);
				}
				if (ChildHierarchyDataComponent->Parent == InvalidRowHandle)
				{
					return;
				}
				// Remove the ParentTag from the parent row if it has no more children
				bool bRemoveParentTag = false;
				Context.RunSubquery(0, ChildHierarchyDataComponent->Parent, CreateSubqueryCallbackBinding([Row, &bRemoveParentTag, Hierarchy](ISubqueryContext& SubQueryContext, RowHandle ParentRow)
				{
					FEditorDataHierarchyData_Template* ParentHierarchyDataComponent;
					{
						const int RowIndexInChunk = Hack_FindRowIndexInChunk(SubQueryContext, ParentRow);
						check(RowIndexInChunk != INDEX_NONE);
						void* HierarchyDataComponentArrayStart = SubQueryContext.GetMutableColumn(Hierarchy->HierarchyData);
						uint8* HierarchyDataComponentObjectAddress = static_cast<uint8*>(HierarchyDataComponentArrayStart) + (RowIndexInChunk * Hierarchy->HierarchyData->GetStructureSize());
						ParentHierarchyDataComponent = reinterpret_cast<FEditorDataHierarchyData_Template*>(HierarchyDataComponentObjectAddress);
					}
					
					ParentHierarchyDataComponent->Children.RemoveSwap(FTedsRowHandle(Row));
					if (ParentHierarchyDataComponent->Children.IsEmpty())
					{
						bRemoveParentTag = true;
					}
				}));
				if (bRemoveParentTag)
				{
					Context.RemoveColumns(ChildHierarchyDataComponent->Parent, {Hierarchy->ParentTag});
				}
				
				ChildHierarchyDataComponent->Parent = InvalidRowHandle;
			}
		)
		.ReadWrite(Hierarchy->HierarchyData)
		.DependsOn().SubQuery(Subquery)
		.Compile());

		DataStorage.RegisterQuery(
		Select(
			TEXT("Remove parent reference from children of destroyed parent"),
			FObserver(FObserver::EEvent::Remove, Hierarchy->ParentTag),
			[this, Handle](IQueryContext& Context, RowHandle ParentRow)
			{
				const FRegisteredHierarchy* Hierarchy = FindRegisteredHierarchy(Handle);
				
				FEditorDataHierarchyData_Template* ParentHierarchyDataComponent;
				{
					const int RowIndexInChunk = Hack_FindRowIndexInChunk(Context, ParentRow);
					check(RowIndexInChunk != INDEX_NONE);
					void* HierarchyDataComponentArrayStart = Context.GetMutableColumn(Hierarchy->HierarchyData);
					uint8* HierarchyDataComponentObjectAddress = static_cast<uint8*>(HierarchyDataComponentArrayStart) + (RowIndexInChunk * Hierarchy->HierarchyData->GetStructureSize());
					ParentHierarchyDataComponent = reinterpret_cast<FEditorDataHierarchyData_Template*>(HierarchyDataComponentObjectAddress);
				}
				if (ParentHierarchyDataComponent->Children.IsEmpty())
				{
					return;
				}
				for (int32 ChildIndex = 0; ChildIndex < ParentHierarchyDataComponent->Children.Num(); ++ChildIndex)
				{
					RowHandle ChildRow = ParentHierarchyDataComponent->Children[ChildIndex];
					Context.RunSubquery(0, ChildRow, CreateSubqueryCallbackBinding([ParentRow, Hierarchy](ISubqueryContext& SubQueryContext, RowHandle ChildRow)
					{
						FEditorDataHierarchyData_Template* ChildHierarchyDataComponent;
						{
							const int RowIndexInChunk = Hack_FindRowIndexInChunk(SubQueryContext, ChildRow);
							check(RowIndexInChunk != INDEX_NONE);
							void* HierarchyDataComponentArrayStart = SubQueryContext.GetMutableColumn(Hierarchy->HierarchyData);
							uint8* HierarchyDataComponentObjectAddress = static_cast<uint8*>(HierarchyDataComponentArrayStart) + (RowIndexInChunk * Hierarchy->HierarchyData->GetStructureSize());
							ChildHierarchyDataComponent = reinterpret_cast<FEditorDataHierarchyData_Template*>(HierarchyDataComponentObjectAddress);
						}
						ChildHierarchyDataComponent->Parent = InvalidRowHandle;
					}));

					Context.RemoveColumns(ChildRow, {Hierarchy->ChildTag});
				}

				ParentHierarchyDataComponent->Children.Reset();
			}
		)
		.ReadWrite(Hierarchy->HierarchyData)
		.DependsOn().SubQuery(Subquery)
		.Compile());

		if (Hierarchy->ParentChangedColumn)
		{
			DataStorage.RegisterQuery(
				Select(
					TEXT("Remove Parent Changed Tag at the end of the frame"),
					FPhaseAmble(FPhaseAmble::ELocation::Postamble, EQueryTickPhase::FrameEnd),
					[this, Handle](IQueryContext& Context, const RowHandle* Rows)
					{
						if (const FRegisteredHierarchy* Hierarchy = FindRegisteredHierarchy(Handle))
						{
							Context.RemoveColumns(TConstArrayView<RowHandle>(Rows, Context.GetRowCount()), {Hierarchy->ParentChangedColumn});
						}
					}
				)
				.Where()
					.All(Hierarchy->ParentChangedColumn)
				.Compile());
		}
	}

	void FTedsHierarchyRegistrar::RegisterProcessors(ICoreProvider& CoreProvider, const FHierarchyHandle& Handle)
	{
		using namespace UE::Editor::DataStorage::Queries;

		const FRegisteredHierarchy* Hierarchy = FindRegisteredHierarchy(Handle);
		checkf(Hierarchy, TEXT("Couldn't find hierarchy that was just registered, processors for the hierarchy could not be registered successfully"));
		
		CoreProvider.RegisterQuery(
		Select(
			TEXT("Resolve hierarchy rows"),
			FProcessor(EQueryTickPhase::PrePhysics, CoreProvider.GetQueryTickGroupName(EQueryTickGroups::Default)),
			[this, Handle](IQueryContext& Context, RowHandle Row)
			{
				const FRegisteredHierarchy* Hierarchy = FindRegisteredHierarchy(Handle);
				
				FEditorDataHierarchyUnresolvedParent_Template* UnresolvedParentColumn;
				{
					const int RowIndexInChunk = Hack_FindRowIndexInChunk(Context, Row);
					check(RowIndexInChunk != INDEX_NONE);
					void* HierarchyDataComponentArrayStart = Context.GetMutableColumn(Hierarchy->UnresolvedParentColumn);
					uint8* HierarchyDataComponentObjectAddress = static_cast<uint8*>(HierarchyDataComponentArrayStart) + (RowIndexInChunk * Hierarchy->UnresolvedParentColumn->GetStructureSize());
					UnresolvedParentColumn = reinterpret_cast<FEditorDataHierarchyUnresolvedParent_Template*>(HierarchyDataComponentObjectAddress);
				}
				
				RowHandle ParentRow = Context.LookupMappedRow(UnresolvedParentColumn->MappingDomain, UnresolvedParentColumn->ParentId);
				if (Context.IsRowAvailable(ParentRow))
				{
					Context.SetParentRow(Row, ParentRow);
					Context.RemoveColumns(Row, {Hierarchy->UnresolvedParentColumn});
				}
			})
		.AccessesHierarchy(Hierarchy->Name)
		.ReadWrite(Hierarchy->UnresolvedParentColumn)
		.Compile());
	}

	const FTedsHierarchyRegistrar::FRegisteredHierarchy* FTedsHierarchyRegistrar::FindRegisteredHierarchy(const FHierarchyHandle& Handle)
	{
		int32 Index = GetHandleIndex(Handle);
		if (Index != INDEX_NONE)
		{
			return &RegisteredHierarchies[Index];
		}
		return nullptr;
	}

	FTedsHierarchyAccessInterface::FTedsHierarchyAccessInterface(const FConstructParams& Params)
		: ChildTag(Params.ChildTag)
		, ParentTag(Params.ParentTag)
		, HierarchyDataColumnType(Params.HierarchyData)
		, UnresolvedParentColumnType(Params.UnresolvedParentColumn)
		, ParentChangedColumn(Params.ParentChangedColumn)
	{
		check(HierarchyDataColumnType->IsChildOf(FEditorDataHierarchyData_Template::StaticStruct()));
	}

	const UScriptStruct* FTedsHierarchyAccessInterface::GetChildTagType() const
	{
		return ChildTag;
	}

	const UScriptStruct* FTedsHierarchyAccessInterface::GetParentTagType() const
	{
		return ParentTag;
	}

	const UScriptStruct* FTedsHierarchyAccessInterface::GetHierarchyDataColumnType() const
	{
		return HierarchyDataColumnType;
	}

	const UScriptStruct* FTedsHierarchyAccessInterface::GetUnresolvedParentColumnType() const
	{
		return UnresolvedParentColumnType;
	}

	bool FTedsHierarchyAccessInterface::HasChildren(const ICoreProvider& Context, RowHandle Row) const
	{
		const FEditorDataHierarchyData_Template* Hierarchy = static_cast<const FEditorDataHierarchyData_Template*>(Context.GetColumnData(Row, HierarchyDataColumnType));
		return Hierarchy && !Hierarchy->Children.IsEmpty();
	}

	void FTedsHierarchyAccessInterface::WalkDepthFirst(
		const ICoreProvider& Context,
		RowHandle Row,
		TFunction<void(const ICoreProvider& Context, RowHandle Owner,RowHandle Target)> OnVisitedFn) const
	{
		// Recursively walk depth first to visit each child
		auto WalkDepthFirstImpl = [this](const ICoreProvider& Context_, RowHandle Row, decltype(OnVisitedFn)& OnVisitedFn_, auto& WalkDepthFirstImplRef) -> void
		{
			const FEditorDataHierarchyData_Template* HierarchyData = static_cast<const FEditorDataHierarchyData_Template*>(Context_.GetColumnData(Row, HierarchyDataColumnType));
			if (HierarchyData)
			{
				for (RowHandle ChildRow : HierarchyData->Children)
				{					
					OnVisitedFn_(Context_, Row, ChildRow);
					
					WalkDepthFirstImplRef(Context_, ChildRow, OnVisitedFn_, WalkDepthFirstImplRef);
				}
			}
		};

		// Call the top level object
		OnVisitedFn(Context, InvalidRowHandle /*no parent*/, Row);

		WalkDepthFirstImpl(Context, Row, OnVisitedFn, WalkDepthFirstImpl);
	}

	void FTedsHierarchyAccessInterface::SetParentRow(ICoreProvider* CoreProvider, RowHandle Target, RowHandle Parent) const
	{
		CoreProvider->RemoveColumn(Target, UnresolvedParentColumnType);

		auto AddHierarchyDataColumn = [this](ICoreProvider* CoreProvider, RowHandle Row, RowHandle ParentRow)
		{
			CoreProvider->AddColumnData(
				Row,
				HierarchyDataColumnType,
				[ParentRow](void* Dest, const UScriptStruct& ColumnType)
				{
					ColumnType.InitializeStruct(Dest);
					FEditorDataHierarchyData_Template* HierarchyDataColumn = static_cast<FEditorDataHierarchyData_Template*>(Dest);
					check(HierarchyDataColumn->Parent == InvalidRowHandle);
					HierarchyDataColumn->Parent = ParentRow;
				},
				[](const UScriptStruct& ColumnType, void* Destination, void* Source)
				{
					// We could get away with defining this as the move operator of the base class as
					// it should be the same as whatever is in HierarchyDataColumnType
					// However, it is technically slicing.  
					new (Destination) FEditorDataHierarchyData_Template(MoveTemp(*static_cast<FEditorDataHierarchyData_Template*>(Source)));
				}
			);
		};

		FEditorDataHierarchyData_Template* HierarchyDataColumn_Parent;
		if (void* ParentHierarchyDataColumnAddress = CoreProvider->GetColumnData(Parent, HierarchyDataColumnType); ParentHierarchyDataColumnAddress == nullptr)
		{
			AddHierarchyDataColumn(CoreProvider, Parent, InvalidRowHandle);
			ParentHierarchyDataColumnAddress = CoreProvider->GetColumnData(Parent, HierarchyDataColumnType);
			check(ParentHierarchyDataColumnAddress);
			HierarchyDataColumn_Parent = static_cast<FEditorDataHierarchyData_Template*>(ParentHierarchyDataColumnAddress);
		}
		else
		{
			HierarchyDataColumn_Parent = static_cast<FEditorDataHierarchyData_Template*>(ParentHierarchyDataColumnAddress);
		}

		RowHandle PreviousParent = InvalidRowHandle;
		FEditorDataHierarchyData_Template* HierarchyDataColumn_Target;
		if (void* TargetHierarchyDataColumnAddress = CoreProvider->GetColumnData(Target, HierarchyDataColumnType); TargetHierarchyDataColumnAddress == nullptr)
		{
			AddHierarchyDataColumn(CoreProvider, Target, Parent);
			TargetHierarchyDataColumnAddress = CoreProvider->GetColumnData(Target, HierarchyDataColumnType);
			check(TargetHierarchyDataColumnAddress);
			HierarchyDataColumn_Target = static_cast<FEditorDataHierarchyData_Template*>(TargetHierarchyDataColumnAddress);
		}
		else
		{
			HierarchyDataColumn_Target = static_cast<FEditorDataHierarchyData_Template*>(TargetHierarchyDataColumnAddress);
			PreviousParent = HierarchyDataColumn_Target->Parent;
		}
		
		if (Parent != InvalidRowHandle)
		{
			// No updates needed if the parent is the same as before
			if (Parent != PreviousParent)
			{
				FEditorDataHierarchyData_Template* HierarchyDataColumn_PreviousParent = nullptr;
				if (void* PreviousParentHierarchyDataColumnAddress = CoreProvider->GetColumnData(PreviousParent, HierarchyDataColumnType))
				{
					HierarchyDataColumn_PreviousParent = static_cast<FEditorDataHierarchyData_Template*>(PreviousParentHierarchyDataColumnAddress);
				}
				
				// In the case the parent changed, we have to also update the previous parent right now since any observers will act on the new parent
				if (HierarchyDataColumn_PreviousParent)
				{
					HierarchyDataColumn_PreviousParent->Children.RemoveSwap(FTedsRowHandle(Target));

					if (HierarchyDataColumn_PreviousParent->Children.IsEmpty())
					{
						CoreProvider->RemoveColumn(PreviousParent, ParentTag);
					}
				}

				// Update the target with the new information
				HierarchyDataColumn_Target->Parent = Parent;
				CoreProvider->AddColumn(Target, ChildTag);

				// Update the new parent with the new information
				HierarchyDataColumn_Parent->Children.Add(FTedsRowHandle(Target));
				CoreProvider->AddColumn(Parent, ParentTag);
			}
		}
		else
		{
			CoreProvider->RemoveColumns(Target, {ChildTag});

			// When the child is removed from a parent (not including when the parent changes to another row), an observer handles all this
			// Commented out just to make it clear what should happen
			
			// HierarchyDataColumn_Target->Parent = InvalidRowHandle;
			// HierarchyDataColumn_Parent->Children.Remove(FTedsRowHandle(Target));
			// if (HierarchyDataColumn_Parent->Children.IsEmpty())
			// {
			// 	CoreProvider->RemoveColumns(Parent, {ParentTag});
			// }
		}

		if (ParentChangedColumn && Parent != PreviousParent)
		{
			CoreProvider->AddColumn(Target, ParentChangedColumn);
		}
	}

	void FTedsHierarchyAccessInterface::SetUnresolvedParent(ICoreProvider* CoreProvider, RowHandle Target, FMapKey ParentId, FName MappingDomain) const
	{
		const UScriptStruct* UnresolvedParentColumn = GetUnresolvedParentColumnType();
		
		CoreProvider->AddColumnData(
				Target,
				UnresolvedParentColumn,
				[ParentId, MappingDomain](void* Dest, const UScriptStruct& ColumnType)
				{
					ColumnType.InitializeStruct(Dest);
					FEditorDataHierarchyUnresolvedParent_Template* UnresolvedParentColumn = static_cast<FEditorDataHierarchyUnresolvedParent_Template*>(Dest);
					UnresolvedParentColumn->ParentId = ParentId;
					UnresolvedParentColumn->MappingDomain = MappingDomain;
				},
				[](const UScriptStruct& ColumnType, void* Destination, void* Source)
				{
					ColumnType.CopyScriptStruct(Destination, Source);
				}
			);
	}

	RowHandle FTedsHierarchyAccessInterface::GetParentRow(const ICoreProvider* CoreProvider, RowHandle Target) const
	{
		const UScriptStruct* HierarchyDataColumn = GetHierarchyDataColumnType();
		const void* ColumnData = CoreProvider->GetColumnData(Target, HierarchyDataColumn);
		if (ColumnData != nullptr)
		{
			const FEditorDataHierarchyData_Template* Column = static_cast<const FEditorDataHierarchyData_Template*>(ColumnData);
			return Column->Parent.RowHandle;
		}
		return InvalidRowHandle;
	}

	TFunction<RowHandle(const void*, const UScriptStruct*)> FTedsHierarchyAccessInterface::CreateParentExtractionFunction() const
	{
		return [this](const void* ColumnData, const UScriptStruct* ColumnType)->RowHandle
		{
			check(ColumnType == GetHierarchyDataColumnType());
			const FEditorDataHierarchyData_Template* HierarchyDataColumn = static_cast<const FEditorDataHierarchyData_Template*>(ColumnData);
			return HierarchyDataColumn->Parent.RowHandle;
		};
	}

	// Deferred command to set the parent of a target row
	// Setting a parent may result in the Target and Parent to change archetypes due to the addition of data and tags
	// to store the relationships if they are not already established
	struct FSetParentCommand
	{
		RowHandle Parent;
		RowHandle Target;
		const FTedsHierarchyAccessInterface* HierarchyAccessInterface = nullptr;
		ICoreProvider* CoreProvider = nullptr;

		void operator()()
		{
			if ((Parent == InvalidRowHandle || CoreProvider->IsRowAssigned(Parent)) && CoreProvider->IsRowAssigned(Target))
			{
				HierarchyAccessInterface->SetParentRow(CoreProvider, Target, Parent);
			}
		}
	};

	// Deferred command to set the unresolved parent of a target row
	struct FSetUnresolvedParentCommand
	{
		RowHandle Target;
		FMapKey ParentId;
		FName MappingDomain;
		const FTedsHierarchyAccessInterface* HierarchyAccessInterface = nullptr;
		ICoreProvider* CoreProvider = nullptr;

		void operator()() const
		{
			HierarchyAccessInterface->SetUnresolvedParent(CoreProvider, Target, ParentId, MappingDomain);
		}
	};

	
	void FTedsHierarchyAccessInterface::SetParent(ICommonQueryContext& Context, RowHandle Target, RowHandle Parent) const
	{
		FSetParentCommand Command;
		Command.Parent = Parent;
		Command.Target = Target;
		Command.HierarchyAccessInterface = this;
		Command.CoreProvider = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		Context.PushCommand(MoveTemp(Command));
	}
	

	void FTedsHierarchyAccessInterface::SetUnresolvedParent(ICommonQueryContext& Context, RowHandle Target, FMapKey ParentId, FName MappingDomain) const
	{
		FSetUnresolvedParentCommand Command;
		Command.ParentId = ParentId;
		Command.Target = Target;
		Command.MappingDomain = MappingDomain;
		Command.HierarchyAccessInterface = this;
		Command.CoreProvider = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		Context.PushCommand(MoveTemp(Command));	
	}

	RowHandle FTedsHierarchyAccessInterface::GetParent(const ICommonQueryContext& Context, RowHandle Target) const
	{
		// NOTE: It is not clear to the user that they will need to have registered the HierarchyDataColumnType as at least ReadOnly
		// for this function to return the Parent.
		// This can be improved with a function on the the context (and other contexts) to check for the access requirements of a
		// query and warn or ensure about a missing requirement - though this does not currently exist at time of writing.
		const FEditorDataHierarchyData_Template* ParentHierarchyDataComponent;
		{
			const int RowIndexInChunk = Hack_FindRowIndexInChunk(Context, Target);
			if (RowIndexInChunk == INDEX_NONE)
			{
				return InvalidRowHandle;
			}
			const void* HierarchyDataComponentArrayStart = Context.GetColumn(HierarchyDataColumnType);
			if (HierarchyDataComponentArrayStart == nullptr)
			{
				return InvalidRowHandle;
			}
			const uint8* HierarchyDataComponentObjectAddress = static_cast<const uint8*>(HierarchyDataComponentArrayStart) + (RowIndexInChunk * HierarchyDataColumnType->GetStructureSize());
			ParentHierarchyDataComponent = reinterpret_cast<const FEditorDataHierarchyData_Template*>(HierarchyDataComponentObjectAddress);
		}

		return ParentHierarchyDataComponent->Parent;
	}

	void FTedsHierarchyAccessInterface::SetParentRow(IQueryContext& Context, RowHandle Target, RowHandle Parent) const
	{
		SetParent(static_cast<ICommonQueryContext&>(Context), Target, Parent);
	}

	void FTedsHierarchyAccessInterface::SetUnresolvedParent(IQueryContext& Context, RowHandle Target, FMapKey ParentId, FName MappingDomain) const
	{
		SetUnresolvedParent(static_cast<ICommonQueryContext&>(Context), Target, ParentId, MappingDomain);
	}

	RowHandle FTedsHierarchyAccessInterface::GetParentRow(const IQueryContext& Context, RowHandle Target) const
	{
		return GetParent(static_cast<const ICommonQueryContext&>(Context), Target);
	}

	void FTedsHierarchyAccessInterface::SetParentRow(ISubqueryContext& Context, RowHandle Target, RowHandle Parent) const
	{
		SetParent(static_cast<ICommonQueryContext&>(Context), Target, Parent);
	}

	void FTedsHierarchyAccessInterface::SetUnresolvedParent(ISubqueryContext& Context, RowHandle Target, FMapKey ParentId, FName MappingDomain) const
	{
		SetUnresolvedParent(static_cast<ICommonQueryContext&>(Context), Target, ParentId, MappingDomain);
	}

	RowHandle FTedsHierarchyAccessInterface::GetParentRow(const ISubqueryContext& Context, RowHandle Target) const
	{
		return GetParent(static_cast<const ICommonQueryContext&>(Context), Target);
	}

	void FTedsHierarchyAccessInterface::SetParentRow(IDirectQueryContext& Context, RowHandle Target, RowHandle Parent) const
	{
		SetParent(static_cast<ICommonQueryContext&>(Context), Target, Parent);
	}

	void FTedsHierarchyAccessInterface::SetUnresolvedParent(IDirectQueryContext& Context, RowHandle Target, FMapKey ParentId, FName MappingDomain) const
	{
		SetUnresolvedParent(static_cast<ICommonQueryContext&>(Context), Target, ParentId, MappingDomain);
	}

	RowHandle FTedsHierarchyAccessInterface::GetParentRow(const IDirectQueryContext& Context, RowHandle Target) const
	{
		// NOTE: It is not clear to the user that they will need to have registered the HierarchyDataColumnType as at least ReadOnly
		// for this function to return the Parent.
		// This can be improved with a function on the the context (and other contexts) to check for the access requirements of a
		// query and warn or ensure about a missing requirement - though this does not currently exist at time of writing.
		const FEditorDataHierarchyData_Template* ParentHierarchyDataComponent;
		{
			const int RowIndexInChunk = Hack_FindRowIndexInChunk(Context, Target);
			if (RowIndexInChunk == INDEX_NONE)
			{
				return InvalidRowHandle;
			}
			const void* HierarchyDataComponentArrayStart = Context.GetColumn(HierarchyDataColumnType);
			if (HierarchyDataComponentArrayStart == nullptr)
			{
				return InvalidRowHandle;
			}
			const uint8* HierarchyDataComponentObjectAddress = static_cast<const uint8*>(HierarchyDataComponentArrayStart) + (RowIndexInChunk * HierarchyDataColumnType->GetStructureSize());
			ParentHierarchyDataComponent = reinterpret_cast<const FEditorDataHierarchyData_Template*>(HierarchyDataComponentObjectAddress);
		}

		return ParentHierarchyDataComponent->Parent;
	}
}
