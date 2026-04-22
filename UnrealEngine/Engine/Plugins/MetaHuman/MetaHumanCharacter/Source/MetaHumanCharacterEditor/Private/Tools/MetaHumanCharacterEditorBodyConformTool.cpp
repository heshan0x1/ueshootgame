// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorBodyConformTool.h"
#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorToolTargetUtil.h"
#include "MetaHumanCharacterPaletteEditorModule.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "InteractiveToolManager.h"
#include "ToolTargetManager.h"
#include "DNAUtils.h"
#include "Logging/MessageLog.h"
#include "Misc/ScopedSlowTask.h"
#include "AssetRegistry/AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorBodyConformTool"

class FBodyConformToolStateCommandChange : public FToolCommandChange
{
public:

	FBodyConformToolStateCommandChange(
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldState,
		TNotNull<UMetaHumanCharacter*> InCharacter,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldState{ InOldState }
	, NewState{ UMetaHumanCharacterEditorSubsystem::Get()->CopyBodyState(InCharacter) }
	, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual void Apply(UObject* InObject) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, NewState);
	}

	virtual void Revert(UObject* InObject) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, OldState);
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		// If the ToolManager is not valid anymore it means the asset editor was closed so mark the transaction as expired
		return !ToolManager.IsValid();
	}
	//~End FToolCommandChange interface


protected:

	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> OldState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> NewState;

	TWeakObjectPtr<UInteractiveToolManager> ToolManager;

};

class FBodyConformToolDNACommandChange : public FToolCommandChange
{
public:

	FBodyConformToolDNACommandChange(
		const TArray<uint8>& InOldDNABuffer,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldState,
		TNotNull<UMetaHumanCharacter*> InCharacter,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldDNABuffer{ InOldDNABuffer }
		, NewDNABuffer{ InCharacter->GetBodyDNABuffer() }
		, OldState{ InOldState }
		, NewState{ UMetaHumanCharacterEditorSubsystem::Get()->CopyBodyState(InCharacter) }
		, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual void Apply(UObject* InObject) override
	{
		ApplyChange(InObject, NewDNABuffer, NewState);
	}

	virtual void Revert(UObject* InObject) override
	{
		ApplyChange(InObject, OldDNABuffer, OldState);
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		// If the ToolManager is not valid anymore it means the asset editor was closed so mark the transaction as expired
		return !ToolManager.IsValid();
	}
	//~End FToolCommandChange interface


protected:

	void ApplyChange(UObject* InObject, const TArray<uint8>& InDNABuffer, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InState)
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

		// if an empty buffer, remove the rig from the character (special case)
		if (InDNABuffer.IsEmpty())
		{
			UMetaHumanCharacterEditorSubsystem::Get()->RemoveBodyRig(Character);
		}
		else
		{
			TArray<uint8> BufferCopy;
			BufferCopy.SetNumUninitialized(InDNABuffer.Num());
			FMemory::Memcpy(BufferCopy.GetData(), InDNABuffer.GetData(), InDNABuffer.Num());
			constexpr bool bImportingAsFixedBodyType = true;
			UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyDNA(Character, ReadDNAFromBuffer(&BufferCopy, EDNADataLayer::All).ToSharedRef(), bImportingAsFixedBodyType);
		}

		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, InState, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
	}

	TArray<uint8> OldDNABuffer;
	TArray<uint8> NewDNABuffer;

	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> OldState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> NewState;


	TWeakObjectPtr<UInteractiveToolManager> ToolManager;

};

UInteractiveTool* UMetaHumanCharacterEditorBodyConformToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	UMetaHumanCharacterEditorBodyConformTool* ConformTool = NewObject<UMetaHumanCharacterEditorBodyConformTool>(InSceneState.ToolManager);
	ConformTool->SetTarget(Target);

	return ConformTool;
}

const FToolTargetTypeRequirements& UMetaHumanCharacterEditorBodyConformToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements(
		TArray<const UClass*>
		{
			UPrimitiveComponentBackedTarget::StaticClass(),
		}
	);

	return TypeRequirements;
}

void UMetaHumanCharacterImportBodySubToolBase::DisplayConformError(const FText& ErrorMessageText) const
{
	UInteractiveTool* OwnerTool = GetTypedOuter<UInteractiveTool>();
	check(OwnerTool);

	OwnerTool->GetToolManager()->DisplayMessage(ErrorMessageText, EToolMessageLevel::UserError);
	FMessageLog(UE::MetaHuman::MessageLogName).Error(ErrorMessageText);
	FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, false);
}

bool UMetaHumanCharacterImportBodyDNAProperties::CanConform() const
{
	return !BodyDNAFile.FilePath.IsEmpty(); 
}

void UMetaHumanCharacterImportBodyDNAProperties::Conform()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ConformDnaTaskMessage", "Conforming Body from DNA"));
	ImportTask.MakeDialog();
	
	TNotNull < UMetaHumanCharacterEditorBodyConformTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyConformTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TArray<FVector3f> OutVertices;
	TArray<FVector3f> JointRotations;
	TSharedPtr<IDNAReader> BodyDNAReader =  ReadDNAFromFile(BodyDNAFile.FilePath, EDNADataLayer::All);
	EImportErrorCode ErrorCode = EImportErrorCode::Success;
	if (!BodyDNAReader.IsValid())
	{
		ErrorCode = EImportErrorCode::InvalidInputData;
	}

	TSharedPtr<IDNAReader> HeadDNAReader;
	if (ErrorCode == EImportErrorCode::Success && !HeadDNAFile.FilePath.IsEmpty())
	{
		HeadDNAReader =  ReadDNAFromFile(HeadDNAFile.FilePath, EDNADataLayer::All);
		if (!HeadDNAReader.IsValid())
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}
	if (ErrorCode == EImportErrorCode::Success)
	{
		TArray<FVector3f> JointTranslations;
		ErrorCode = Subsystem->GetJointsForBodyConforming(BodyDNAReader.ToSharedRef(), JointTranslations, JointRotations);
	}
	if (ErrorCode == EImportErrorCode::Success)
	{
		ErrorCode = Subsystem->GetMeshForBodyConforming(Character, BodyDNAReader.ToSharedRef(), HeadDNAReader, OutVertices);
	}
	if (ErrorCode != EImportErrorCode::Success)
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText =  LOCTEXT("FailedToConformBody", "Failed to conform body mesh");
		}
		DisplayConformError(ErrorMessageText);
	}
	else
	{
		Subsystem->ConformBody( Character, OutVertices, JointRotations, ImportOptions.bTargetIsInMetaHumanAPose, ImportOptions.bEstimateJointsFromMesh);
		TUniquePtr<FBodyConformToolStateCommandChange> CommandChange = MakeUnique<FBodyConformToolStateCommandChange>(
		OwnerTool->GetOriginalState(),
		Character,
		OwnerTool->GetToolManager());
		// update original state in tool so undo works as expected
		OwnerTool->UpdateOriginalState();
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolConformDNACommandChangeUndo", "Body conform"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
}

bool UMetaHumanCharacterImportBodyDNAProperties::CanImportMesh() const
{
	return !BodyDNAFile.FilePath.IsEmpty(); 
}

void UMetaHumanCharacterImportBodyDNAProperties::ImportMesh()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportMeshDnaTaskMessage", "Importing Body Mesh from DNA"));
	ImportTask.MakeDialog();
	
	TNotNull < UMetaHumanCharacterEditorBodyConformTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyConformTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TArray<FVector3f> OutVertices;
	TSharedPtr<IDNAReader> BodyDNAReader =  ReadDNAFromFile(BodyDNAFile.FilePath, EDNADataLayer::All);
	EImportErrorCode ErrorCode = EImportErrorCode::Success;
	if (!BodyDNAReader.IsValid())
	{
		ErrorCode = EImportErrorCode::InvalidInputData;
	}
	if (ErrorCode == EImportErrorCode::Success)
	{
		TSharedPtr<IDNAReader> HeadDNAReader =  ReadDNAFromFile(HeadDNAFile.FilePath, EDNADataLayer::All);
		if (HeadDNAReader.IsValid() && !HeadDNAFile.FilePath.IsEmpty())
		{
			ErrorCode = Subsystem->GetMeshForBodyConforming(Character, BodyDNAReader.ToSharedRef(), HeadDNAReader, OutVertices);
		}
		else
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}

	if (ErrorCode == EImportErrorCode::Success && Subsystem->SetBodyMesh( Character, OutVertices, ImportOptions.bAutoRigHelperJoints))
	{
		// Add command change to undo stack
		TUniquePtr<FBodyConformToolStateCommandChange> CommandChange = MakeUnique<FBodyConformToolStateCommandChange>(
			OwnerTool->GetOriginalState(),
			Character,
			OwnerTool->GetToolManager());
		// update original state in tool so undo works as expected
		OwnerTool->UpdateOriginalState();
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolImportMeshDNACommandChangeUndo", "Body mesh import"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText =  LOCTEXT("FailedToImportBodyMesh", "Failed to import body mesh");
		}

		DisplayConformError(ErrorMessageText);
	}

}

bool UMetaHumanCharacterImportBodyDNAProperties::CanImportJoints() const
{
	return !BodyDNAFile.FilePath.IsEmpty(); 
}

void UMetaHumanCharacterImportBodyDNAProperties::ImportJoints()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportJointsDnaTaskMessage", "Importing Body Joints from DNA"));
	ImportTask.MakeDialog();
	
	TNotNull < UMetaHumanCharacterEditorBodyConformTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyConformTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TArray<FVector3f> OutVertices;
	TSharedPtr<IDNAReader> BodyDNAReader =  ReadDNAFromFile(BodyDNAFile.FilePath, EDNADataLayer::All);
	EImportErrorCode ErrorCode = EImportErrorCode::Success;
	if (!BodyDNAReader.IsValid())
	{
		ErrorCode = EImportErrorCode::InvalidInputData;
	}

	TArray<FVector3f> JointRotations;
	TArray<FVector3f> JointTranslations;
	if (ErrorCode == EImportErrorCode::Success)
	{
		ErrorCode = Subsystem->GetJointsForBodyConforming(BodyDNAReader.ToSharedRef(), JointTranslations, JointRotations);	
	}
	
	if (ErrorCode == EImportErrorCode::Success && Subsystem->SetBodyJoints( Character, JointTranslations, JointRotations, ImportOptions.bImportHelperJoints))
	{
		// Add command change to undo stack
		TUniquePtr<FBodyConformToolStateCommandChange> CommandChange = MakeUnique<FBodyConformToolStateCommandChange>(
			OwnerTool->GetOriginalState(),
			Character,
			OwnerTool->GetToolManager());
		// update original state in tool so undo works as expected
		OwnerTool->UpdateOriginalState();
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolImportJointsDNACommandChangeUndo", "Body Conform bones import"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText = LOCTEXT("FailedToImportBodyJoints", "Failed to import body bones");
		}
		DisplayConformError(ErrorMessageText);
	}
}


bool UMetaHumanCharacterImportBodyDNAProperties::CanImportWholeRig() const
{
	return !BodyDNAFile.FilePath.IsEmpty(); 
}

void UMetaHumanCharacterImportBodyDNAProperties::ImportWholeRig()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportWholeRigDnaTaskMessage", "Importing Body Whole Rig from DNA"));
	ImportTask.MakeDialog();
	
	TNotNull < UMetaHumanCharacterEditorBodyConformTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyConformTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	EImportErrorCode ErrorCode = EImportErrorCode::Success;

	TSharedPtr<IDNAReader> BodyDNAReader =  ReadDNAFromFile(BodyDNAFile.FilePath, EDNADataLayer::All);
	if (BodyDNAReader.IsValid())
	{
		if (HeadDNAFile.FilePath.IsEmpty())
		{
			ErrorCode = Subsystem->ImportBodyWholeRig(Character, BodyDNAReader.ToSharedRef(), nullptr);
		}
		else
		{
			if (TSharedPtr<IDNAReader> HeadDNAReader =  ReadDNAFromFile(HeadDNAFile.FilePath, EDNADataLayer::All); HeadDNAReader.IsValid())
			{
				ErrorCode = Subsystem->ImportBodyWholeRig(Character, BodyDNAReader.ToSharedRef(), HeadDNAReader.ToSharedRef());
			}
			else
			{
				ErrorCode = EImportErrorCode::InvalidInputData;
			}
		}
	}
	else
	{
		ErrorCode = EImportErrorCode::InvalidInputData;
	}

	if (ErrorCode == EImportErrorCode::Success)
	{
		// Add command change to undo stack
		TUniquePtr<FBodyConformToolDNACommandChange> CommandChange = MakeUnique<FBodyConformToolDNACommandChange>(
							OwnerTool->GetOriginalDNABuffer(),
							OwnerTool->GetOriginalState(),
							Character,
							OwnerTool->GetToolManager());
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolDNAWholeRigCommandChangeUndo", "Body DNA Import Whole Rig"));
			
		// update original state and DNA in tool so undo works as expected
		OwnerTool->UpdateOriginalState();
		OwnerTool->UpdateOriginalDNABuffer();

		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText =  LOCTEXT("FailedToImportBodyWholeRig", "Failed to import DNA as whole rig");
		}
		DisplayConformError(ErrorMessageText);
	}
}

bool UMetaHumanCharacterImportBodyDNAProperties::GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const
{
	switch (InErrorCode)
	{
	case EImportErrorCode::InvalidInputData:
		OutErrorMessage = LOCTEXT("FailedToImportBodyDNAInvalidInputData", "Failed to import DNA: input mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidInputBones:
		OutErrorMessage = LOCTEXT("FailedToImportBodyDNAInvalidInputBones", "Failed to import DNA: input mesh bones are not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidHeadMesh:
		OutErrorMessage = LOCTEXT("FailedToImportBodyDNAInvalidHeadMesh", "Failed to import head DNA: input mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::CombinedBodyCannotBeImportedAsWholeRig:
		OutErrorMessage = LOCTEXT("FailedToImportBodyDNACombinedAsWholeRig", "Failed to import DNA: can not import combined head and body mesh as body whole rig");
		break;
	default:
		return false;
	}

	return true;
}

bool UMetaHumanCharacterImportBodyTemplateProperties::CanConform() const
{
	return BodyMesh.ToSoftObjectPath().IsValid();
}

void UMetaHumanCharacterImportBodyTemplateProperties::Conform()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ConformTemplateTaskMessage", "Conforming Body from Template"));
	ImportTask.MakeDialog();
	
	TNotNull < UMetaHumanCharacterEditorBodyConformTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyConformTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TArray<FVector3f> OutVertices;
	TArray<FVector3f> JointRotations;

	EImportErrorCode ErrorCode = Subsystem->GetMeshForBodyConforming(Character, BodyMesh.LoadSynchronous(), HeadMesh.LoadSynchronous(), bMatchVerticesByUVs, OutVertices);
	if (ErrorCode == EImportErrorCode::Success)
	{
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(BodyMesh.Get());
		if (SkeletalMesh != nullptr)
		{
			TArray<FVector3f> JointTranslations;
			ErrorCode = Subsystem->GetJointsForBodyConforming(SkeletalMesh, JointTranslations, JointRotations);
		}
	}
	if (ErrorCode != EImportErrorCode::Success)
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText =  LOCTEXT("FailedToConformBodyTemplate", "Failed to conform the body");
		}
		DisplayConformError(ErrorMessageText);
	}
	else
	{
		Subsystem->ConformBody( Character, OutVertices, JointRotations, ConformBodyParams.bTargetIsInMetaHumanAPose, ConformBodyParams.bEstimateJointsFromMesh);
		TUniquePtr<FBodyConformToolStateCommandChange> CommandChange = MakeUnique<FBodyConformToolStateCommandChange>(
		OwnerTool->GetOriginalState(),
		Character,
		OwnerTool->GetToolManager());
		// update original state in tool so undo works as expected
		OwnerTool->UpdateOriginalState();
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolConformTemplateCommandChangeUndo", "Body bones import"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
}

bool UMetaHumanCharacterImportBodyTemplateProperties::CanImportMesh() const
{
	return BodyMesh.ToSoftObjectPath().IsValid();
}

void UMetaHumanCharacterImportBodyTemplateProperties::ImportMesh()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportTemplateTaskMessage", "Import Body Mesh from Template"));
	ImportTask.MakeDialog();
	
	TNotNull < UMetaHumanCharacterEditorBodyConformTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyConformTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TArray<FVector3f> OutVertices;
	EImportErrorCode ErrorCode = Subsystem->GetMeshForBodyConforming(Character, BodyMesh.LoadSynchronous(), HeadMesh.LoadSynchronous(), bMatchVerticesByUVs, OutVertices);
	if (ErrorCode == EImportErrorCode::Success && Subsystem->SetBodyMesh( Character, OutVertices, ConformBodyParams.bAutoRigHelperJoints))
	{
		// Add command change to undo stack
		TUniquePtr<FBodyConformToolStateCommandChange> CommandChange = MakeUnique<FBodyConformToolStateCommandChange>(
			OwnerTool->GetOriginalState(),
			Character,
			OwnerTool->GetToolManager());
		// update original state in tool so undo works as expected
		OwnerTool->UpdateOriginalState();
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolImportMeshTemplateCommandChangeUndo", "Body mesh import"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText =  LOCTEXT("FailedToImportBodyMeshFromTemplate", "Failed to import body mesh");
		}
		DisplayConformError(ErrorMessageText);
	}
}

bool UMetaHumanCharacterImportBodyTemplateProperties::CanImportJoints() const
{
	if (!BodyMesh.ToSoftObjectPath().IsValid())
	{
		return false;
	}
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(BodyMesh.ToSoftObjectPath());
	if (!AssetData.IsValid())
	{
		return false;
	}
	return AssetData.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName();
}

void UMetaHumanCharacterImportBodyTemplateProperties::ImportJoints()
{
	const float ImportWorkProgress = 2.0f;
	FScopedSlowTask ImportTask(ImportWorkProgress, LOCTEXT("ImportJointsTemplateTaskMessage", "Import Body Joints from Template"));
	ImportTask.MakeDialog();
	
	TNotNull < UMetaHumanCharacterEditorBodyConformTool*> OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyConformTool>();
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TArray<FVector3f> JointTranslations;
	TArray<FVector3f> JointRotations;
	USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(BodyMesh.LoadSynchronous());
	EImportErrorCode ErrorCode = Subsystem->GetJointsForBodyConforming(SkelMesh, JointTranslations, JointRotations);
	
	if (ErrorCode == EImportErrorCode::Success && Subsystem->SetBodyJoints( Character, JointTranslations, JointRotations, ConformBodyParams.bImportHelperJoints))
	{
		// Add command change to undo stack
		TUniquePtr<FBodyConformToolStateCommandChange> CommandChange = MakeUnique<FBodyConformToolStateCommandChange>(
			OwnerTool->GetOriginalState(),
			Character,
			OwnerTool->GetToolManager());
		// update original state in tool so undo works as expected
		OwnerTool->UpdateOriginalState();
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("BodyConformToolImportJointsTemplateCommandChangeUndo", "Body bones import"));
		// make sure we clear any errors
		OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	}
	else
	{
		FText ErrorMessageText;
		if (!GetErrorMessageText(ErrorCode, ErrorMessageText))
		{
			ErrorMessageText =  LOCTEXT("FailedToImportBodyJointsFromTemplate", "Failed to import body joints");
		}
		
		DisplayConformError(ErrorMessageText);
	}
}

bool UMetaHumanCharacterImportBodyTemplateProperties::GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const
{
	switch (InErrorCode)
	{
	case EImportErrorCode::InvalidInputData:
		OutErrorMessage = LOCTEXT("FailedToImportBodyTemplateInvalidInputData", "Failed to import Template Mesh: input mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidInputBones:
		OutErrorMessage = LOCTEXT("FailedToImportBodyTemplateInvalidInputBones", "Failed to import Template Mesh: input mesh bones are not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidHeadMesh:
		OutErrorMessage = LOCTEXT("FailedToImportBodyTemplateInvalidHeadMesh", "Failed to import head Template Mesh: input mesh is not consistent with MetaHuman topology");
		break;
	default:
		return false;
	}

	return true;
}

void UMetaHumanCharacterEditorBodyConformTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("BodyConformToolName", "Conform"));

	// Save the original state to restored in case the tool is cancelled
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	OriginalState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OriginalDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();

	ImportDNAProperties = NewObject<UMetaHumanCharacterImportBodyDNAProperties>(this);
	ImportDNAProperties->RestoreProperties(this);
	
	ImportTemplateProperties = NewObject<UMetaHumanCharacterImportBodyTemplateProperties>(this);
	ImportTemplateProperties->RestoreProperties(this);

	const FMetaHumanCharacterEditorToolCommands& Commands = FMetaHumanCharacterEditorToolCommands::Get();
	SubTools->RegisterSubTools(
	{
		{ Commands.BeginBodyConformImportBodyDNATool, ImportDNAProperties },
		{ Commands.BeginBodyConformImportBodyTemplateTool, ImportTemplateProperties },
	});
}

void UMetaHumanCharacterEditorBodyConformTool::UpdateOriginalState()
{
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	OriginalState = Subsystem->CopyBodyState(MetaHumanCharacter);
}


void UMetaHumanCharacterEditorBodyConformTool::UpdateOriginalDNABuffer()
{
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	OriginalState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OriginalDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();
}



void UMetaHumanCharacterEditorBodyConformTool::Shutdown(EToolShutdownType InShutdownType)
{
	Super::Shutdown(InShutdownType);
	ImportDNAProperties->SaveProperties(this);
	ImportTemplateProperties->SaveProperties(this);
}

TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> UMetaHumanCharacterEditorBodyConformTool::GetOriginalState() const
{
	return OriginalState.ToSharedRef();
}

const TArray<uint8>& UMetaHumanCharacterEditorBodyConformTool::GetOriginalDNABuffer() const
{
	return OriginalDNABuffer;
}


#undef LOCTEXT_NAMESPACE