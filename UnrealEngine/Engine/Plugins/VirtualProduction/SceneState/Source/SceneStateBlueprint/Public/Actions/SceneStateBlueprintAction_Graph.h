// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraphSchema_K2_Actions.h"
#include "Textures/SlateIcon.h"

#define UE_API SCENESTATEBLUEPRINT_API

namespace UE::SceneState::Graph
{

struct FBlueprintAction_Graph : public FEdGraphSchemaAction_K2Graph
{
	FBlueprintAction_Graph() = default;

	struct FArguments
	{
		UEdGraph* Graph = nullptr;
		EEdGraphSchemaAction_K2Graph::Type GraphType = EEdGraphSchemaAction_K2Graph::Graph;
		FText Category;
		FSlateIcon Icon;
		int32 Grouping = 0;
		int32 SectionID = 0;
	};
	UE_API explicit FBlueprintAction_Graph(const FArguments& InArgs);

	UE_API static FName StaticGetTypeId();

	//~ Begin FEdGraphSchemaAction
	UE_API virtual const FSlateBrush* GetPaletteIcon() const override;
	UE_API virtual FName GetTypeId() const override;
	UE_API virtual void MovePersistentItemToCategory(const FText& InNewCategory) override;
	UE_API virtual int32 GetReorderIndexInContainer() const override;
	UE_API virtual bool ReorderToBeforeAction(TSharedRef<FEdGraphSchemaAction> OtherAction) override;
	//~ End FEdGraphSchemaAction

private:
	FSlateIcon Icon;
};

} // UE::SceneState::Editor

#undef UE_API
