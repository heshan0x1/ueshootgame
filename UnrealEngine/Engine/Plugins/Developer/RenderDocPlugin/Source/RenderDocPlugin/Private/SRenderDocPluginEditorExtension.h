// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "SViewportToolBar.h"

class FRenderDocPluginModule;
class FExtensionBase;
class FExtensibilityManager;
class FExtender;
class FToolBarBuilder;

class FRenderDocPluginEditorExtension
{
public:
  FRenderDocPluginEditorExtension(FRenderDocPluginModule* ThePlugin);
  ~FRenderDocPluginEditorExtension();

private:
  void Initialize(FRenderDocPluginModule* ThePlugin);
};

#endif //WITH_EDITOR
