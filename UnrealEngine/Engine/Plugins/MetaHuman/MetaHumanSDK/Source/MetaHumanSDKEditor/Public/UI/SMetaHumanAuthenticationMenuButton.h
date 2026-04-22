// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

/** Widget used to display a combo button that displays MetaHuman authentication menu options. */
class SMetaHumanAuthenticationMenuButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanAuthenticationMenuButton)
		{}

	SLATE_END_ARGS()

	/** Constructs the widget. */
	METAHUMANSDKEDITOR_API void Construct(const FArguments& InArgs);

private:
	/** Makes the authentication menu options widget. */
	TSharedRef<SWidget> MakeAuthenticationMenuOptionsWidget();

	/** Makes the account username text label widget. . */
	TSharedRef<SWidget> MakeAccountUsernameTextWidget() const;

	/** Makes the account id text label widget. . */
	TSharedRef<SWidget> MakeAccountIDTextWidget() const;

	/** Makes the sign out button widget. */
	TSharedRef<SWidget> MakeSignOutButtonWidget() const;

	/** Called when the sign out but is clicked. */
	FReply OnSignOutButtonClicked() const;

	/** True if the sign out but is enabled. */
	bool IsSignOutButtonEnabled() const;

	/** The name identifier of the authentication menu. */
	static const FName AuthenticationMenuName;
};
