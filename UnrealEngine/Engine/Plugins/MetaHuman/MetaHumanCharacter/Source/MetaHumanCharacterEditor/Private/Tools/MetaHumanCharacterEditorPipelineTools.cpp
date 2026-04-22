// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorPipelineTools.h"

#include "InteractiveToolManager.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanCharacterPaletteProjectSettings.h"
#include "MetaHumanSDKSettings.h"

#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "SceneManagement.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"
#include "ToolTargetManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SoftObjectPtr.h"
#include "Dialogs/Dialogs.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

class FPipelineToolCommandChange : public FToolCommandChange
{
public:

	FPipelineToolCommandChange(const FMetaHumanCharacterAssemblySettings& InOldAssemblySettings,
		const FMetaHumanCharacterAssemblySettings& InNewAssemblySettings,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldAssemblySettings{ InOldAssemblySettings }
		, NewAssemblySettings{ InNewAssemblySettings }
		, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual FString ToString() const override
	{
		return TEXT("MetaHuman Character Edit Assembly");
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		return !ToolManager.IsValid();
	}

	void Apply(UObject* InObject) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

		Character->Modify();
		Character->AssemblySettings = NewAssemblySettings;

		UpdatePipelineToolProperties(NewAssemblySettings);
	}

	void Revert(UObject* InObject) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

		Character->Modify();
		Character->AssemblySettings = OldAssemblySettings;

		UpdatePipelineToolProperties(OldAssemblySettings);
	}
	//~End FToolCommandChange interface

protected:

	void UpdatePipelineToolProperties(const FMetaHumanCharacterAssemblySettings& InAssemblySettings)
	{
		if (ToolManager.IsValid())
		{
			if (UMetaHumanCharacterEditorPipelineTool* PipelineTool = Cast<UMetaHumanCharacterEditorPipelineTool>(ToolManager->GetActiveTool(EToolSide::Left)))
			{
				UMetaHumanCharacterEditorPipelineToolProperties* PipelineToolProperties = nullptr;
				if (PipelineTool->GetToolProperties().FindItemByClass<UMetaHumanCharacterEditorPipelineToolProperties>(&PipelineToolProperties))
				{
					PipelineToolProperties->CopyFrom(InAssemblySettings);
					PipelineToolProperties->SilentUpdateWatched();

					PipelineTool->PreviousAssemblySettings = InAssemblySettings;
				}
			}
		}
	}

protected:

	FMetaHumanCharacterAssemblySettings OldAssemblySettings;
	FMetaHumanCharacterAssemblySettings NewAssemblySettings;

	TWeakObjectPtr<UInteractiveToolManager> ToolManager;

};

bool UMetaHumanCharacterEditorPipelineToolBuilder::CanBuildTool(const FToolBuilderState& InSceneState) const
{
	const int32 NumTargets = InSceneState.TargetManager->CountSelectedAndTargetableWithPredicate(InSceneState, GetTargetRequirements(), [](UActorComponent& Component)
	{
		return Component.GetOwner()->Implements<UMetaHumanCharacterEditorActorInterface>();
	});

	// Restrict the tool to a single target
	return NumTargets == 1;
}

UInteractiveTool* UMetaHumanCharacterEditorPipelineToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	switch (ToolType)
	{
		case EMetaHumanCharacterPipelineEditingTool::Pipeline:
		{
			UMetaHumanCharacterEditorPipelineTool* PipelineTool = NewObject<UMetaHumanCharacterEditorPipelineTool>(InSceneState.ToolManager);
			PipelineTool->SetTarget(Target);
			PipelineTool->SetTargetWorld(InSceneState.World);
			return PipelineTool;
		}

		default:
			checkNoEntry();
	}

	return nullptr;
}

const FToolTargetTypeRequirements& UMetaHumanCharacterEditorPipelineToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements(
		TArray<const UClass*>
		{
			UPrimitiveComponentBackedTarget::StaticClass()
		}
	);

	return TypeRequirements;
}

void UMetaHumanCharacterEditorPipelineToolProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	//const FName MemberPropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorPipelineToolProperties, PipelineType)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorPipelineToolProperties, PipelineQuality))
	{
		if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet)
		{
			UpdateSelectedPipeline();
		}
	}
}

void UMetaHumanCharacterEditorPipelineToolProperties::CopyFrom(const FMetaHumanCharacterAssemblySettings& InAssemblySettings)
{
	PipelineQuality = InAssemblySettings.PipelineQuality;
	PipelineType = InAssemblySettings.PipelineType;

	DefaultAnimationSystemNameAnimBP = InAssemblySettings.AnimationSystemNameAnimBP;
	AnimationSystemName = InAssemblySettings.AnimationSystemName;
	ArchiveName = InAssemblySettings.ArchiveName;
	NameOverride = InAssemblySettings.NameOverride.IsEmpty() ? DefaultName : InAssemblySettings.NameOverride;

	CommonDirectory = InAssemblySettings.CommonDirectory;
	RootDirectory = InAssemblySettings.RootDirectory;
	OutputFolder = InAssemblySettings.OutputFolder;

	bBakeMakeup = InAssemblySettings.bBakeMakeup;
	bExportZipFile = InAssemblySettings.bExportZipFile;
}

void UMetaHumanCharacterEditorPipelineToolProperties::CopyTo(FMetaHumanCharacterAssemblySettings& OutAssemblySettings)
{
	OutAssemblySettings.PipelineQuality = PipelineQuality;
	OutAssemblySettings.PipelineType = PipelineType;

	OutAssemblySettings.AnimationSystemName = AnimationSystemName;
	OutAssemblySettings.ArchiveName = ArchiveName;
	OutAssemblySettings.NameOverride = NameOverride.IsEmpty() ? DefaultName : NameOverride;

	OutAssemblySettings.CommonDirectory = CommonDirectory;
	OutAssemblySettings.RootDirectory = RootDirectory;
	OutAssemblySettings.OutputFolder = OutputFolder;

	OutAssemblySettings.bBakeMakeup = bBakeMakeup;
	OutAssemblySettings.bExportZipFile = bExportZipFile;
}

void UMetaHumanCharacterEditorPipelineToolProperties::UpdateSelectedPipeline()
{
	TSoftClassPtr<UMetaHumanCharacterPipeline> PipelineClassPtr = GetSelectedPipelineClass();
	if (PipelineClassPtr != nullptr)
	{
		TNotNull<UMetaHumanCharacterEditorPipelineTool*> PipelineTool = GetTypedOuter<UMetaHumanCharacterEditorPipelineTool>();
		TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(PipelineTool->GetTarget());

		TObjectPtr<UMetaHumanCollectionPipeline>& CollectionPipeline = Character->PipelinesPerClass.FindOrAdd(GetSelectedPipelineClass().LoadSynchronous());
		if (CollectionPipeline == nullptr)
		{
			CollectionPipeline = NewObject<UMetaHumanCollectionPipeline>(Character, GetSelectedPipelineClass().LoadSynchronous());
		}
	}

	OnPipelineSelectionChanged.ExecuteIfBound();
}

TObjectPtr<UMetaHumanCollectionPipeline> UMetaHumanCharacterEditorPipelineToolProperties::GetSelectedPipeline() const
{
	TSoftClassPtr<UMetaHumanCollectionPipeline> PipelineClassPtr = GetSelectedPipelineClass();
	if (PipelineClassPtr != nullptr)
	{
		TNotNull<UMetaHumanCharacterEditorPipelineTool*> PipelineTool = GetTypedOuter<UMetaHumanCharacterEditorPipelineTool>();
		TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(PipelineTool->GetTarget());

		return Character->PipelinesPerClass.FindOrAdd(GetSelectedPipelineClass().LoadSynchronous());
	}

	return nullptr;
}

TObjectPtr<UMetaHumanCollectionEditorPipeline> UMetaHumanCharacterEditorPipelineToolProperties::GetSelectedEditorPipeline() const
{
	if (TObjectPtr<UMetaHumanCollectionPipeline> ActivePipeline = GetSelectedPipeline())
	{
		return ActivePipeline->GetMutableEditorPipeline();
	}

	return nullptr;
}

FMetaHumanCharacterEditorBuildParameters UMetaHumanCharacterEditorPipelineToolProperties::InitBuildParameters() const
{
	FMetaHumanCharacterEditorBuildParameters BuildParams;
	BuildParams.PipelineOverride = GetSelectedPipeline();
	BuildParams.PipelineType = PipelineType;
	BuildParams.PipelineQuality = PipelineQuality;
	BuildParams.AnimationSystemName = AnimationSystemName;

	if (BuildParams.PipelineType == EMetaHumanDefaultPipelineType::Cinematic)
	{
		BuildParams.PipelineQuality = EMetaHumanQualityLevel::Cinematic;
	}

	if (PipelineType == EMetaHumanDefaultPipelineType::Cinematic || PipelineType == EMetaHumanDefaultPipelineType::Optimized)
	{
		if (RootDirectory.Path.IsEmpty())
		{
			// Make the full output path to be used based on the MH SDK settings
			if (const UMetaHumanSDKSettings* Settings = GetDefault<UMetaHumanSDKSettings>())
			{
				if (PipelineQuality == EMetaHumanQualityLevel::Cinematic)
				{
					BuildParams.AbsoluteBuildPath = Settings->CinematicImportPath.Path;
				}
				else
				{
					BuildParams.AbsoluteBuildPath = Settings->OptimizedImportPath.Path;
				}
			}
		}
		else
		{
			BuildParams.AbsoluteBuildPath = RootDirectory.Path;
		}

		BuildParams.CommonFolderPath = CommonDirectory.Path;
		BuildParams.NameOverride = NameOverride;
	}
	else if (PipelineType == EMetaHumanDefaultPipelineType::UEFN)
	{
		BuildParams.NameOverride = NameOverride;
	}
	else if (PipelineType == EMetaHumanDefaultPipelineType::DCC)
	{
		BuildParams.AbsoluteBuildPath = OutputFolder.Path;
		BuildParams.NameOverride = ArchiveName;
		BuildParams.bBakeMakeup = bBakeMakeup;
		BuildParams.bExportZipFile = bExportZipFile;
	}

	return BuildParams;
}

TSoftClassPtr<UMetaHumanCollectionPipeline> UMetaHumanCharacterEditorPipelineToolProperties::GetSelectedPipelineClass() const
{
	return FMetaHumanCharacterEditorBuild::GetDefaultPipelineClass(PipelineType, PipelineQuality);
}

TArray<FName> UMetaHumanCharacterEditorPipelineToolProperties::GetAnimationSystemOptions() const
{
	TArray<FName> Options = { DefaultAnimationSystemNameAnimBP };

	if (IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanCharacterBuildExtender::FeatureName))
	{
		TArray<IMetaHumanCharacterBuildExtender*> Extenders = IModularFeatures::Get().GetModularFeatureImplementations<IMetaHumanCharacterBuildExtender>(IMetaHumanCharacterBuildExtender::FeatureName);
		for (IMetaHumanCharacterBuildExtender* Extender : Extenders)
		{
			Options += Extender->GetAnimationSystemOptions();
		}
	}

	return Options;
}

bool UMetaHumanCharacterEditorPipelineToolProperties::InitializeAnimationSystemNameVisibility() const
{
	return ((PipelineType == EMetaHumanDefaultPipelineType::Cinematic || PipelineType == EMetaHumanDefaultPipelineType::Optimized) &&
		(GetAnimationSystemOptions().Num() > 1));
}

void UMetaHumanCharacterEditorPipelineTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("AssemblyToolName", "Assembly"));

	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	PropertyObject = NewObject<UMetaHumanCharacterEditorPipelineToolProperties>(this);
	PropertyObject->DefaultName = Character->GetName();
	AddToolPropertySource(PropertyObject);

	PreviousAssemblySettings = Character->AssemblySettings;
	PropertyObject->CopyFrom(PreviousAssemblySettings);
	PropertyObject->UpdateSelectedPipeline();

	PropertyObject->RestoreProperties(this, Character->GetName());
}

void UMetaHumanCharacterEditorPipelineTool::Shutdown(EToolShutdownType ShutdownType)
{
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	PropertyObject->SaveProperties(this, Character->GetName());

	FMetaHumanCharacterAssemblySettings CurrentAssemblySettings;
	PropertyObject->CopyTo(CurrentAssemblySettings);

	Character->Modify();
	Character->AssemblySettings = CurrentAssemblySettings;

	// Add the undo command
	TUniquePtr<FPipelineToolCommandChange> CommandChange = MakeUnique<FPipelineToolCommandChange>(PreviousAssemblySettings, CurrentAssemblySettings, GetToolManager());
	GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("PipelineToolCommandChangeTransaction", "Edit Assembly"));
	
	PropertyObject->OnPipelineSelectionChanged.Unbind();
}

void UMetaHumanCharacterEditorPipelineTool::OnPropertyModified(UObject* InPropertySet, FProperty* InProperty)
{
	if (InPropertySet == PropertyObject)
	{
		if (UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target))
		{
			FMetaHumanCharacterAssemblySettings NewAssemblySettings;
			PropertyObject->CopyTo(NewAssemblySettings);

			TUniquePtr<FPipelineToolCommandChange> CommandChange = MakeUnique<FPipelineToolCommandChange>(PreviousAssemblySettings, NewAssemblySettings, GetToolManager());
			GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("PipelineToolCommandChange", "Edit Assembly"));

			PreviousAssemblySettings = NewAssemblySettings;
		}
	}
}

bool UMetaHumanCharacterEditorPipelineTool::CanBuild(FText& OutErrorMsg) const
{
	if (UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target))
	{
		const bool bCanBuildCharacter = UMetaHumanCharacterEditorSubsystem::Get()->CanBuildMetaHuman(Character, OutErrorMsg);
		bool bCanBuildPipeline = true;

		if (UMetaHumanCollectionEditorPipeline* SelectedPipeline = PropertyObject->GetSelectedEditorPipeline())
		{
			bCanBuildPipeline = SelectedPipeline->CanBuild();

			if (!bCanBuildPipeline)
			{
				OutErrorMsg = LOCTEXT("CantBuildPipeline", "Selected pipeline can't be built. Please check that all values are correct and valid");
			}
		}

		return bCanBuildCharacter && bCanBuildPipeline;
	}

	return false;
}

void UMetaHumanCharacterEditorPipelineTool::Build() const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	if (Character)
	{
		FPlatformMemoryStats Stats;

		FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
		constexpr uint64 GB = 1024 * 1024 * 1024;
		if (MemoryStats.AvailableVirtual < (10 * GB))
		{
			const FText NotEnoughMemoryTitle = LOCTEXT("NotEnoughMemoryDialogTitle", "Not enough memory to assemble MetaHuman");
			const FText NotEnoughMemoryMessage = FText::Format(LOCTEXT("PipelineNotEnoughMemoryDialogMessage", "Assembling a MetaHuman Character requires at least 10 GiB of free memory but only {0} is available.\n"
															   "If you proceed the editor might crash. Would you like to continue?"),
															   FText::AsMemory(MemoryStats.AvailableVirtual));

			FSuppressableWarningDialog::FSetupInfo SetupInfo(NotEnoughMemoryMessage, NotEnoughMemoryTitle, TEXT("MetaHumanCharacterSupressNotEnoughMemory"));
			SetupInfo.ConfirmText = LOCTEXT("PipelineNotEnoughMemoryDialogConfirmText", "Yes");
			SetupInfo.CancelText = LOCTEXT("PipelineNotEnoughMemoryDialogCancelText", "Cancel");

			const FSuppressableWarningDialog NotEnoughMemoryDialog{ SetupInfo };

			const FSuppressableWarningDialog::EResult Result = NotEnoughMemoryDialog.ShowModal();
			if (Result == FSuppressableWarningDialog::EResult::Cancel)
			{
				return;
			}
		}

		const FMetaHumanCharacterEditorBuildParameters BuildParams = PropertyObject->InitBuildParameters();
		UMetaHumanCharacterEditorSubsystem::Get()->BuildMetaHuman(Character, BuildParams);
	}
}

#undef LOCTEXT_NAMESPACE 