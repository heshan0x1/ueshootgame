// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/AvaInteractiveToolsActorToolBase.h"
#include "AvaDataLinkActorTool.generated.h"

UCLASS()
class UAvaDataLinkActorTool : public UAvaInteractiveToolsActorToolBase
{
	GENERATED_BODY()

public:
	UAvaDataLinkActorTool();

	//~ Begin UAvaInteractiveToolsToolBase
	virtual bool ShouldForceDefaultAction() const override;
	virtual bool SupportsDefaultAction() const override;
	virtual bool OnBegin() override;
	virtual void OnRegisterTool(IAvalancheInteractiveToolsModule* InAITModule) override;
	//~ End UAvaInteractiveToolsToolBase
};
