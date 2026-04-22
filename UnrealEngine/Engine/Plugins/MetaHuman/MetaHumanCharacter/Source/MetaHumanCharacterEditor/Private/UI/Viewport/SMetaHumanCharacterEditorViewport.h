// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SAssetEditorViewport.h"

class SMetaHumanCharacterEditorViewport : public SAssetEditorViewport
{
public:

	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;

	TSharedPtr<class SComboButton> CustomEnvironmentSelectionBox;

	TSharedRef<class FMetaHumanCharacterViewportClient> GetMetaHumanCharacterEditorViewportClient() const;
};