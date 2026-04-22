// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimDetailsOptionsMenuContext.generated.h"

namespace UE::ControlRigEditor { class SAnimDetailsOptions; }

/** Menu context for the anim details options menu */
UCLASS()
class UAnimDetailsOptionsMenuContext
	: public UObject
{
	GENERATED_BODY()

public:
	/** The anim details options widget that uses this menu context */
	TWeakPtr<UE::ControlRigEditor::SAnimDetailsOptions> WeakAnimDetailsOptions;
};
