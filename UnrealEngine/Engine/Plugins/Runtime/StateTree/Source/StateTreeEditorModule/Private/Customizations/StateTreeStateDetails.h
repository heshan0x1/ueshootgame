// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "EditorUndoClient.h"

class IDetailLayoutBuilder;
class IDetailChildrenBuilder;
class IDetailCategoryBuilder;
class IPropertyHandle;
class IPropertyUtilities;
class SWidget;
class SComboButton;

class FStateTreeStateDetails : public IDetailCustomization, FSelfRegisteringEditorUndoClient
{
private:
	using Super = IDetailCustomization;

public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void PendingDelete() override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:

	//~ FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;

	bool bIsPendingDelete = false;
};
