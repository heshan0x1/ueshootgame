// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

namespace UE::RigVMEditor
{
	struct FRigVMEditorRemoveUnusedMembersColumnID
	{
		/** The column to display the member name label */
		static const FName LabelRow;

		/** The column to display a checkbox to chose if the member should be removed */
		static const FName SelectedCheckBox;
	};
}
