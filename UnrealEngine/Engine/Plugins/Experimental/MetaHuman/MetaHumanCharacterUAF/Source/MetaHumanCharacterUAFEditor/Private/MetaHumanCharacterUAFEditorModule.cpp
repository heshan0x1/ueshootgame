// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystem/MetaHumanCharacterBuild.h"
#include "MetaHumanDefaultPipelineBase.h"

#include "Modules/ModuleManager.h"
#include "SubobjectDataSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

#include "AnimNextRigVMAsset.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "MetaHumanCharacterUAFProjectSettings.h"
#include "Component/AnimNextComponent.h"
#include "Graph/PostProcessAnimationAssetUserData.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanCharacterUAF, Log, All);

class FMetaHumanCharacterUAFEditorModule
	: public IModuleInterface
	, public IMetaHumanCharacterBuildExtender
	, public IMetaHumanCharacterPipelineExtender
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(IMetaHumanCharacterBuildExtender::FeatureName, static_cast<IMetaHumanCharacterBuildExtender*>(this));
		IModularFeatures::Get().RegisterModularFeature(IMetaHumanCharacterPipelineExtender::FeatureName, static_cast<IMetaHumanCharacterPipelineExtender*>(this));
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(IMetaHumanCharacterBuildExtender::FeatureName, static_cast<IMetaHumanCharacterBuildExtender*>(this));
		IModularFeatures::Get().UnregisterModularFeature(IMetaHumanCharacterPipelineExtender::FeatureName, static_cast<IMetaHumanCharacterPipelineExtender*>(this));
	}

	//~Begin IMetaHumanCharacterBuildExtender interface

	static inline const FName AnimationSystemName = TEXT("Unreal Animation Framework (UAF) - Experimental");
	virtual TArray<FName> GetAnimationSystemOptions() const override
	{
		return {AnimationSystemName};
	}

	// Ensure assets from our plugin get considered.
	virtual TSet<FString> GetMountPoints() const override
	{
		return {UE_PLUGIN_NAME};
	}

	// Ensure the UAF assets get copied over to the project common folder.
	virtual TSet<FName> GetPackageRootsToCopyFrom() const override
	{
		return {UE_PLUGIN_NAME};
	}

	// Manually copy required UAF assets as dependencies are created after duplication in the assembly process.
	virtual TArray<UObject*> GetRootObjects(const FMetaHumanCharacterEditorBuildParameters& BuildParameters) const override
	{
		if (BuildParameters.AnimationSystemName != AnimationSystemName)
		{
			return {};
		}
		
		auto AddObject = [](const FString& AssetPath, TArray<UObject*>& OutRootObjects)
		{
			UObject* Object = LoadObject<UObject>(nullptr, *AssetPath);
			check(Object);
			OutRootObjects.Add(Object);
		};

		TArray<UObject*> Result;
		AddObject(TEXT("/" UE_PLUGIN_NAME "/Animation/UAF/MetaHuman_Body_AnimGraph.MetaHuman_Body_AnimGraph"), Result);
		AddObject(TEXT("/" UE_PLUGIN_NAME "/Animation/UAF/MetaHuman_Face_AnimGraph.MetaHuman_Face_AnimGraph"), Result);
		AddObject(TEXT("/" UE_PLUGIN_NAME "/Animation/UAF/MetaHuman_Module.MetaHuman_Module"), Result);
		return Result;
	}

	void SetCommonPath(const FString& InCommonFolderPath) override
	{
		CommonFolderPath = InCommonFolderPath;
	}
	FString CommonFolderPath;

	//~End IMetaHumanCharacterBuildExtender interface

	static bool AssignPostProcessAnimation(USkeletalMesh* SkeletalMesh, FStringView ObjectPath)
	{
		if (!SkeletalMesh)
		{
			return false;
		}
		
		SkeletalMesh->SetPostProcessAnimBlueprint(nullptr);

		if (UObject* PostProcessAnimGraph = StaticLoadObject(UObject::StaticClass(), nullptr, ObjectPath))
		{
			if (UPostProcessAnimationUserAssetData* UserAssetData = Cast<UPostProcessAnimationUserAssetData>(SkeletalMesh->GetAssetUserDataOfClass(UPostProcessAnimationUserAssetData::StaticClass())))
			{
				UserAssetData->AnimationAsset = PostProcessAnimGraph;
				return true;
			}
			else
			{
				UserAssetData = NewObject<UPostProcessAnimationUserAssetData>(SkeletalMesh);
				UserAssetData->AnimationAsset = PostProcessAnimGraph;
				SkeletalMesh->AddAssetUserData(UserAssetData);
				return true;
			}
		}

		return false;
	}
	
	FString GetCommonPathFor(const FString& ObjectPath) const
	{
		return FPaths::Combine(CommonFolderPath, ObjectPath);
	}

	//~Begin IMetaHumanCharacterPipelineExtender interface

	virtual TSubclassOf<AActor> GetOverwriteBlueprint(EMetaHumanQualityLevel QualityLevel, FName InAnimationSystemName) const override
	{
		if (InAnimationSystemName != AnimationSystemName)
		{
			return {};
		}
		
		if (const UMetaHumanCharacterUAFProjectSettings* Settings = GetDefault<UMetaHumanCharacterUAFProjectSettings>())
		{
			const TSoftClassPtr<AActor> ActorBlueprint = Settings->Blueprints[QualityLevel];
			const FString AssetPath = ActorBlueprint.ToSoftObjectPath().GetAssetPathString();

			if (!ActorBlueprint.IsNull())
			{
				ActorBlueprint.LoadSynchronous();
				if (ActorBlueprint)
				{
					return ActorBlueprint.Get();
				}
				else
				{
					UE_LOGFMT(LogMetaHumanCharacterUAF, Error,
						"Cannot assemble UAF MetaHuman as the actor blueprint '{AssetPath}' for quality level '{Quality}' is invalid. Does the asset exist on disk?",
						AssetPath, ("Quality", UEnum::GetValueAsString(QualityLevel)));
				}
			}
			else
			{
				UE_LOGFMT(LogMetaHumanCharacterUAF, Error,
					"Cannot assemble UAF MetaHuman as no actor blueprint for quality level '{Quality}' has been chosen. "
					"Did you select a valid actor blueprint in the project settings in the MetaHuman Character UAF plugin?",
					("Quality", UEnum::GetValueAsString(QualityLevel)));
			}
		}

		return {};
	}

	static bool ContainsUAFComponent(TNotNull<UBlueprint*> InBlueprint, const TArray<FSubobjectDataHandle>& SubobjectDataHandles)
	{
		for (const FSubobjectDataHandle& Handle : SubobjectDataHandles)
		{
			if (UActorComponent* ActorComponent = const_cast<UActorComponent*>(Handle.GetData()->GetObjectForBlueprint<UActorComponent>(InBlueprint)))
			{
				if (Cast<UAnimNextComponent>(ActorComponent))
				{
					return true;
				}
			}
		}

		return false;
	}

	virtual void ModifyBlueprint(TNotNull<UBlueprint*> InBlueprint) override
	{
		AActor* ActorCDO = InBlueprint->GeneratedClass->GetDefaultObject<AActor>();
		if (!ActorCDO)
		{
			return;
		}

		TArray<FSubobjectDataHandle> SubobjectDataHandles;
		USubobjectDataSubsystem* SubobjectDataSubsystem = USubobjectDataSubsystem::Get();
		SubobjectDataSubsystem->GatherSubobjectData(ActorCDO, SubobjectDataHandles);
		SubobjectDataHandles = TSet(SubobjectDataHandles).Array();

		if (!ContainsUAFComponent(InBlueprint, SubobjectDataHandles))
		{
			return;
		}

		for (const FSubobjectDataHandle& Handle : SubobjectDataHandles)
		{
			if (UActorComponent* ActorComponent = const_cast<UActorComponent*>(Handle.GetData()->GetObjectForBlueprint<UActorComponent>(InBlueprint)))
			{
				if (USkeletalMeshComponent* SkelMeshComponent = Cast<USkeletalMeshComponent>(ActorComponent))
				{
					// Disable legacy animation system for all skeletal mesh components. These need to be disabled when using UAF.
					SkelMeshComponent->SetEnableAnimation(false);

					USkeletalMesh* SkeletalMesh = SkelMeshComponent->GetSkeletalMeshAsset();
					FString ComponentName = ActorComponent->GetName();
					ComponentName.RemoveFromEnd(UActorComponent::ComponentTemplateNameSuffix);

					// Assign the post-process anim graphs for the face & body skeletal meshes.
					if (ComponentName == TEXT("Face"))
					{
						if (!AssignPostProcessAnimation(SkeletalMesh, GetCommonPathFor("Animation/UAF/MetaHuman_Face_AnimGraph.MetaHuman_Face_AnimGraph")))
						{
							UE_LOGFMT(LogMetaHumanCharacterUAF, Error, "Assigning UAF post-process anim graph for '{ComponentName}' failed.", ComponentName);
						}
							
					}
					else if (ComponentName == TEXT("Body"))
					{
						if (!AssignPostProcessAnimation(SkeletalMesh, GetCommonPathFor("Animation/UAF/MetaHuman_Body_AnimGraph.MetaHuman_Body_AnimGraph")))
						{
							UE_LOGFMT(LogMetaHumanCharacterUAF, Error, "Assigning UAF post-process anim graph for '{ComponentName}' failed.", ComponentName);
						}
					}
				}

				if (UAnimNextComponent* UAFComponent = Cast<UAnimNextComponent>(ActorComponent))
				{
					// Recompile UAF module.
					if (TObjectPtr<UAnimNextModule> Module = UAFComponent->GetModule())
					{
						if (UAnimNextRigVMAsset* AnimNextRigVMAsset = CastChecked<UAnimNextRigVMAsset>(Module.Get()))
						{
							if (UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(AnimNextRigVMAsset))
							{
								if (IRigVMClientHost* VMClientHost = Cast<IRigVMClientHost>(EditorData))
								{
									VMClientHost->RecompileVM();
								}
							}
						}
					}
				}
			}
		}

		// Relink the object paths of the variables in the set variable nodes.
		const FString OldPath = TEXT("/" UE_PLUGIN_NAME "/Animation/UAF/");
		const FString NewPath = GetCommonPathFor("Animation/UAF/");
		for (UEdGraph* Graph : InBlueprint->UbergraphPages)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->GetClass()->GetName() == TEXT("K2Node_AnimNextComponentSetVariable"))
				{
					for (UEdGraphPin* Pin : Node->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == TEXT("Variable"))
						{
							FString& Value = Pin->DefaultValue;
							Value = Value.Replace(*OldPath, *NewPath);
						}
					}
				}
			}
		}
	}

	//~End IMetaHumanCharacterPipelineExtender interface
};

IMPLEMENT_MODULE(FMetaHumanCharacterUAFEditorModule, MetaHumanCharacterUAFEditor)