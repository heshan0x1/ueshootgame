// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SMetaHumanAuthenticationMenuButton.h"

#include "Cloud/MetaHumanServiceRequest.h"
#include "EditorDialogLibrary.h"
#include "Styling/StyleColors.h"
#include "ToolMenus.h"
#include "UI/MetaHumanStyleSet.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMetaHumanAuthenticationMenuButton"

const FName SMetaHumanAuthenticationMenuButton::AuthenticationMenuName(TEXT("MetaHumanSDKEditor.AuthenticationMenuName"));

namespace UE::MetaHuman::Private
{
	static constexpr float MenuButtonSize = 34.f;

	static const FText NoUserSignedInText = LOCTEXT("AuthenticationMenu_NoUserLabel", "No user signed in");
	static const FText NoUserIDText = LOCTEXT("AuthenticationMenu_NoIDLabel", "please autorig to trigger log-in flow");
	static const FText NoUserSignedInToolTipText = LOCTEXT("AuthenticationMenu_NoUserToolTipText", "No user signed in, please autorig to trigger log-in flow");
	static const FText WindowTitle = LOCTEXT("AuthenticationMenu_WindowTitle", "MetaHuman Cloud Authentication");
	
	static FText CachedAccountUsernameText = NoUserSignedInText;
	static FText CachedAccountIDText = NoUserIDText;
}

void SMetaHumanAuthenticationMenuButton::Construct(const FArguments& InArgs)
{
	using namespace UE::MetaHuman::Private;
	CachedAccountUsernameText = NoUserSignedInText;
	CachedAccountIDText = NoUserIDText;

	ChildSlot
		[
			SNew(SBox)
			.WidthOverride(MenuButtonSize)
			.HeightOverride(MenuButtonSize)
			[
				SNew(SComboButton)
				.HasDownArrow(false)
				.ButtonStyle(FAppStyle::Get(), TEXT("HoverHintOnly"))
				.OnGetMenuContent(this, &SMetaHumanAuthenticationMenuButton::MakeAuthenticationMenuOptionsWidget)
				.ToolTipText(LOCTEXT("AuthenticationMenuButton_ToolTipText", "Open the Authentication menu"))
				.ButtonContent()
				[
					SNew(SBox)
					.WidthOverride(MenuButtonSize)
					.HeightOverride(MenuButtonSize)
					[
						SNew(SImage)
						.Image(UE::MetaHuman::FMetaHumanStyleSet::Get().GetBrush("UserIcon"))
					]
				]
			]
		];
}

TSharedRef<SWidget> SMetaHumanAuthenticationMenuButton::MakeAuthenticationMenuOptionsWidget()
{
	using namespace UE::MetaHuman::ServiceAuthentication;
	using namespace UE::MetaHuman::Private;

	CheckHasLoggedInUserAsync(FOnCheckHasLoggedInUserCompleteDelegate::CreateLambda(
			[](bool bSignedIn, FString InAccountIdString, FString InAccountUserName)
			{
				CachedAccountUsernameText = bSignedIn ? FText::FromString(InAccountUserName).ToUpper() : UE::MetaHuman::Private::NoUserSignedInText;
				CachedAccountIDText = bSignedIn ? FText::FromString(InAccountIdString).ToUpper() : UE::MetaHuman::Private::NoUserIDText;
			}));

	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(AuthenticationMenuName))
	{
		UToolMenu* Menu = ToolMenus->RegisterMenu(AuthenticationMenuName);
		FToolMenuSection& Section = Menu->AddSection("Authentication", FText::GetEmpty());
		Section.AddEntry
		(
			FToolMenuEntry::InitWidget
			(
				TEXT("AccountUsernameText"),
				MakeAccountUsernameTextWidget(),
				FText::GetEmpty()
			)
		);

		Section.AddEntry
		(
			FToolMenuEntry::InitWidget
			(
				TEXT("AccountIDText"),
				MakeAccountIDTextWidget(),
				FText::GetEmpty()
			)
		);

		Section.AddSeparator(NAME_None);

		Section.AddEntry
		(
			FToolMenuEntry::InitWidget
			(
				TEXT("SignOutButton"),
				MakeSignOutButtonWidget(),
				FText::GetEmpty()
			)
		);
	}

	FToolMenuContext Context;
	return ToolMenus->GenerateWidget(AuthenticationMenuName, Context);
}

TSharedRef<SWidget> SMetaHumanAuthenticationMenuButton::MakeAccountUsernameTextWidget() const
{
	using namespace UE::MetaHuman::Private;

	return
		SNew(SBox)
		.HAlign(HAlign_Center)
		.Padding(8.f, 8.f, 8.f, 4.f)
		[
			SNew(STextBlock)
			.Font(UE::MetaHuman::FMetaHumanStyleSet::Get().GetFontStyle(TEXT("MetaHumanSDKEditor.AuthenticationMenuFont")))
			.ColorAndOpacity(FStyleColors::AccentGray)
			.Text_Lambda([](){ return CachedAccountUsernameText; })
			.ToolTipText_Lambda([]()
				{
					const FText UsernameText = 
						CachedAccountUsernameText.ToString() != NoUserSignedInText.ToString() ? 
						CachedAccountUsernameText : 
						NoUserSignedInToolTipText;

					return
						FText::Format(LOCTEXT("AccountUsernameTextBlock_ToolTipText", "Account Username: {0}"), UsernameText);
				})
		];
}

TSharedRef<SWidget> SMetaHumanAuthenticationMenuButton::MakeAccountIDTextWidget() const
{
	using namespace UE::MetaHuman::Private;

	return
		SNew(SBox)
		.HAlign(HAlign_Center)
		.Padding(8.f, 4.f, 8.f, 8.f)
		[
			SNew(STextBlock)
			.Font(UE::MetaHuman::FMetaHumanStyleSet::Get().GetFontStyle(TEXT("MetaHumanSDKEditor.AuthenticationMenuFont")))
			.ColorAndOpacity(FStyleColors::AccentGray)
			.Text_Lambda([]() { return CachedAccountIDText; })
			.ToolTipText_Lambda([]()
				{
					const FText IDText =
						CachedAccountIDText.ToString() != NoUserIDText.ToString() ?
						CachedAccountIDText :
						NoUserSignedInToolTipText;

					return
						FText::Format(LOCTEXT("AccountIDTextBlock_ToolTipText", "Account ID: {0}"), IDText);
				})
		];
}

TSharedRef<SWidget> SMetaHumanAuthenticationMenuButton::MakeSignOutButtonWidget() const
{
	return
		SNew(SBox)
		.Padding(8.f, 4.f)
		.MinDesiredWidth(240.f)
		[
			SNew(SButton)
			.ButtonStyle(UE::MetaHuman::FMetaHumanStyleSet::Get(), TEXT("MetaHumanSDKEditor.AuthenticationMenuButton"))
			.ToolTipText(LOCTEXT("SignOutButton_ToolTipText", "Sign out of your account"))
			.ForegroundColor(FStyleColors::AccentWhite)
			.OnClicked(this, &SMetaHumanAuthenticationMenuButton::OnSignOutButtonClicked)
			.IsEnabled(this, &SMetaHumanAuthenticationMenuButton::IsSignOutButtonEnabled)
			[
				SNew(SBox)
				.Padding(FMargin(4.f, 6.f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SignOutButton_Label", "Sign Out"))
				]
			]
		];
}

FReply SMetaHumanAuthenticationMenuButton::OnSignOutButtonClicked() const
{
	using namespace UE::MetaHuman::ServiceAuthentication;
	CheckHasLoggedInUserAsync(FOnCheckHasLoggedInUserCompleteDelegate::CreateLambda(
		[](bool bSignedIn, FString InAccountIdString, FString InAccountUserName)
		{
			using namespace UE::MetaHuman::Private;
			if (bSignedIn)
			{
				EAppReturnType::Type Result = UEditorDialogLibrary::ShowMessage(WindowTitle,
					FText::Format(LOCTEXT("MetaHuman.Cloud.Authentication.SignedIn", "{0} (ID {1}) is signed in\nSign out?"), FText::FromString(InAccountUserName), FText::FromString(InAccountIdString)), EAppMsgType::OkCancel);
				if (Result == EAppReturnType::Ok)
				{
					LogoutFromAuthEnvironment(FOnLogoutCompleteDelegate::CreateLambda(
						[InAccountIdString] 
						{
							UEditorDialogLibrary::ShowMessage(WindowTitle,
								FText::Format(LOCTEXT("MetaHuman.Cloud.Authentication.SignedOut", "Account ID {0} signed out"), FText::FromString(InAccountIdString)), EAppMsgType::Ok);
						}));
				}
			}
			else
			{
				UEditorDialogLibrary::ShowMessage(WindowTitle, NoUserSignedInToolTipText, EAppMsgType::Ok);
			}
		}));

	return FReply::Handled();
}

bool SMetaHumanAuthenticationMenuButton::IsSignOutButtonEnabled() const
{
	using namespace UE::MetaHuman::Private;
	const bool bIsUserSignedOut =
		CachedAccountUsernameText.ToString() == NoUserSignedInText.ToString() &&
		CachedAccountIDText.ToString() == NoUserIDText.ToString();

	return !bIsUserSignedOut;
}

#undef LOCTEXT_NAMESPACE
