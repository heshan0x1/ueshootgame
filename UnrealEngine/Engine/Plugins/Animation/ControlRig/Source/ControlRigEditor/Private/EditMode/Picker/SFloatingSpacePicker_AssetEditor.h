// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRig.h"
#include "SFloatingSpacePicker_Base.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FControlRigEditMode;
class SRigSpacePickerWidget;

namespace UE::ControlRigEditor
{
/**
 * Handles displaying the floating space picker when directly editing control rigs,
 * i.e. when FControlRigEditMode::IsEditingControlRigDirectly() == true.
 */
class SFloatingSpacePicker_AssetEditor : public SFloatingSpacePicker_Base
{
public:
	
	void Construct(const FArguments& InArgs);

private:

	//~ Begin SFloatingSpacePicker_Base Interface
	virtual void OnActiveSpaceChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const FRigElementKey& InSpaceKey) override;
	virtual void OnSpaceListChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const TArray<FRigElementKeyWithLabel>& InSpaceList) override;
	//~ End SFloatingSpacePicker_Base Interface
};
}


