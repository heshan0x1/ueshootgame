// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsActorCompatibilityModule.h"

#include "ActorComponentDebugHierarchyWidget/ActorComponentTree.h"
#include "Columns/TedsActorComponentCompatibilityColumns.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "DataStorage/Features.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

namespace UE::Editor::DataStorage
{
	static bool bEnableActorComponentCompatibility = true;
	FAutoConsoleVariableRef CVarEnableActorComponentRegistrator(
		TEXT("TEDS.Feature.ActorCompatibility.ActorComponents.Enable"),
		bEnableActorComponentCompatibility,
		TEXT("Enables ActorComponent tracking in TEDS"),
		ECVF_ReadOnly);

	static bool bEnableDemoTab = false;
	FAutoConsoleVariableRef CVarAddDemoTab(
		TEXT("TEDS.Debug.ActorCompatibility.ActorComponents.EnableHierarchyViewer"),
		bEnableDemoTab,
		TEXT("Enables a debug tab to demonstrate usage of hierarchy views with ActorComponents"),
		ECVF_ReadOnly);
	
	void FTedsActorCompatibilityModule::StartupModule()
	{
		IModuleInterface::StartupModule();

		FModuleManager::Get().LoadModule(TEXT("TypedElementFramework"));
		FModuleManager::Get().LoadModule(TEXT("EditorDataStorageHierarchy"));

		if (bEnableActorComponentCompatibility)
		{
			using namespace UE::Editor::DataStorage;
			using namespace UE::Editor::DataStorage::Queries;

			UE::Editor::DataStorage::OnEditorDataStorageFeaturesEnabled().AddLambda([this]()
			{
				using namespace UE::Editor::DataStorage;
				using namespace UE::Editor::DataStorage::Queries;
				
				ICoreProvider* CoreProvider = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		
				const FHierarchyRegistrationParams HierarchyParams
				{
					.Name = TEXT("ActorComponent"),
				};

				CoreProvider->RegisterHierarchy(HierarchyParams);
		
				TableHandle ActorComponentTable = CoreProvider->RegisterTable(
					{
						FTypedElementUObjectColumn::StaticStruct(),
						FTypedElementUObjectIdColumn::StaticStruct(),
						FTypedElementClassTypeInfoColumn::StaticStruct(),
						FActorComponentTypeTag::StaticStruct(),
						FTypedElementLabelColumn::StaticStruct(),
						FTypedElementLabelHashColumn::StaticStruct(),
						FTypedElementSyncFromWorldTag::StaticStruct()
					},
					FName("Editor_ActorComponentTable"));

				ICompatibilityProvider* CompatibilityProvider = GetMutableDataStorageFeature<ICompatibilityProvider>(CompatibilityFeatureName);
				CompatibilityProvider->RegisterTypeTableAssociation(UActorComponent::StaticClass(), ActorComponentTable);

				// 1. For actors that need syncing, add any components they have to the compatibility layer
				// This solves a lot of issues around trying to intercept all the places components are added and removed
				struct FRegisterActorComponentsWithCompatibility
				{
					TWeakObjectPtr<AActor> WkActor;
					RowHandle ActorRow;
					TArray<TWeakObjectPtr<UActorComponent>> ComponentsToRegister;

					void operator()()
					{
						AActor* Actor = WkActor.Get();
						if (!Actor)
						{
							return;
						}
						ICompatibilityProvider* CompatibilityProvider = GetMutableDataStorageFeature<ICompatibilityProvider>(CompatibilityFeatureName);

						for (TWeakObjectPtr<UActorComponent> Component : ComponentsToRegister)
						{
							UActorComponent* NewComponent = Component.Get();
							if (!NewComponent)
							{
								CompatibilityProvider->AddCompatibleObject(NewComponent);
							}
						}
					};
				};
				CoreProvider->RegisterQuery(
					Select(
						TEXT("Register ActorComponent Ownership"),
						FProcessor(EQueryTickPhase::DuringPhysics, CoreProvider->GetQueryTickGroupName(EQueryTickGroups::Default))
						.SetExecutionMode(EExecutionMode::GameThread),
						[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& ObjectColumn)
						{
							if (AActor* Actor = static_cast<AActor*>(ObjectColumn.Object.Get()))
							{
								FRegisterActorComponentsWithCompatibility Command;
								Command.WkActor = Actor;
								Command.ActorRow = Row;
								
								constexpr bool bIncludeFromChildActors = false;
								Actor->ForEachComponent(bIncludeFromChildActors, [&Context, &Command](UActorComponent* Component)
								{
									RowHandle ComponentRow = Context.LookupMappedRow(ICompatibilityProvider::ObjectMappingDomain, FMapKeyView(static_cast<const UObject*>(Component)));
									if (ComponentRow == InvalidRowHandle)
									{
										Command.ComponentsToRegister.Add(Component);
									}
								});
								
								if (!Command.ComponentsToRegister.IsEmpty())
								{
									Context.PushCommand<FRegisterActorComponentsWithCompatibility>(MoveTemp(Command));
								}
							}
						})
					.Where()
						.All<FTypedElementActorTag>()
						.Any<FTypedElementSyncFromWorldTag>()
					.Compile());

				// 2. For components that aren't a child, which means they don't have an associated owner, set up that owner
				//    If a component is not associated with an owner, then remove that association in TEDS - in practice this is usally only a
				//    transitory state
				//    Note: ActorComponent ownership can change due to renaming.  
				CoreProvider->RegisterQuery(
					Select(
						TEXT("Set ActorComponent Hierarchy"),
						FProcessor(EQueryTickPhase::PostPhysics, CoreProvider->GetQueryTickGroupName(EQueryTickGroups::Default))
						.SetExecutionMode(EExecutionMode::GameThread),
						[](IQueryContext& Context, RowHandle ComponentRow, const FTypedElementUObjectColumn& ObjectColumn)
						{
							if (UActorComponent* ActorComponent = Cast<UActorComponent>(ObjectColumn.Object.Get()))
							{
								const AActor* OwnerActor = ActorComponent->GetOwner();
								RowHandle ActorRow;
								if (OwnerActor)
								{
									ActorRow = Context.LookupMappedRow(ICompatibilityProvider::ObjectMappingDomain, FMapKeyView(static_cast<const UObject*>(OwnerActor)));
								}
								else
								{
									ActorRow = InvalidRowHandle;
								}
								
								const bool bNotAssociatedWithActor = OwnerActor == nullptr && ActorRow == InvalidRowHandle;
								const bool bActorRowAssigned = Context.IsRowAssigned(ActorRow);

								RowHandle ParentRowOfComponent = Context.GetParentRow(ComponentRow);
								
								if (bNotAssociatedWithActor || (bActorRowAssigned && ParentRowOfComponent != ActorRow))
								{
									Context.SetParentRow(ComponentRow, ActorRow);
								}
							}
						})
						.AccessesHierarchy(TEXT("ActorComponent"))
						.Where()
							.All<FActorComponentTypeTag, FTypedElementSyncFromWorldTag>()
						.Compile());

				// 
				CoreProvider->RegisterQuery(
					Select(
						TEXT("Sync actorcomponent label to column"),
						FProcessor(EQueryTickPhase::PrePhysics, CoreProvider->GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
						.SetExecutionMode(EExecutionMode::GameThread),
						[](const FTypedElementUObjectColumn& Actor, FTypedElementLabelColumn& Label, FTypedElementLabelHashColumn& LabelHash)
						{
							if (const UActorComponent* ComponentInstance = Cast<UActorComponent>(Actor.Object); ComponentInstance != nullptr)
							{
								const FString Name = ComponentInstance->GetName();
								uint64 NameHash = CityHash64(reinterpret_cast<const char*>(*Name), Name.Len() * sizeof(**Name));
								if (LabelHash.LabelHash != NameHash)
								{
									Label.Label = Name;
									LabelHash.LabelHash = NameHash;
								}
							}
						}
					)
					.Where()
						.All<FActorComponentTypeTag, FTypedElementSyncFromWorldTag>()
					.Compile());
			});
		}

		if (bEnableDemoTab)
		{
			UE::Editor::DataStorage::ActorCompatibility::RegisterActionComponentDebugHierarchyWidget();
		}
	}

	void FTedsActorCompatibilityModule::ShutdownModule()
	{
		IModuleInterface::ShutdownModule();
	}
} // namespace UE::Editor::DataStorage

IMPLEMENT_MODULE(UE::Editor::DataStorage::FTedsActorCompatibilityModule, TedsActorCompatibility);