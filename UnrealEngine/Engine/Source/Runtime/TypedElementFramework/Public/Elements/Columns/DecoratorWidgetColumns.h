// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#include "DecoratorWidgetColumns.generated.h"

/**
 * Tag used to uniquely identify widget factories for decorator widgets
 */
USTRUCT(meta = (DisplayName = "Decorator Widget Factory"))
struct FEditorDataStorageUiDecoratorWidgetFactoryTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

namespace UE::Editor::DataStorage::Ui
{
	using FDecoratorWidgetFactoryTag = FEditorDataStorageUiDecoratorWidgetFactoryTag;
}