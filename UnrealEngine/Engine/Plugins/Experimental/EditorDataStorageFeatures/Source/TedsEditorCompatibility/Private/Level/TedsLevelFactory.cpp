// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsLevelFactory.h"

#include "DataStorage/Features.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementIconOverrideColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Styling/SlateIconFinder.h"
#include "UObject/Package.h"

namespace UE::Editor::DataStorage::Levels
{
	static const FName LevelTableName("Editor_Level");
}

void UTedsLevelFactory::RegisterTables(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage;
	
	TableHandle LevelTable = DataStorage.RegisterTable<FLevelTag, FTypedElementLabelColumn, FTypedElementIconOverrideColumn>(Levels::LevelTableName);

	DataStorageCompat = GetMutableDataStorageFeature<ICompatibilityProvider>(CompatibilityFeatureName);
	checkf(DataStorageCompat, TEXT("TEDS Factory cannot be init before the data storage compatibility feature is available"));
	
	DataStorageCompat->RegisterTypeTableAssociation(ULevel::StaticClass(), LevelTable);
}

void UTedsLevelFactory::PreRegister(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UTedsLevelFactory::OnLevelAddedToWorld);
	FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UTedsLevelFactory::OnLevelRemovedFromWorld);
}

void UTedsLevelFactory::PreShutdown(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
}

void UTedsLevelFactory::RegisterQueries(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::DataStorage::Queries;
	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync level label to column"),
			FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](const FTypedElementUObjectColumn& LevelColumn, FTypedElementLabelColumn& Label)
			{
				if (const ULevel* Level = Cast<ULevel>(LevelColumn.Object); Level != nullptr)
				{
					Label.Label = FPackageName::GetShortName(Level->GetOutermost()->GetName());
				}
			}
		)
		.Where()
			.All<FLevelTag, FTypedElementSyncFromWorldTag>()
		.Compile());

	struct FRegisterLevelCommand
	{
		void operator()() const
		{
			if (ULevel* Level = LevelPtr.Get())
			{
				TedsCompat->AddCompatibleObject(Level);
			}
		}

		ICompatibilityProvider* TedsCompat;
		TWeakObjectPtr<ULevel> LevelPtr;
	};

	// The Persistent world for each world needs to be tracked separately, since it doesn't go through LevelAddedToWorld/LevelRemovedFromWorld
	DataStorage.RegisterQuery(
		Select(TEXT("Register Persistent Level on World creation"),
			FObserver::OnAdd<FWorldTag>(),
			[this](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& ObjectColumn)
			{
				if (UWorld* World = Cast<UWorld>(ObjectColumn.Object.Get()))
				{
					FRegisterLevelCommand Command{.TedsCompat = DataStorageCompat, .LevelPtr = World->PersistentLevel};
					Context.PushCommand(Command);
				}
			})
		.Where()
			.All<FWorldTag>()
		.Compile());

	struct FUnregisterLevelCommand
	{
		void operator()() const
		{
			if (ULevel* Level = LevelPtr.Get())
			{
				TedsCompat->RemoveCompatibleObject(Level);
			}
		}

		ICompatibilityProvider* TedsCompat;
		TWeakObjectPtr<ULevel> LevelPtr;
	};

	DataStorage.RegisterQuery(
		Select(TEXT("Unregister Persistent Level on World removal"),
			FObserver::OnRemove<FTypedElementUObjectColumn>(),
			[this](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& ObjectColumn)
			{
				// We need to make sure the persistent level is removed properly if the world is marked for GC
				constexpr bool bEvenIfPendingKill = true;
				if (UWorld* World = Cast<UWorld>(ObjectColumn.Object.Get(bEvenIfPendingKill)))
				{
					FUnregisterLevelCommand Command{.TedsCompat = DataStorageCompat, .LevelPtr = World->PersistentLevel};
					Context.PushCommand(Command);
				}
			})
		.Where()
			.All<FWorldTag>()
		.Compile());

	DataStorage.RegisterQuery(
		Select(TEXT("Add Icon override to level"),
			FObserver::OnAdd<FTypedElementUObjectColumn>(),
			[this](IQueryContext& Context, RowHandle Row, FTypedElementIconOverrideColumn& IconOverrideColumn)
			{
				// ULevels show up with the icon for UWorld in the editor (e.g Outliner)
				FSlateIcon WorldIcon = FSlateIconFinder::FindIconForClass(UWorld::StaticClass());
				IconOverrideColumn.IconName = WorldIcon.GetStyleName();
			})
		.Where()
			.All<FLevelTag>()
		.Compile());
}

void UTedsLevelFactory::OnLevelAddedToWorld(ULevel* InLevel, UWorld* InWorld)
{
	if (DataStorageCompat)
	{
		DataStorageCompat->AddCompatibleObject(InLevel);
	}
}

void UTedsLevelFactory::OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	if (DataStorageCompat)
	{
		DataStorageCompat->RemoveCompatibleObject(InLevel);
	}
}
