// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SChaosVDRecordingControls.h"

#include "AsyncCompilationHelpers.h"
#include "ChaosVDEngine.h"
#include "ChaosVDModule.h"
#include "ChaosVDRecordingDetails.h"
#include "ChaosVDScene.h"
#include "ChaosVDSettingsManager.h"
#include "ChaosVDStyle.h"
#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"
#include "Editor.h"
#include "SChaosVDNameListPicker.h"
#include "SEnumCombo.h"
#include "SocketSubsystem.h"
#include "Input/Reply.h"
#include "Misc/MessageDialog.h"
#include "StatusBarSubsystem.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Chaos/ChaosVDEngineEditorBridge.h"
#include "Chaos/ChaosVDTraceRelayTransport.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/ScopedSlowTask.h"
#include "Settings/ChaosVDGeneralSettings.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SChaosVDMainTab.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SChaosVDRecordingControls)

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

class UChaosVDGeneralSettings;

const FName SChaosVDRecordingControls::RecordingControlsToolbarName = FName("ChaosVD.MainToolBar.RecordingControls");

void SChaosVDRecordingControls::Construct(const FArguments& InArgs, const TSharedRef<SChaosVDMainTab>& InMainTabSharedRef)
{
	MainTabWeakPtr = InMainTabSharedRef;
	StatusBarID = InMainTabSharedRef->GetStatusBarName();
	
	RecordingAnimation = FCurveSequence();
	RecordingAnimation.AddCurve(0.f, 1.5f, ECurveEaseFunction::Linear);

	ChildSlot
	[
		GenerateToolbarWidget()
	];

	CurrentSelectedSessionId = FChaosVDRemoteSessionsManager::LocalEditorSessionID;

	if (TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager())
	{
		RemoteSessionManager->OnSessionRecordingStarted().AddSP(this, &SChaosVDRecordingControls::HandleRecordingStart);
		RemoteSessionManager->OnSessionRecordingStopped().AddSP(this, &SChaosVDRecordingControls::HandleRecordingStop);
		RemoteSessionManager->OnSessionExpired().AddSP(this, &SChaosVDRecordingControls::CancelInFlightConnectionAttempt);
	}
}

TSharedRef<SWidget> SChaosVDRecordingControls::GenerateToggleRecordingStateButton(EChaosVDRecordingMode RecordingMode, const FText& StartRecordingTooltip)
{
	return SNew(SButton)
			.OnClicked(FOnClicked::CreateRaw(this, &SChaosVDRecordingControls::ToggleRecordingState, RecordingMode))
			.ForegroundColor(FSlateColor::UseForeground())
			.IsFocusable(false)
			.IsEnabled_Raw(this, &SChaosVDRecordingControls::IsRecordingToggleButtonEnabled, RecordingMode)
			.Visibility_Raw(this, &SChaosVDRecordingControls::IsRecordingToggleButtonVisible, RecordingMode)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.OnHovered_Lambda([this](){ bRecordingButtonHovered = true;})
			.OnUnhovered_Lambda([this](){ bRecordingButtonHovered = false;})
			.ToolTipText_Lambda([this, StartRecordingTooltip]()
			{
				return IsRecording() ? LOCTEXT("StopRecordButtonDesc", "Stop the current recording ") : StartRecordingTooltip;
			})
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(FMargin(0, 0, 0, 0))
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SImage)
					.Image_Raw(this, &SChaosVDRecordingControls::GetRecordOrStopButton, RecordingMode)
					.ColorAndOpacity_Lambda([this]()
					{
						if (IsRecording())
						{
							if (!RecordingAnimation.IsPlaying())
							{
								RecordingAnimation.Play(AsShared(), true);
							}

							const FLinearColor Color = bRecordingButtonHovered ? FLinearColor::Red : FLinearColor::White;
							return FSlateColor(bRecordingButtonHovered ? Color : Color.CopyWithNewOpacity(0.2f + 0.8f * RecordingAnimation.GetLerp()));
						}

						RecordingAnimation.Pause();
						return FSlateColor::UseSubduedForeground();
					})
				]
				+SHorizontalBox::Slot()
				.Padding(FMargin(4, 0, 0, 0))
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Visibility_Lambda([this](){return IsRecording() ? EVisibility::Collapsed : EVisibility::Visible;})
					.TextStyle(FAppStyle::Get(), "SmallButtonText")
					.Text_Lambda( [RecordingMode]()
					{
						return RecordingMode == EChaosVDRecordingMode::File ? LOCTEXT("RecordToFileButtonLabel", "Record To File") : LOCTEXT("RecordToLiveButtonLabel", "Record Live Session");
					})
				]
			];
}

FText SChaosVDRecordingControls::GetCurrentSelectedSessionName() const
{
	if (TSharedPtr<FChaosVDSessionInfo> CurrentSessionPtr = GetCurrentSessionInfo())
	{
		return FText::AsCultureInvariant(CurrentSessionPtr->SessionName);
	}

	static FText InvalidSessionName = LOCTEXT("InvalidSessionLabel", "Select a Session");

	return InvalidSessionName;
}

TSharedRef<SWidget> SChaosVDRecordingControls::GenerateTargetSessionSelector()
{
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.MenuPlacement(MenuPlacement_AboveAnchor).ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton"))
			.OnGetMenuContent(this, &SChaosVDRecordingControls::GenerateTargetSessionDropdown)
			.HasDownArrow(true)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Raw(this, &SChaosVDRecordingControls::GetCurrentSelectedSessionName)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		]];
}

TSharedRef<SWidget> SChaosVDRecordingControls::GenerateTargetSessionDropdown()
{
	using namespace Chaos::VisualDebugger;

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("CVDRecordingWidgetTargets", LOCTEXT("CVDRecordingTargetsMenu", "Available Targets"));
	{
		if (TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager())
		{
			RemoteSessionManager->EnumerateActiveSessions([this, &MenuBuilder](const TSharedRef<FChaosVDSessionInfo>& InSessionInfoRef)
			{
				if (EnumHasAnyFlags(InSessionInfoRef->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper))
				{
					return true;
				}
	
				FText SessionNameAsText = FText::AsCultureInvariant(InSessionInfoRef->SessionName);
				MenuBuilder.AddMenuEntry(
				SessionNameAsText,
				FText::Format(LOCTEXT("SingleTargetItemTooltip", "Select {0} session as current target"), SessionNameAsText),
				GetIconForSession(InSessionInfoRef->InstanceId),
				FUIAction(FExecuteAction::CreateSP(this, &SChaosVDRecordingControls::SelectTargetSession, InSessionInfoRef->InstanceId), EUIActionRepeatMode::RepeatDisabled));
				return true;
			});
		}
	}
	
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("CVDRecordingWidgetTargetsMulti", LOCTEXT("CVDRecordingMultiTargetsMenu", "Multi Target"));

	FText AllRemoteTargetsLabel = LOCTEXT("AllRemoteOption", "All Remote");
	MenuBuilder.AddMenuEntry(
			AllRemoteTargetsLabel,
			LOCTEXT("MultiRemoteTargetTooltip", "Select this to act on all remote targets"),
			GetIconForSession(FChaosVDRemoteSessionsManager::AllRemoteSessionsWrapperGUID),
			FUIAction(FExecuteAction::CreateSP(this, &SChaosVDRecordingControls::SelectTargetSession, FChaosVDRemoteSessionsManager::AllRemoteSessionsWrapperGUID),
			FCanExecuteAction::CreateSP(this, &SChaosVDRecordingControls::CanSelectMultiSessionTarget, FChaosVDRemoteSessionsManager::AllRemoteSessionsWrapperGUID),
			EUIActionRepeatMode::RepeatDisabled));

	FText AllRemoteServersTargetsLabel = LOCTEXT("AllRemoteServersOption", "All Remote Servers");
	MenuBuilder.AddMenuEntry(
			AllRemoteServersTargetsLabel,
			LOCTEXT("MultiRemoteServerTargetTooltip", "Select this to act on all remote server targets"),
			GetIconForSession(FChaosVDRemoteSessionsManager::AllRemoteServersWrapperGUID),
			FUIAction(FExecuteAction::CreateSP(this, &SChaosVDRecordingControls::SelectTargetSession, FChaosVDRemoteSessionsManager::AllRemoteServersWrapperGUID),
			FCanExecuteAction::CreateSP(this, &SChaosVDRecordingControls::CanSelectMultiSessionTarget, FChaosVDRemoteSessionsManager::AllRemoteServersWrapperGUID),
			EUIActionRepeatMode::RepeatDisabled));

	FText AllRemoteClientsTargetsLabel = LOCTEXT("AllRemoteClientsOption", "All Remote Clients");
	MenuBuilder.AddMenuEntry(
			AllRemoteClientsTargetsLabel,
			LOCTEXT("MultiRemoteClientTargetTooltip", "Select this to act on all remote client targets"),
			GetIconForSession(FChaosVDRemoteSessionsManager::AllRemoteClientsWrapperGUID),
			FUIAction(FExecuteAction::CreateSP(this, &SChaosVDRecordingControls::SelectTargetSession, FChaosVDRemoteSessionsManager::AllRemoteClientsWrapperGUID),
			FCanExecuteAction::CreateSP(this, &SChaosVDRecordingControls::CanSelectMultiSessionTarget, FChaosVDRemoteSessionsManager::AllRemoteClientsWrapperGUID),
			EUIActionRepeatMode::RepeatDisabled));

	FText AllTargets = LOCTEXT("AllTargetsOption", "All");
	MenuBuilder.AddMenuEntry(
		AllTargets,
		LOCTEXT("MultiAllTargetTooltip", "Select this to act on all targets, both Local and Remote"),
		GetIconForSession(FChaosVDRemoteSessionsManager::AllSessionsWrapperGUID),
		FUIAction(FExecuteAction::CreateSP(this, &SChaosVDRecordingControls::SelectTargetSession, FChaosVDRemoteSessionsManager::AllSessionsWrapperGUID), EUIActionRepeatMode::RepeatDisabled));

	MenuBuilder.AddMenuSeparator();

	FText CustomTargets = LOCTEXT("CustomTargetsOption", "Custom Selection");
	MenuBuilder.AddSubMenu(CustomTargets,
		LOCTEXT("MultiCustomTargetTooltip", "Select this to act on the specific targets you selected"),
		FNewMenuDelegate::CreateSP(this, &SChaosVDRecordingControls::GenerateCustomTargetsMenu)
		);

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SChaosVDRecordingControls::GenerateRecordingTimeTextBlock()
{
	return SNew(SBox)
			.VAlign(VAlign_Center)
			.Visibility_Raw(this, &SChaosVDRecordingControls::GetRecordingTimeTextBlockVisibility)
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "SmallButtonText")
				.Text_Raw(this, &SChaosVDRecordingControls::GetRecordingTimeText)
				.ColorAndOpacity(FColor::White)
			];
}

TSharedRef<SWidget> SChaosVDRecordingControls::GenerateToolbarWidget()
{
	RegisterMenus();

	FToolMenuContext MenuContext;

	UChaosVDRecordingToolbarMenuContext* CommonContextObject = NewObject<UChaosVDRecordingToolbarMenuContext>();
	CommonContextObject->RecordingControlsWidget = SharedThis(this);

	MenuContext.AddObject(CommonContextObject);

	return UToolMenus::Get()->GenerateWidget(RecordingControlsToolbarName, MenuContext);
}

EVisibility SChaosVDRecordingControls::GetRecordingTimeTextBlockVisibility() const
{
	TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo();
	bool bIsVisible = SessionInfo && !EnumHasAnyFlags(SessionInfo->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper) && IsRecording();

	return  bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<SWidget> SChaosVDRecordingControls::GenerateDataChannelsButton()
{
	return SNew(SComboButton)
			.ContentPadding(FMargin(6.0f, 0.0f))
			.IsEnabled_Raw(this, &SChaosVDRecordingControls::HasDataChannelsSupport)
			.MenuPlacement(MenuPlacement_AboveAnchor).ComboButtonStyle(&FAppStyle::Get()
			.GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
			.OnGetMenuContent(this, &SChaosVDRecordingControls::GenerateDataChannelsMenu)
			.HasDownArrow(true)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DataChannelsButton", "Data Channels"))
					.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("DialogButtonText"))
				]
			];
}

TSharedRef<SWidget> SChaosVDRecordingControls::GenerateLoadingModeSelector()
{
	TAttribute<int32> GetCurrentValueAttribute;
	GetCurrentValueAttribute.BindSPLambda(this, [this]()
	{
		return static_cast<int32>(CurrentLoadingMode);
	});

	SEnumComboBox::FOnEnumSelectionChanged EnumValueChangedDelegate = SEnumComboBox::FOnEnumSelectionChanged::CreateSPLambda(this, [this](int32 NewValue, ESelectInfo::Type SelectionType)
	{
		CurrentLoadingMode = static_cast<EChaosVDLoadRecordedDataMode>(NewValue);
	});
	
	return SNew(SEnumComboBox, StaticEnum<EChaosVDLoadRecordedDataMode>())
		.IsEnabled_Raw(this, &SChaosVDRecordingControls::CanChangeLoadingMode)
		.CurrentValue(GetCurrentValueAttribute)
		.OnEnumSelectionChanged(EnumValueChangedDelegate);
}

TSharedRef<SWidget> SChaosVDRecordingControls::GenerateDataChannelsMenu()
{
	using namespace Chaos::VisualDebugger;

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("CVDRecordingWidget", LOCTEXT("CVDRecordingMenuChannels", "Data Channels"));
	{
		if (TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo())
		{
			for (const TPair<FString, FChaosVDDataChannelState>& DataChannelStateWithName : SessionInfo->DataChannelsStatesByName)
			{
				FText ChannelNamesAsText = FText::AsCultureInvariant(DataChannelStateWithName.Key);
				MenuBuilder.AddMenuEntry(
				ChannelNamesAsText,
				FText::Format(LOCTEXT("ChannelDesc", "Enable/disable the {0} channel"), ChannelNamesAsText),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SChaosVDRecordingControls::ToggleChannelEnabledState, DataChannelStateWithName.Key),
					FCanExecuteAction::CreateSP(this, &SChaosVDRecordingControls::CanChangeChannelEnabledState, DataChannelStateWithName.Key), FIsActionChecked::CreateSP(this, &SChaosVDRecordingControls::IsChannelEnabled, DataChannelStateWithName.Key)), NAME_None, EUserInterfaceActionType::ToggleButton);
			}
		}
	}

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SChaosVDRecordingControls::ToggleChannelEnabledState(FString ChannelName)
{
	TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager();
	TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo();
	if (RemoteSessionManager && SessionInfo)
	{
		if (FChaosVDDataChannelState* ChannelState = SessionInfo->DataChannelsStatesByName.Find(ChannelName))
		{
			ChannelState->bWaitingUpdatedState = true;
			RemoteSessionManager->SendDataChannelStateChangeCommand(SessionInfo->Address, {ChannelState->ChannelName, !ChannelState->bIsEnabled });
		}
	}
}

bool SChaosVDRecordingControls::IsChannelEnabled(FString ChannelName)
{
	if (TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo())
	{
		if (FChaosVDDataChannelState* ChannelState = SessionInfo->DataChannelsStatesByName.Find(ChannelName))
		{
			return ChannelState->bIsEnabled;
		}
	}

	return false;
}

bool SChaosVDRecordingControls::CanChangeChannelEnabledState(FString ChannelName)
{
	if (TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo())
	{
		if (FChaosVDDataChannelState* ChannelState = SessionInfo->DataChannelsStatesByName.Find(ChannelName))
		{
			return ChannelState->bCanChangeChannelState && !ChannelState->bWaitingUpdatedState;
		}
	}
	
	return false;
}

void SChaosVDRecordingControls::SelectTargetSession(FGuid SessionId)
{
	CurrentSelectedSessionId = SessionId;
}

void SChaosVDRecordingControls::GenerateCustomTargetsMenu(FMenuBuilder& MenuBuilder)
{
	if (TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager())
	{
		RemoteSessionManager->EnumerateActiveSessions([this, &MenuBuilder](const TSharedRef<FChaosVDSessionInfo>& InSessionInfoRef)
		{
			if (EnumHasAnyFlags(InSessionInfoRef->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper))
			{
				return true;
			}
	
			FText SessionNameAsText = FText::AsCultureInvariant(InSessionInfoRef->SessionName);
			MenuBuilder.AddMenuEntry(
			SessionNameAsText,
			FText::Format(LOCTEXT("MultiTargetItemTooltip", "Select {0} session as one of the current targets"), SessionNameAsText),
			GetIconForSession(InSessionInfoRef->InstanceId),
			FUIAction(
			FExecuteAction::CreateSP(this, &SChaosVDRecordingControls::ToggleSessionSelectionInCustomTarget, InSessionInfoRef->InstanceId),
			FCanExecuteAction::CreateSP(this, &SChaosVDRecordingControls::CanSelectInCustomTarget, InSessionInfoRef->InstanceId),
			FIsActionChecked::CreateSP(this, &SChaosVDRecordingControls::IsSessionPartOfCustomTargetSelection, InSessionInfoRef->InstanceId)),
			NAME_None, EUserInterfaceActionType::ToggleButton);

			return true;
		});
	}
}

bool SChaosVDRecordingControls::IsSessionPartOfCustomTargetSelection(FGuid SessionGuid)
{
	if (TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager())
	{
		if (TSharedPtr<FChaosVDMultiSessionInfo> CustomSessionTarget = StaticCastSharedPtr<FChaosVDMultiSessionInfo>(RemoteSessionManager->GetSessionInfo(FChaosVDRemoteSessionsManager::CustomSessionsWrapperGUID).Pin()))
		{
			return CustomSessionTarget->InnerSessionsByInstanceID.Find(SessionGuid) != nullptr;
		}
	}

	return false;
}

void SChaosVDRecordingControls::ToggleSessionSelectionInCustomTarget(FGuid SessionGuid)
{
	if (TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager())
	{
		if (TSharedPtr<FChaosVDMultiSessionInfo> CustomSessionTarget = StaticCastSharedPtr<FChaosVDMultiSessionInfo>(RemoteSessionManager->GetSessionInfo(FChaosVDRemoteSessionsManager::CustomSessionsWrapperGUID).Pin()))
		{
			if (CustomSessionTarget->InnerSessionsByInstanceID.Find(SessionGuid) != nullptr)
			{
				CustomSessionTarget->InnerSessionsByInstanceID.Remove(SessionGuid);
			}
			else
			{
				CustomSessionTarget->InnerSessionsByInstanceID.Add(SessionGuid, RemoteSessionManager->GetSessionInfo(SessionGuid));
			}

			SelectTargetSession(FChaosVDRemoteSessionsManager::CustomSessionsWrapperGUID);
		}
	}
}

bool SChaosVDRecordingControls::CanSelectInCustomTarget(FGuid SessionGuid) const
{
	if (TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager())
	{
		if (TSharedPtr<FChaosVDSessionInfo> CustomSessionTarget = RemoteSessionManager->GetSessionInfo(SessionGuid).Pin())
		{
			return CustomSessionTarget->ReadyState == EChaosVDRemoteSessionReadyState::Ready;
		}
	}

	return false;
}

bool SChaosVDRecordingControls::CanSelectMultiSessionTarget(FGuid SessionGuid) const
{
	if (TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager())
	{
		if (TSharedPtr<FChaosVDSessionInfo> CustomSessionTarget = RemoteSessionManager->GetSessionInfo(SessionGuid).Pin())
		{
			return CanSelectMultiSessionTarget(CustomSessionTarget.ToSharedRef());
		}
	}

	return false;
}

bool SChaosVDRecordingControls::CanSelectMultiSessionTarget(const TSharedRef<FChaosVDSessionInfo>& SessionInfoRef) const
{
	if (EnumHasAnyFlags(SessionInfoRef->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper))
	{
		TSharedRef<FChaosVDMultiSessionInfo> AsMultiSessionInfo = StaticCastSharedRef<FChaosVDMultiSessionInfo>(SessionInfoRef);
		return !AsMultiSessionInfo->InnerSessionsByInstanceID.IsEmpty();
	}

	return false;
}

FSlateIcon SChaosVDRecordingControls::GetIconForSession(FGuid SessionId)
{
	if (TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetSessionInfo(SessionId))
	{
		return SessionInfo->IsRecording() ? FSlateIcon(FChaosVDStyle::GetStyleSetName(),FName("RecordIcon"), FName("RecordIcon")) : FSlateIcon();
	}

	return FSlateIcon();
}

TSharedPtr<FChaosVDSessionInfo> SChaosVDRecordingControls::GetCurrentSessionInfo() const
{
	return GetSessionInfo(CurrentSelectedSessionId);
}

TSharedPtr<FChaosVDSessionInfo> SChaosVDRecordingControls::GetSessionInfo(FGuid Id) const
{
	TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager();
	TSharedPtr<FChaosVDSessionInfo> SessionInfo = RemoteSessionManager ? RemoteSessionManager->GetSessionInfo(Id).Pin() : nullptr;

	return SessionInfo;
}

bool SChaosVDRecordingControls::HasDataChannelsSupport() const
{
	if (TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo())
	{
		return !SessionInfo->DataChannelsStatesByName.IsEmpty();
	}

	return false;
}

bool SChaosVDRecordingControls::CanChangeLoadingMode() const
{
	if (TSharedPtr<FChaosVDSessionInfo> CurrentSession = GetCurrentSessionInfo())
	{
		// In multi session mode targets, the loading mode is controlled automatically
		if (EnumHasAnyFlags(CurrentSession->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper))
		{
			return false;
		}
		else if (const TSharedPtr<SChaosVDMainTab> MainTabSharedPtr = MainTabWeakPtr.Pin())
		{
			// If nothing is loaded yet, it does not make sense change the loading mode
			return !MainTabSharedPtr->GetChaosVDEngineInstance()->GetCurrentSessionDescriptors().IsEmpty();
		}
	}

	return false;
}

SChaosVDRecordingControls::~SChaosVDRecordingControls()
{
	if (TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager())
	{
		RemoteSessionManager->OnSessionRecordingStarted().RemoveAll(this);
		RemoteSessionManager->OnSessionRecordingStopped().RemoveAll(this);
		RemoteSessionManager->OnSessionExpired().RemoveAll(this);
	}
}

const FSlateBrush* SChaosVDRecordingControls::GetRecordOrStopButton(EChaosVDRecordingMode RecordingMode) const
{
	const FSlateBrush* RecordIconBrush = FChaosVDStyle::Get().GetBrush("RecordIcon");
	return bRecordingButtonHovered && IsRecording() ? FChaosVDStyle::Get().GetBrush("StopIcon") : RecordIconBrush;
}

void SChaosVDRecordingControls::HandleRecordingStop(TWeakPtr<FChaosVDSessionInfo> SessionInfo)
{
	const TSharedPtr<SChaosVDMainTab> MainTabSharedPtr = MainTabWeakPtr.Pin();
	if (!MainTabSharedPtr.IsValid())
	{
		return;
	}

	TSharedPtr<FChaosVDSessionInfo> SessionInfoPtr = SessionInfo.Pin();

	if (!ensure(SessionInfoPtr))
	{
		return;
	}

	FString TraceTarget;

	switch (SessionInfoPtr->GetConnectionDetails().TransportMode)
	{
		case EChaosVDTransportMode::FileSystem:
			{
				TraceTarget = SessionInfoPtr->GetConnectionDetails().TraceTarget;
				break;
			}
		case EChaosVDTransportMode::TraceServer:
			{
				FTraceStoreConnectionSettings ConnectionDetails = GetTraceStoreConnectionSettings(SessionInfoPtr.ToSharedRef());
				TraceTarget = FChaosVDModule::Get().GetTraceManager()->GetTraceFileNameFromStoreForSession(ConnectionDetails.SessionAddress, ConnectionDetails.TraceID);
				break;
			}
		case EChaosVDTransportMode::Direct:
		case EChaosVDTransportMode::Relay:
		case EChaosVDTransportMode::Invalid:
		default:
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Transport mode is not supported | [%s]"), __func__, *UEnum::GetValueAsString(SessionInfoPtr->GetConnectionDetails().TransportMode));
			break;
	}

	const bool bIsLiveSession = SessionInfoPtr->GetRecordingMode() == EChaosVDRecordingMode::Live;

	if (UStatusBarSubsystem* StatusBarSubsystem = GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr)
	{
		StatusBarSubsystem->PopStatusBarMessage(StatusBarID, RecordingMessageHandle);

		if (bIsLiveSession)
		{
			const FText LiveSessionEnded = LOCTEXT("LiveSessionEndedMessage"," Live session has ended");
			LiveSessionEndedMessageHandle = StatusBarSubsystem->PushStatusBarMessage(StatusBarID, LiveSessionEnded);
		}
		else
		{
			const FText RecordingPathMessage = FText::Format(LOCTEXT("RecordingSavedPathMessage"," Recording saved at {0} "), FText::AsCultureInvariant(TraceTarget));
			RecordingPathMessageHandle = StatusBarSubsystem->PushStatusBarMessage(StatusBarID, RecordingPathMessage);
		}
	}

	if (!bIsLiveSession && !EnumHasAnyFlags(SessionInfoPtr->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper))
	{
		if (!TraceTarget.IsEmpty())
		{
			if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("OpenLastRecordingMessage", "Do you want to load the recorded file now? ")) == EAppReturnType::Yes)
			{
				MainTabSharedPtr->GetChaosVDEngineInstance()->LoadRecording(TraceTarget, EChaosVDLoadRecordedDataMode::SingleSource);			
			}
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FailedToOpenLastRecordingMessage", "Failed to obtain the file path to the recording."));
		}
	}
}

void SChaosVDRecordingControls::HandleRecordingStart(TWeakPtr<FChaosVDSessionInfo> SessionInfo)
{
	UStatusBarSubsystem* StatusBarSubsystem = GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr;
	if (!StatusBarSubsystem)
	{
		return;
	}
	
	if (RecordingPathMessageHandle.IsValid())
	{
		StatusBarSubsystem->PopStatusBarMessage(StatusBarID, RecordingPathMessageHandle);
		RecordingPathMessageHandle = FStatusBarMessageHandle();
	}
	
	if (LiveSessionEndedMessageHandle.IsValid())
	{
		StatusBarSubsystem->PopStatusBarMessage(StatusBarID, LiveSessionEndedMessageHandle);
		LiveSessionEndedMessageHandle = FStatusBarMessageHandle();
	}

	RecordingMessageHandle = StatusBarSubsystem->PushStatusBarMessage(StatusBarID, LOCTEXT("RecordingMessage", "Recording..."));
}

void SChaosVDRecordingControls::ExecuteAsyncConnectionAttemptTaskWithRetry(FGuid SessionID, int32 RemainingRetries, const TFunction<bool()>& InRecordingStartAttemptCallback, const TFunction<void()>& InRecordingFailedCallback)
{
	TSharedPtr<FAsyncConnectionAttemptTask> ConnectionAttemptRequest = MakeShared<FAsyncConnectionAttemptTask>(SessionID, InRecordingStartAttemptCallback, InRecordingFailedCallback, StaticCastWeakPtr<SChaosVDRecordingControls>(AsWeak()));
	ConnectionAttemptRequest->Start(RemainingRetries, IntervalBetweenAutoplayConnectionAttemptsSeconds);
}

void SChaosVDRecordingControls::FAsyncConnectionAttemptTask::HandleSuccess()
{
	State = EState::Success;

	if (TSharedPtr<SChaosVDRecordingControls> OwnerPtr = Owner.Pin())
	{
		OwnerPtr->HandleConnectionAttemptResult(SessionID, EChaosVDLiveConnectionAttemptResult::Success, ProgressNotification);
	}
}

void SChaosVDRecordingControls::FAsyncConnectionAttemptTask::HandleFailure()
{
	State = EState::Failed;

	if (RecordingFailedCallback)
	{
		RecordingFailedCallback();
	}

	if (TSharedPtr<SChaosVDRecordingControls> OwnerPtr = Owner.Pin())
	{
		OwnerPtr->HandleConnectionAttemptResult(SessionID, EChaosVDLiveConnectionAttemptResult::Failed, ProgressNotification);
	}
}

bool SChaosVDRecordingControls::FAsyncConnectionAttemptTask::CanExecute() const
{
	// The only two states where Execute() can be run are Not Started and InProgress
	// In Progress is allowed because we might need to execute again as part of the retry process
	return State == EState::NotStarted || State == EState::InProgress;
}

void SChaosVDRecordingControls::CancelInFlightConnectionAttempt(FGuid SessionID)
{
	if (TSharedPtr<FAsyncConnectionAttemptTask>* ExistingRequestInFlightPtrPtr = InFlightAsyncConnectionRequests.Find(SessionID))
	{
		if (TSharedPtr<FAsyncConnectionAttemptTask> ExistingRequestInFlightPtr = *ExistingRequestInFlightPtrPtr)
		{
			ExistingRequestInFlightPtr->Cancel();
		}
		else
		{
			UE_LOG(LogChaosVDEditor, Verbose, TEXT("[%hs] Attempted to cancel a connection of a Session that is no linger valid | Session ID [%s] ."), __func__, *SessionID.ToString());
		}

		return;
	}

	UE_LOG(LogChaosVDEditor, Verbose, TEXT("[%hs] Session ID [%s] not found."), __func__, *SessionID.ToString());
}

SChaosVDRecordingControls::FAsyncConnectionAttemptTask::~FAsyncConnectionAttemptTask()
{
	if (RetryDelegateHandle.IsValid())
	{
		FTSTicker::RemoveTicker(RetryDelegateHandle);
		RetryDelegateHandle = FTSTicker::FDelegateHandle();
	}
}

void SChaosVDRecordingControls::FAsyncConnectionAttemptTask::Cancel()
{
	State = EState::Canceled;

	if (RecordingFailedCallback)
	{
		RecordingFailedCallback();
	}

	if (TSharedPtr<SChaosVDRecordingControls> OwnerPtr = Owner.Pin())
	{
		OwnerPtr->HandleConnectionAttemptResult(SessionID, EChaosVDLiveConnectionAttemptResult::Canceled, ProgressNotification);
	}
}

void SChaosVDRecordingControls::FAsyncConnectionAttemptTask::ScheduleRetry()
{
	if (!CanExecute())
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] This connection attempt is in an invalid state | Current State Value [%u]"), __func__, State);
		return;
	}

	if (RetryDelegateHandle.IsValid())
	{
		UE_LOG(LogChaosVDEditor, Warning, TEXT("[%hs] Attempted to schedule a retry when there is one already scheduled"), __func__);
		return;
	}

	if (RemainingRetries <= 0)
	{
		UE_LOG(LogChaosVDEditor, Warning, TEXT("[%hs] Failed to connect to live session | attempts exhausted..."), __func__);
		HandleFailure();
		return;
	}
	
	RemainingRetries--;

	RetryDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSPLambda(AsShared(), [this](float DeltaTime)
	{
		Execute();
		return false;
	}), IntervalBetweenAttemptsSeconds);
}

void SChaosVDRecordingControls::FAsyncConnectionAttemptTask::Start(int32 InMaxRetriesAttempts, float InIntervalBetweenAttemptsSeconds)
{
	if (State != EState::NotStarted)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] This connection attempt is already started or in an invalid state | Current State Value [%u]"), __func__, State);
		return;
	}

	TSharedPtr<SChaosVDRecordingControls> OwnerPtr = Owner.Pin();
	if (!OwnerPtr)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] This connection attempt is does not have a valid owner | Cannot start attempt... "), __func__);
		return;
	}

	if (TSharedPtr<FAsyncConnectionAttemptTask>* ExistingRequestInFlight = OwnerPtr->InFlightAsyncConnectionRequests.Find(SessionID))
	{
		UE_LOG(LogChaosVDEditor, Warning, TEXT("[%hs] There is a connection attempt in fight for this session. attempting to cancel it | Session ID [%s]"), __func__, *SessionID.ToString());
		(*ExistingRequestInFlight)->Cancel();
	}

	OwnerPtr->InFlightAsyncConnectionRequests.Add(SessionID, AsShared());

	State = EState::InProgress;

	RemainingRetries = InMaxRetriesAttempts;
	IntervalBetweenAttemptsSeconds = InIntervalBetweenAttemptsSeconds;
	
	if (!ProgressNotification)
	{
		FNotificationInfo Info(LOCTEXT("ConnectingToSessionMessage", "Connecting Session ..."));
		Info.bFireAndForget = false;
		Info.FadeOutDuration = 3.0f;
		Info.ExpireDuration = 0.0f;
		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("CancelConnectionAttemptButton", "Cancel"), LOCTEXT("CancelConnectionAttemptButtonTip", "Cancels the active connection attempt."),
		FSimpleDelegate::CreateSP(AsShared(), &FAsyncConnectionAttemptTask::Cancel)));

		ProgressNotification = OwnerPtr->PushConnectionAttemptNotification(Info);
	}
	
	// We need to wait at least one tick before attempting to connect to give it time to the trace to be initialized, write to disk, and for the
	// session manager to hear back from a remote instance
	ScheduleRetry();
}

void SChaosVDRecordingControls::FAsyncConnectionAttemptTask::Execute()
{
	if (RetryDelegateHandle.IsValid())
	{
		FTSTicker::RemoveTicker(RetryDelegateHandle);
		RetryDelegateHandle.Reset();
	}

	if (!CanExecute())
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] This connection attempt is in an invalid state | Current State Value [%u]"), __func__, State);
		return;
	}

	if (!RecordingStartAttemptCallback)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed to connect to session | Invalid callback provided..."), __func__);
		HandleFailure();
		return;
	}

	if (TSharedPtr<SChaosVDRecordingControls> OwnerPtr = Owner.Pin())
	{
		 OwnerPtr->UpdateConnectionAttemptNotification(ProgressNotification, RemainingRetries);
	}

	// CVD needs the trace session name to be able to load a live session. Although the session exist, the session name might not be written right away
	// Trace files don't really have metadata, it is all part of the same stream, so we need to wait until it is written which might take a few ticks.
	// Therefore if it is not ready, try again a few times.
	if (!RecordingStartAttemptCallback())
	{
		UE_LOG(LogChaosVDEditor, Verbose, TEXT("[%hs] Failed to connect to live session | Attempting again in [%f]..."), __func__, IntervalBetweenAttemptsSeconds);
		ScheduleRetry();
	}
	else
	{
		HandleSuccess();
	}
}

SChaosVDRecordingControls::FTraceStoreConnectionSettings SChaosVDRecordingControls::GetTraceStoreConnectionSettings(const TSharedRef<FChaosVDSessionInfo>& InSessionInfoRef)
{
	FTraceStoreConnectionSettings ConnectionSettings;

	if (!ensure(!EnumHasAnyFlags(InSessionInfoRef->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper)))
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%s] Cannot not be called with a multi session wrapper."), ANSI_TO_TCHAR(__FUNCTION__));

		return ConnectionSettings;
	}

	const FChaosVDTraceDetails& RecordingSessionDetails = InSessionInfoRef->GetConnectionDetails();

	if (const UE::Trace::FStoreClient::FSessionInfo* TraceSessionInfo = FChaosVDTraceManager::GetTraceSessionInfo(RecordingSessionDetails.TraceTarget, RecordingSessionDetails.TraceGuid))
	{
		ConnectionSettings.SessionAddress = RecordingSessionDetails.TraceTarget;
		ConnectionSettings.TraceID = TraceSessionInfo->GetTraceId();
		return ConnectionSettings;
	}

	return ConnectionSettings;
}

void SChaosVDRecordingControls::ToggleMultiSessionSessionRecordingState(EChaosVDRecordingMode RecordingMode, const TSharedRef<FChaosVDMultiSessionInfo>& InSessionInfoRef)
{
	const TSharedPtr<SChaosVDMainTab> MainTabSharedPtr = MainTabWeakPtr.Pin();
	if (!ensure(MainTabSharedPtr))
	{
		return;
	}

	bool bNewRecordingState = !IsRecording();

	if (bNewRecordingState)
	{
		CurrentLoadingMode = EChaosVDLoadRecordedDataMode::MultiSource;
		MainTabSharedPtr->GetChaosVDEngineInstance()->CloseActiveTraceSessions();
	}

	InSessionInfoRef->EnumerateInnerSessions([this, RecordingMode, bNewRecordingState](const TSharedRef<FChaosVDSessionInfo>& InInnerSessionRef)
	{
		SetSessionRecordingState(bNewRecordingState, RecordingMode, InInnerSessionRef);
		return true;
	});
}

void SChaosVDRecordingControls::ToggleSingleSessionRecordingState(EChaosVDRecordingMode RecordingMode, const TSharedRef<FChaosVDSessionInfo>& SessionInfoRef)
{
	SetSessionRecordingState(!SessionInfoRef->IsRecording(), RecordingMode, SessionInfoRef);
}

void SChaosVDRecordingControls::SetSessionRecordingState(bool bIsRecording, EChaosVDRecordingMode RecordingMode, const TSharedRef<FChaosVDSessionInfo>& SessionInfoRef)
{
	TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManager = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager();
	if (!RemoteSessionManager)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Session Manager is not available"), __func__);
		return;
	}

	if (bIsRecording)
	{
		const TSharedPtr<SChaosVDMainTab> MainTabSharedPtr = MainTabWeakPtr.Pin();
		if (!ensure(MainTabSharedPtr))
		{
			// This should not be possible, but if we don't have a valid tab instance it will not be possible to properly handle any start recording attempt
			UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Main Tab instance is not available"), __func__);
			return;
		}

		TArray<FString, TInlineAllocator<1>> RecordingArgs;

		int32 RemainingRetries = 4;
		EChaosVDTransportMode DataTransportOverrideMode = EChaosVDTransportMode::Invalid;

		if (UChaosVDGeneralSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDGeneralSettings>())
		{
			RemainingRetries = Settings->MaxConnectionRetries;
			DataTransportOverrideMode = Settings->DataTransportModeOverride;
		}
		else
		{
			UE_LOG(LogChaosVDEditor, Warning, TEXT("Failed to obtain setting object. Setting the retries attempts to connect to a session to 4 as a fallback."))
		}

		SessionInfoRef->ReadyState = EChaosVDRemoteSessionReadyState::Busy;
		SessionInfoRef->SetLastRequestedRecordingMode(RecordingMode);

		auto RecordingAttemptFailedCallback = [SessionGUID = SessionInfoRef->InstanceId]()
		{
			TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManagerPtr = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager();
			TSharedPtr<FChaosVDSessionInfo> SessionInfoPtr = RemoteSessionManagerPtr ? RemoteSessionManagerPtr->GetSessionInfo(SessionGUID).Pin() : nullptr;

			if (!SessionInfoPtr)
			{
				return;
			}

			SessionInfoPtr->ReadyState = EChaosVDRemoteSessionReadyState::Ready;
		};

		if (RecordingMode == EChaosVDRecordingMode::Live)
		{
			FChaosVDStartRecordingCommandMessage RecordingParams;
			RecordingParams.RecordingMode = EChaosVDRecordingMode::Live;
			RecordingParams.TransportMode = DataTransportOverrideMode != EChaosVDTransportMode::Invalid ? DataTransportOverrideMode : EChaosVDTransportMode::TraceServer;

			check(GLog);
			bool bOutCanBindAll = false;
			//TODO: Add a way to specify a local address in case we have multiple adapters?
			TSharedRef<FInternetAddr> LocalIP = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalHostAddr(*GLog, bOutCanBindAll);

			constexpr bool bAppendPort = false;
			RecordingParams.Target = LocalIP->ToString(bAppendPort);

#if WITH_CHAOS_VISUAL_DEBUGGER
			Chaos::VisualDebugger::FChaosVDDataChannelsManager::Get().EnumerateChannels([&RecordingParams](const TSharedRef<Chaos::VisualDebugger::FChaosVDOptionalDataChannel>& Channel)
			{
				if (Channel->IsChannelEnabled())
				{
					RecordingParams.DataChannelsEnabledOverrideList.Add(Channel->GetId().ToString());
				}
				return true;
			});
#endif

			if (RecordingParams.TransportMode == EChaosVDTransportMode::Direct)
			{
				// Memory Tracing mode requires opening a trace session in the editor side first before starting the recording
				// so targets have something to connect to (Usually targets connect to a running trace server)
				if (!MainTabSharedPtr->ConnectToLiveSession_Direct(CurrentLoadingMode))
				{
					UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed to connect to live session using direct trace mode"), __func__);
					RecordingAttemptFailedCallback();
					return;
				}

				// If we get here it means we have at least one valid session
				const FChaosVDTraceSessionDescriptor& SessionDesc = MainTabSharedPtr->GetChaosVDEngineInstance()->GetCurrentSessionDescriptors().Last();

				RecordingParams.Target.Appendf(TEXT(":%u"), SessionDesc.SessionPort);

				RemoteSessionManager->SendStartRecordingCommand(SessionInfoRef->Address, RecordingParams);
			}
			else if (RecordingParams.TransportMode == EChaosVDTransportMode::Relay)
			{
				// Relay Tracing mode requires opening a trace session in the editor side first before starting the recording
				// so targets have something to connect to (Usually targets connect to a running trace server)
				if (!MainTabSharedPtr->ConnectToLiveSession_Relay(SessionInfoRef->InstanceId, CurrentLoadingMode))
				{
					UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed to connect to live session using relay trace mode"), __func__);
					RecordingAttemptFailedCallback();
					return;
				}

				RemoteSessionManager->SendStartRecordingCommand(SessionInfoRef->Address, RecordingParams);

#if WITH_CHAOS_VISUAL_DEBUGGER
				// Once the start recording command is issued, we can try to connect to the created session (if everything went well).
				// If it didn't go well, this will take care of update the UI to notify the user
				ExecuteAsyncConnectionAttemptTaskWithRetry(SessionInfoRef->InstanceId, RemainingRetries, [WeakThis = AsWeak(), SessionInstanceId = SessionInfoRef->InstanceId]()
				{
					TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManagerPtr = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager();
					TSharedPtr<FChaosVDSessionInfo> SessionInfoPtr = RemoteSessionManagerPtr ? RemoteSessionManagerPtr->GetSessionInfo(SessionInstanceId).Pin() : nullptr;
					if (!SessionInfoPtr)
					{
						return false;
					}
    
					const FChaosVDTraceDetails& RecordingSessionDetails = SessionInfoPtr->GetConnectionDetails();

					// We didn't receive the connection details yet. Trying again later...
					if (!RecordingSessionDetails.IsValid())
					{
						return false;
					}

					// Check if there is a connection attempt in progress and its results
					const Chaos::VD::EConnectionAttemptResult ConnectionAttemptStatus = FChaosVDEngineEditorBridge::Get().GetRelayTransportInstance()->GetConnectionAttemptResult(SessionInstanceId);
					switch (ConnectionAttemptStatus)
					{
					case Chaos::VD::InProgress:
						{
							// A connection attempt started but we don't have the results yet. Trying again later...
							return false;
						}
					case Chaos::VD::NotStarted:
						{
							Chaos::VD::FRelayConnectionInfo ConnectionInfo;
							ConnectionInfo.Port = RecordingSessionDetails.Port;
							ConnectionInfo.Address = RecordingSessionDetails.TraceTarget;
							ConnectionInfo.CertificateAuthority = RecordingSessionDetails.CertAuth;
							Chaos::VD::EConnectionAttemptResult Result = FChaosVDEngineEditorBridge::Get().GetRelayTransportInstance()->ConnectToRelay(SessionInstanceId, ConnectionInfo);

							return Result == Chaos::VD::EConnectionAttemptResult::Success;
						}
					case Chaos::VD::Success:
						// The current connection attempt succeed, we can stop the async retry loop
						return true;
					case Chaos::VD::Failed:
					default:
						// There was a connection attempt, but it failed. Abandoning the retry attempt as they will not succeed
						return false;
					}
				},
				RecordingAttemptFailedCallback);
#endif
			}
			else
			{
				RemoteSessionManager->SendStartRecordingCommand(SessionInfoRef->Address, RecordingParams);
                
                // Once the start recording command is issue, we can try to connect to the created session (if everything went well).
                // If it didn't go well, this will take care of update the UI to notify the user
                ExecuteAsyncConnectionAttemptTaskWithRetry(SessionInfoRef->InstanceId, RemainingRetries, [WeakThis = AsWeak(), SessionInstanceId = SessionInfoRef->InstanceId]()
                {
                	TSharedPtr<FChaosVDRemoteSessionsManager> RemoteSessionManagerPtr = FChaosVDEngineEditorBridge::Get().GetRemoteSessionsManager();
                	TSharedPtr<FChaosVDSessionInfo> SessionInfoPtr = RemoteSessionManagerPtr ? RemoteSessionManagerPtr->GetSessionInfo(SessionInstanceId).Pin() : nullptr;
    
                	if (!SessionInfoPtr)
                	{
                		return false;
                	}
                	
                	TSharedPtr<SChaosVDRecordingControls> Controls =  StaticCastSharedPtr<SChaosVDRecordingControls>(WeakThis.Pin());
                	const TSharedPtr<SChaosVDMainTab> MainTabSharedPtr = Controls ? Controls->MainTabWeakPtr.Pin() : nullptr;
                	if (!MainTabSharedPtr)
                	{
                		return false;
                	}
    
                	FTraceStoreConnectionSettings ConnectionDetails = Controls->GetTraceStoreConnectionSettings(SessionInfoPtr.ToSharedRef());
                	return MainTabSharedPtr->ConnectToLiveSession(ConnectionDetails.TraceID, ConnectionDetails.SessionAddress, Controls->CurrentLoadingMode);
                },
                RecordingAttemptFailedCallback);
			}
		}
		else
		{
			FChaosVDStartRecordingCommandMessage RecordingParams;
			RecordingParams.RecordingMode = EChaosVDRecordingMode::File;
			RecordingParams.TransportMode = DataTransportOverrideMode != EChaosVDTransportMode::Invalid ? DataTransportOverrideMode : EChaosVDTransportMode::FileSystem;

#if WITH_CHAOS_VISUAL_DEBUGGER
			Chaos::VisualDebugger::FChaosVDDataChannelsManager::Get().EnumerateChannels([&RecordingParams](const TSharedRef<Chaos::VisualDebugger::FChaosVDOptionalDataChannel>& Channel)
			{
				if (Channel->IsChannelEnabled())
				{
					RecordingParams.DataChannelsEnabledOverrideList.Add(Channel->GetId().ToString());
				}
				return true;
			});
#endif
			
			RemoteSessionManager->SendStartRecordingCommand(SessionInfoRef->Address, RecordingParams);

			// Once the start recording command is issued, we need to check if the recording started, which might take a few frames.
			// This will take care of retrying, waiting and update the UI to notify the user if needed.
			ExecuteAsyncConnectionAttemptTaskWithRetry(SessionInfoRef->InstanceId, RemainingRetries, [WeakThis = AsWeak()]()
			{
				TSharedPtr<SChaosVDRecordingControls> Controls = StaticCastSharedPtr<SChaosVDRecordingControls>(WeakThis.Pin());
				return Controls ? Controls->IsRecording() : false;
			},
			RecordingAttemptFailedCallback);
		}
	}
	else
	{
		CancelInFlightConnectionAttempt(SessionInfoRef->InstanceId);
		SessionInfoRef->SetLastRequestedRecordingMode(EChaosVDRecordingMode::Invalid);
		RemoteSessionManager->SendStopRecordingCommand(SessionInfoRef->Address);
	}
}

FReply SChaosVDRecordingControls::ToggleRecordingState(EChaosVDRecordingMode RecordingMode)
{
	TSharedPtr<FChaosVDSessionInfo> SessionInfoPtr = GetCurrentSessionInfo();
	if (!SessionInfoPtr)
	{
		return FReply::Handled();
	}

	if (EnumHasAnyFlags(SessionInfoPtr->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper))
	{
		ToggleMultiSessionSessionRecordingState(RecordingMode, StaticCastSharedPtr<FChaosVDMultiSessionInfo>(SessionInfoPtr).ToSharedRef());
	}
	else
	{
		ToggleSingleSessionRecordingState(RecordingMode, SessionInfoPtr.ToSharedRef());
	}

	return FReply::Handled();
}

bool SChaosVDRecordingControls::IsRecordingToggleButtonEnabled(EChaosVDRecordingMode RecordingMode) const
{
	if (CurrentSelectedSessionId == FChaosVDRemoteSessionsManager::InvalidSessionGUID)
	{
		return false;
	}

	if (!IsRecording())
	{
		return true;
	}

	// If we are recording, don't show the stop button for the mode that is disabled
	if (TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo())
	{
		if (SessionInfo->GetRecordingMode() == RecordingMode)
		{
			if (EnumHasAnyFlags(SessionInfo->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::IsMultiSessionWrapper))
			{
				TSharedPtr<FChaosVDMultiSessionInfo> AsMultiSessionInfo = StaticCastSharedPtr<FChaosVDMultiSessionInfo>(SessionInfo);

				return CanSelectMultiSessionTarget(SessionInfo.ToSharedRef());
			}

			return true;
		}

		return false;
	}

	return false;
}

EVisibility SChaosVDRecordingControls::IsRecordingToggleButtonVisible(EChaosVDRecordingMode RecordingMode) const
{
	if (!IsRecording())
	{
		return EVisibility::Visible;
	}

	// If we are recording, don't show the stop button for the mode that is disabled
	if (TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo())
	{
		EChaosVDRecordingMode CurrentRecordingMode = SessionInfo->GetRecordingMode();
		if (CurrentRecordingMode == EChaosVDRecordingMode::Invalid)
		{
			return EVisibility::Visible;
		}

		return SessionInfo->GetRecordingMode() == RecordingMode ? EVisibility::Visible : EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

void SChaosVDRecordingControls::RegisterMenus()
{
	const UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus->IsMenuRegistered(RecordingControlsToolbarName))
	{
		return;
	}

	UToolMenu* ToolBar = UToolMenus::Get()->RegisterMenu(RecordingControlsToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);

	FToolMenuSection& Section = ToolBar->AddSection("LoadRecording");
	Section.AddDynamicEntry("OpenFile", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const UChaosVDRecordingToolbarMenuContext* Context = InSection.FindContext<UChaosVDRecordingToolbarMenuContext>();
		TSharedRef<SChaosVDRecordingControls> RecordingControls = Context->RecordingControlsWidget.Pin().ToSharedRef();

		TSharedRef<SWidget> RecordToFileButton = SNew(SBox).Padding(4.0f,0.0f)[ RecordingControls->GenerateToggleRecordingStateButton(EChaosVDRecordingMode::File, LOCTEXT("RecordToFileButtonDesc", "Starts a recording for the current session, saving it directly to file")) ];
		TSharedRef<SWidget> RecordToLiveButton = SNew(SBox).Padding(4.0f,0.0f)[ RecordingControls->GenerateToggleRecordingStateButton(EChaosVDRecordingMode::Live, LOCTEXT("RecordLiveButtonDesc", "Starts a recording and automatically connects to it playing it back in real time")) ];
		TSharedRef<SWidget> SessionsDropdown = SNew(SBox).Padding(4.0f,0.0f)[ RecordingControls->GenerateTargetSessionSelector() ];
		TSharedRef<SWidget> RecordingTime = RecordingControls->GenerateRecordingTimeTextBlock();
		TSharedRef<SWidget> DataChannelsButton = SNew(SBox).Padding(4.0f,0.0f)[ RecordingControls->GenerateDataChannelsButton() ];
		TSharedRef<SWidget> LoadingModeSelector = SNew(SBox).Padding(4.0f,0.0f)[ RecordingControls->GenerateLoadingModeSelector() ];

		InSection.AddSeparator("RecordingControlsDivider");

		InSection.AddEntry(
			FToolMenuEntry::InitWidget(
				"AvailableSessions",
				SessionsDropdown,
				FText::GetEmpty(),
				false,
				false
			));
		
		InSection.AddEntry(
			FToolMenuEntry::InitWidget(
				"LoadingModeSelector",
				LoadingModeSelector,
				FText::GetEmpty(),
				false,
				false
			));

		InSection.AddEntry(
			FToolMenuEntry::InitWidget(
				"RecordToFileButton",
				RecordToFileButton,
				FText::GetEmpty(),
				true,
				false
			));

		InSection.AddEntry(
			FToolMenuEntry::InitWidget(
				"RecordToLiveButton",
				RecordToLiveButton,
				FText::GetEmpty(),
				false,
				false
			));

		InSection.AddEntry(
			FToolMenuEntry::InitWidget(
				"RecordingTime",
				RecordingTime,
				FText::GetEmpty(),
				false,
				false
			));

		InSection.AddEntry(
			FToolMenuEntry::InitWidget(
				"DataChannelsButton",
				DataChannelsButton,
				FText::GetEmpty(),
				false,
				false
			));
	}));
}

bool SChaosVDRecordingControls::IsRecording() const
{
	TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo();
	return SessionInfo ? SessionInfo->IsRecording() : false;
}

FText SChaosVDRecordingControls::GetRecordingTimeText() const
{
	if (TSharedPtr<FChaosVDSessionInfo> SessionInfo = GetCurrentSessionInfo())
	{
		FNumberFormattingOptions FormatOptions;
		FormatOptions.MinimumFractionalDigits = 2;
		FormatOptions.MaximumFractionalDigits = 2;

		FText SecondsText = FText::AsNumber(SessionInfo->LastKnownRecordingState.ElapsedTime, &FormatOptions);
		return FText::Format(LOCTEXT("RecordingTimer","{0} s"), SecondsText);
	}

	return LOCTEXT("RecordingTimerError","Failed to get time information");
}

TSharedPtr<SNotificationItem> SChaosVDRecordingControls::PushConnectionAttemptNotification(const FNotificationInfo& InNotificationInfo)
{
	TSharedPtr<SNotificationItem> ConnectionAttemptNotification = FSlateNotificationManager::Get().AddNotification(InNotificationInfo);
	
	if (ConnectionAttemptNotification.IsValid())
	{
		ConnectionAttemptNotification->SetCompletionState(SNotificationItem::CS_Pending);
		return ConnectionAttemptNotification;
	}

	return nullptr;
}

void SChaosVDRecordingControls::UpdateConnectionAttemptNotification(const TSharedPtr<SNotificationItem>& InNotification, int32 AttemptsRemaining)
{
	if (InNotification)
	{
		InNotification->SetSubText(FText::FormatOrdered(LOCTEXT("SessionConnectionAttemptSubText", "Attempts Remaining {0}"), AttemptsRemaining));
	}
}

void SChaosVDRecordingControls::HandleConnectionAttemptResult(FGuid SessionGUID, EChaosVDLiveConnectionAttemptResult Result, const TSharedPtr<SNotificationItem>& InNotification)
{
	InFlightAsyncConnectionRequests.Remove(SessionGUID);

	if (InNotification)
	{
		switch (Result)
		{
		case EChaosVDLiveConnectionAttemptResult::Success:
			{
				InNotification->SetText(LOCTEXT("SessionConnectionSuccess", "Connected!"));
				InNotification->SetSubText(FText::GetEmpty());
				InNotification->SetCompletionState(SNotificationItem::ECompletionState::CS_Success);
				break;
			}
		case EChaosVDLiveConnectionAttemptResult::Canceled:
			{
				InNotification->SetText(LOCTEXT("SessionConnectionCanceledText", "Connection Attempt Canceled"));
				InNotification->SetCompletionState(SNotificationItem::ECompletionState::CS_None);
				break;
			}
		case EChaosVDLiveConnectionAttemptResult::Failed:
			{
				InNotification->SetText(LOCTEXT("SessionConnectionFailedText", "Failed to connect"));
				InNotification->SetSubText(LOCTEXT("SessionConnectionFailedSubText", "See the logs for more details..."));
				InNotification->SetCompletionState(SNotificationItem::ECompletionState::CS_Fail);
				break;
			}
		default:
			{
				InNotification->SetText(LOCTEXT("SessionConnectionUnknownText", "Something went wrong..."));
				InNotification->SetSubText(LOCTEXT("SessionConnectionUnknownTextSubText", "See the logs for more details..."));
				InNotification->SetCompletionState(SNotificationItem::ECompletionState::CS_Fail);
				break;
			}
		}

		InNotification->ExpireAndFadeout();
	}
}

#undef LOCTEXT_NAMESPACE 
