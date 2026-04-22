// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterFactoryNew.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterAnalytics.h"
#include "Editor/EditorEngine.h"

extern UNREALED_API UEditorEngine* GEditor;

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

UMetaHumanCharacterFactoryNew::UMetaHumanCharacterFactoryNew()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UMetaHumanCharacter::StaticClass();
}

UObject* UMetaHumanCharacterFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn)
{
	UMetaHumanCharacter* NewMetaHumanCharacter = NewObject<UMetaHumanCharacter>(InParent, InClass, InName, InFlags | RF_Transactional);

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterEditorSubsystem = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();
	MetaHumanCharacterEditorSubsystem->InitializeMetaHumanCharacter(NewMetaHumanCharacter);

	check(NewMetaHumanCharacter->IsCharacterValid());
	UE::MetaHuman::Analytics::RecordNewCharacterEvent(NewMetaHumanCharacter);

	return NewMetaHumanCharacter;
}

FText UMetaHumanCharacterFactoryNew::GetToolTip() const
{
	return LOCTEXT("MetaHumanCharacterFactory_ToolTip", 
		"MetaHuman Character Asset\n"
		"\nThe MetaHuman Character Asset holds all the information required build a MetaHuman.\n"
		"Any data that needs to be serialized for a MetaHuman should be stored in this class\n"
		"This class relies on the UMetaHumanCharacterEditorSubsystem to have its properties\n"
		"initialized and its basically a container for data associated with a MetaHuman");
}

#undef LOCTEXT_NAMESPACE
