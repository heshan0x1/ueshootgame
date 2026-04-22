// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PersonaAssetEditorToolkit.h"
#include "IHasPersonaToolkit.h"
#include "Features/IModularFeature.h"
#include "AssetDefinition.h"

class UPhysicsAsset;

/*-----------------------------------------------------------------------------
   IPhysicsAssetEditor
-----------------------------------------------------------------------------*/

class IPhysicsAssetEditor : public FPersonaAssetEditorToolkit, public IHasPersonaToolkit
{

};

class IPhysicsAssetEditorOverride : public IModularFeature
{
public:
	static const inline FName ModularFeatureName = "PhysicsAssetEditorOverride";

	virtual bool OpenAsset(UPhysicsAsset*) = 0;
};