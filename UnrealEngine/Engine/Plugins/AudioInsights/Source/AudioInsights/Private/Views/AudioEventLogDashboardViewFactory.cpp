// Copyright Epic Games, Inc. All Rights Reserved.
#include "Views/AudioEventLogDashboardViewFactory.h"

#include "AudioEventLogEditorCommands.h"
#include "AudioInsightsModule.h"
#include "AudioInsightsStyle.h"
#include "AudioMixerTrace.h"
#include "Features/IModularFeatures.h"
#include "Messages/AudioEventLogTraceMessages.h"
#include "Providers/AudioEventLogTraceProvider.h"

#if !WITH_EDITOR
#include "AudioInsightsComponent.h"
#endif // !WITH_EDITOR

#define LOCTEXT_NAMESPACE "AudioInsightsEventLog"

namespace UE::Audio::Insights
{
	/////////////////////////////////////////////////////////////////////////////////////////
	// AudioEventLogPrivate
	namespace AudioEventLogPrivate
	{
		const FAudioEventLogDashboardEntry& CastEntry(const IDashboardDataViewEntry& InData)
		{
			return static_cast<const FAudioEventLogDashboardEntry&>(InData);
		};

		FAudioEventLogDashboardEntry& CastEntry(IDashboardDataViewEntry& InData)
		{
			return static_cast<FAudioEventLogDashboardEntry&>(InData);
		};

		bool CanClearLog()
		{
#if WITH_EDITOR
			return true;
#else
			const TSharedPtr<const FAudioInsightsComponent> AudioInsightsComponent = FAudioInsightsModule::GetChecked().GetAudioInsightsComponent();

			return AudioInsightsComponent.IsValid() && AudioInsightsComponent->GetIsLiveSession();
#endif // WITH_EDITOR
		}

#if WITH_EDITOR
		bool TimestampIsInChunkMarkedToDelete(const double Timestamp)
		{
			FAudioInsightsCacheManager& CacheManager = IAudioInsightsModule::GetChecked().GetCacheManager();

			if (CacheManager.GetNumUsedChunks() != CacheManager.GetNumChunks())
			{
				// The cache is not full - no chunks are marked to delete
				return false;
			}

			const TOptional<uint32> NumChunksFromStart = CacheManager.TryGetNumChunksFromStartForTimestamp(Timestamp);
			return NumChunksFromStart.IsSet() && NumChunksFromStart.GetValue() == 0u;
		}
#endif // WITH_EDITOR

		const FString CategorySoundActivity = LOCTEXT("EventLogCategory_SoundActivity", "Sound Activity").ToString();
		const FString CategoryVirtualization = LOCTEXT("EventLogCategory_Virtualization", "Virtualization").ToString();
		const FString CategoryPlayRequests = LOCTEXT("EventLogCategory_PlayRequests", "Play Requests").ToString();
		const FString CategoryStopRequests = LOCTEXT("EventLogCategory_StopRequests", "Stop Requests").ToString();
		const FString CategoryPlayErrors = LOCTEXT("EventLogCategory_PlayErrors", "Play Errors").ToString();
		const FString CategoryMessages = LOCTEXT("EventLogCategory_Messages", "Messages").ToString();
		const FString CategoryCustom = LOCTEXT("EventLogCategory_Custom", "Custom").ToString();

		const FName CacheStatusColumnID = "CacheStatus";
		const FName MessageIDColumnID = "MessageID";
		const FName TimestampColumnID = "Timestamp";
		const FName PlayOrderColumnID = "PlayOrder";
		const FName EventColumnID = "Event";
		const FName AssetNameColumnID = "Asset";
		const FName ActorColumnID = "Actor";
		const FName CategoryColumnID = "Category";

		const FText ClearLogText = LOCTEXT("AudioEventLog_ClearLog_Text", "Clear Log");
		const FText ClearLogTooltipText = LOCTEXT("AudioEventLog_ClearLog_Tooltip", "Clears the event log for the currently selected world.");
	} // namespace AudioEventLogPrivate

	const TMap<FString, TSet<FString>> FAudioEventLogDashboardViewFactory::GetInitEventTypeFilters()
	{
		using namespace AudioEventLogPrivate;
		using namespace ::Audio::Trace::EventLog;

		return
		{
			{
				CategorySoundActivity,
				{
					ID::SoundStart,
					ID::SoundStop,
					ID::PauseSoundRequested,
					ID::ResumeSoundRequested
				}
			},

			{
				CategoryVirtualization,
				{
					ID::SoundVirtualized,
					ID::SoundRealized
				}
			},

			{
				CategoryPlayRequests,
				{
					ID::PlayRequestSoundHandle,
					ID::PlayRequestAudioComponent,
					ID::PlayRequestOneShot,
					ID::PlayRequestSoundAtLocation,
					ID::PlayRequestSound2D,
					ID::PlayRequestSlateSound
				}
			},

			{
				CategoryStopRequests,
				{
					ID::StopAllRequested,
					ID::StopRequestedSoundHandle,
					ID::StopRequestAudioComponent,
					ID::StopRequestActiveSound,
					ID::StopRequestSoundsUsingResource,
					ID::StopRequestConcurrency
				}
			},

			{
				CategoryPlayErrors,
				{
					ID::PlayFailedNotPlayable,
					ID::PlayFailedOutOfRange,
					ID::PlayFailedDebugFiltered,
					ID::PlayFailedConcurrency,
				}
			},

			{
				CategoryMessages,
				{
					ID::FlushAudioDeviceRequested
				}
			},

			{
				CategoryCustom,
				{
					// Auto-populates with custom events from users which haven't been categorized 
				}
			}
		};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// FAudioEventLogDashboardViewFactory
	FAudioEventLogDashboardViewFactory::FAudioEventLogDashboardViewFactory()
		: InitEventTypeFilters(GetInitEventTypeFilters())
	{
		using namespace AudioEventLogPrivate;

		FTraceModule& AudioInsightsTraceModule = static_cast<FTraceModule&>(FAudioInsightsModule::GetChecked().GetTraceModule());
		AudioInsightsTraceModule.OnAnalysisStarting.AddRaw(this, &FAudioEventLogDashboardViewFactory::OnAnalysisStarting);

		AudioEventLogTraceProvider = MakeShared<FAudioEventLogTraceProvider>();

		AudioInsightsTraceModule.AddTraceProvider(AudioEventLogTraceProvider);

		Providers = TArray<TSharedPtr<FTraceProviderBase>>
		{
			AudioEventLogTraceProvider
		};

		SortByColumn = TimestampColumnID;
		SortMode = EColumnSortMode::Ascending;

		FAudioEventLogEditorCommands::Register();
		BindCommands();

#if WITH_EDITOR
		FAudioEventLogSettings::OnReadSettings.AddRaw(this, &FAudioEventLogDashboardViewFactory::OnReadEditorSettings);
		FAudioEventLogSettings::OnWriteSettings.AddRaw(this, &FAudioEventLogDashboardViewFactory::OnWriteEditorSettings);

		FAudioInsightsCacheManager& CacheManager = IAudioInsightsModule::GetChecked().GetCacheManager();
		CacheManager.OnChunkOverwritten.AddRaw(this, &FAudioEventLogDashboardViewFactory::OnCacheChunkOverwritten);
#endif // WITH_EDITOR
	}

	FAudioEventLogDashboardViewFactory::~FAudioEventLogDashboardViewFactory()
	{
		if (FModuleManager::Get().IsModuleLoaded("AudioInsights") && IModularFeatures::Get().IsModularFeatureAvailable(TraceServices::ModuleFeatureName))
		{
			FTraceModule& TraceModule = static_cast<FTraceModule&>(FAudioInsightsModule::GetChecked().GetTraceModule());
			TraceModule.OnAnalysisStarting.RemoveAll(this);
		}

#if WITH_EDITOR
		FAudioEventLogSettings::OnReadSettings.RemoveAll(this);
		FAudioEventLogSettings::OnWriteSettings.RemoveAll(this);

		if (FModuleManager::Get().IsModuleLoaded("AudioInsights"))
		{
			FAudioInsightsCacheManager& CacheManager = IAudioInsightsModule::GetChecked().GetCacheManager();
			CacheManager.OnChunkOverwritten.RemoveAll(this);
		}
#endif // WITH_EDITOR

		FAudioEventLogEditorCommands::Unregister();
	}

	void FAudioEventLogDashboardViewFactory::BindCommands()
	{
		CommandList = MakeShared<FUICommandList>();

		const FAudioEventLogEditorCommands& Commands = FAudioEventLogEditorCommands::Get();

		CommandList->MapAction(Commands.GetResetInspectTimestampCommand(), FExecuteAction::CreateLambda([this]() { ResetInspectTimestamp(); }));

#if WITH_EDITOR
		CommandList->MapAction(Commands.GetBrowseCommand(), FExecuteAction::CreateLambda([this]() { BrowseToAsset(); }));
		CommandList->MapAction(Commands.GetEditCommand(), FExecuteAction::CreateLambda([this]() { OpenAsset(); }));

		CommandList->MapAction(
			Commands.GetAutoStopCachingWhenLastInCacheCommand(),
			FExecuteAction::CreateLambda([this]() { AutoStopCachingMode = EAutoStopCachingMode::WhenInLastChunk; }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return AutoStopCachingMode == EAutoStopCachingMode::WhenInLastChunk; }));

		CommandList->MapAction(
			Commands.GetAutoStopCachingOnInspectCommand(),
			FExecuteAction::CreateLambda([this]() { AutoStopCachingMode = EAutoStopCachingMode::OnInspect; }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return AutoStopCachingMode == EAutoStopCachingMode::OnInspect; }));

		CommandList->MapAction(
			Commands.GetAutoStopCachingDisabledCommand(),
			FExecuteAction::CreateLambda([this]() { AutoStopCachingMode = EAutoStopCachingMode::Never; }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]() { return AutoStopCachingMode == EAutoStopCachingMode::Never; }));
#endif // WITH_EDITOR
	}

	void FAudioEventLogDashboardViewFactory::ResetInspectTimestamp()
	{
		FocusedItem.Reset();
		bAutoScroll = true;
		if (FilteredEntriesListView.IsValid())
		{
			FilteredEntriesListView->ClearSelection();
			FilteredEntriesListView->ClearHighlightedItems();
		}

		FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();
		AudioInsightsModule.GetTimingViewExtender().ResetMessageProcessType();
	}

	FName FAudioEventLogDashboardViewFactory::GetName() const
	{
		return "EventLog";
	}

	FText FAudioEventLogDashboardViewFactory::GetDisplayName() const
	{
		return LOCTEXT("AudioDashboard_AudioEventLog_Name", "Event Log");
	}

	EDefaultDashboardTabStack FAudioEventLogDashboardViewFactory::GetDefaultTabStack() const
	{
		return EDefaultDashboardTabStack::Log;
	}

	FSlateIcon FAudioEventLogDashboardViewFactory::GetIcon() const
	{
		return FSlateStyle::Get().CreateIcon("AudioInsights.Icon.Log");
	}

	FReply FAudioEventLogDashboardViewFactory::OnDataRowKeyInput(const FGeometry& Geometry, const FKeyEvent& KeyEvent) const
	{
		return (CommandList && CommandList->ProcessCommandBindings(KeyEvent)) ? FReply::Handled() : FReply::Unhandled();
	}

	TSharedRef<SWidget> FAudioEventLogDashboardViewFactory::MakeWidget(TSharedRef<SDockTab> OwnerTab, const FSpawnTabArgs& SpawnTabArgs)
	{
		FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();
		AudioInsightsModule.GetTimingViewExtender().OnTimingViewTimeMarkerChanged.AddSP(this, &FAudioEventLogDashboardViewFactory::OnTimingViewTimeMarkerChanged);

		if (!DashboardWidget.IsValid())
		{
			DashboardWidget = FTraceTableDashboardViewFactory::MakeWidget(OwnerTab, SpawnTabArgs);

			if (FilteredEntriesListView.IsValid())
			{
				FilteredEntriesListView->SetSelectionMode(ESelectionMode::SingleToggle);
				FilteredEntriesListView->OnMouseWheelDetected.AddSP(this, &FAudioEventLogDashboardViewFactory::OnUpdateAutoScroll);
			}

			if (HeaderRowWidget.IsValid())
			{
				VisibleColumnsSettingsMenu = MakeShared<FVisibleColumnsSettingsMenu<FAudioEventLogVisibleColumns>>(HeaderRowWidget.ToSharedRef(), FAudioEventLogVisibleColumns());

				VisibleColumnsSettingsMenu->OnVisibleColumnsSettingsUpdated.AddSPLambda(this, []()
				{
#if WITH_EDITOR
					FAudioEventLogSettings::OnRequestWriteSettings.Broadcast();
#endif // WITH_EDITOR	
				});
			}
		}

#if WITH_EDITOR
		// Read the editor settings after the widget has been created
		FAudioEventLogSettings::OnRequestReadSettings.Broadcast();
#endif // WITH_EDITOR

		return DashboardWidget->AsShared();
	}

	void FAudioEventLogDashboardViewFactory::RefreshFilteredEntriesListView()
	{
		FTraceObjectTableDashboardViewFactory::RefreshFilteredEntriesListView();

		if (!FilteredEntriesListView.IsValid())
		{
			return;
		}

		if (FilteredEntriesListView->IsUserScrolling())
		{
			return;
		}

		const TSharedPtr<IDashboardDataViewEntry> FocusedItemShared = FocusedItem.Pin();
		if (FocusedItemShared.IsValid())
		{
			FilteredEntriesListView->RequestScrollIntoView(FocusedItemShared);
		}
		else if (bAutoScroll)
		{
			if (SortMode == EColumnSortMode::Ascending)
			{
				FilteredEntriesListView->ScrollToBottom();
			}
			else if (SortMode == EColumnSortMode::Descending)
			{
				FilteredEntriesListView->ScrollToTop();
			}
		}
	}

	void FAudioEventLogDashboardViewFactory::ProcessEntries(FTraceTableDashboardViewFactory::EProcessReason Reason)
	{
		using namespace AudioEventLogPrivate;

		const FString FilterString = GetSearchFilterText().ToString();
		FTraceTableDashboardViewFactory::FilterEntries<FAudioEventLogTraceProvider>([this, &FilterString](const IDashboardDataViewEntry& Entry)
		{
			UpdateCustomEventLogFilters(Entry);

			const FAudioEventLogDashboardEntry& EventLogEntry = static_cast<const FAudioEventLogDashboardEntry&>(Entry);

			const FEventLogFilterID* FilterID = EventFilterTypes.Find(EventLogEntry.EventName);

			const bool bPassesEventTypeFilter = FilterID ? PassesEventTypeFilter(*FilterID) : true;

			if (bPassesEventTypeFilter)
			{
				if (FilterString.IsNumeric() && LexToString(EventLogEntry.PlayOrder).Equals(FilterString))
				{
					return true;
				}
				else if (EventLogEntry.GetDisplayName().ToString().Contains(FilterString) || EventLogEntry.ActorLabel.Contains(FilterString))
				{
					return true;
				}
			}

			return false;
		});

#if WITH_EDITOR
		if (Reason == EProcessReason::EntriesUpdated && FilteredEntriesListView.IsValid())
		{
			// Reset bCacheStatusIsDirty flag to push icon update to the UI
			for (const TSharedPtr<IDashboardDataViewEntry>& Entry : FilteredEntriesListView->GetItems())
			{
				if (!Entry.IsValid())
				{
					continue;
				}

				FAudioEventLogDashboardEntry& EventLogEntry = CastEntry(*Entry);
				if (EventLogEntry.CachedState == EAudioEventCacheState::NextToBeDeleted)
				{
					EventLogEntry.bCacheStatusIsDirty = false;
				}
				else
				{
					// Only the earliest items in the list will be marked as NextToBeDeleted - no need to process anymore items
					break;
				}
			}
		}
#endif // WITH_EDITOR
	}

	const TMap<FName, FTraceTableDashboardViewFactory::FColumnData>& FAudioEventLogDashboardViewFactory::GetColumns() const
	{
		using namespace AudioEventLogPrivate;

		auto CreateColumnData = [this]()
		{
			FAudioEventLogVisibleColumns VisibleColumns;
			if (VisibleColumnsSettingsMenu.IsValid())
			{
				VisibleColumnsSettingsMenu->WriteToSettings(VisibleColumns);
			}

			return TMap<FName, FTraceTableDashboardViewFactory::FColumnData>
			{
#if WITH_EDITOR
				{
					CacheStatusColumnID,
					{
						FText::GetEmpty(),
						[](const IDashboardDataViewEntry& InData) { return FText::GetEmpty(); }, /* GetDisplayValue */
						[](const IDashboardDataViewEntry& InData) -> FName
						{
							return FName("AudioInsights.Icon.EventLog.MarkedForDelete");
						},
						!VisibleColumns.GetIsVisible(CacheStatusColumnID) /* bDefaultHidden */,
						0.075f /* FillWidth */,
						HAlign_Center /* Alignment */,
						[this](const IDashboardDataViewEntry& InData) -> FLinearColor /* GetIconColor */
						{
							const FAudioEventLogDashboardEntry& Entry = CastEntry(InData);

							switch (Entry.CachedState)
							{
								case EAudioEventCacheState::NextToBeDeleted:
									return Entry.bCacheStatusIsDirty ? FLinearColor::Transparent : FLinearColor::White;
								default:
									break;
							}

							return FLinearColor::Transparent;
						},
						[](const IDashboardDataViewEntry& InData) -> FText /* GetIconTooltip */
						{
							const FAudioEventLogDashboardEntry& Entry = CastEntry(InData);

							switch (Entry.CachedState)
							{
								case EAudioEventCacheState::NextToBeDeleted:
									return LOCTEXT("AudioEventLog_NextToBeDeleted", "This event has been marked for deletion the next time the cache is full.");
								default:
									break;
							}

							return FText::GetEmpty();
						}
					}
				},
#endif // WITH_EDITOR
				{
					MessageIDColumnID,
					{
						LOCTEXT("AudioEventLog_MessageIDColumnDisplayName", "ID"),
						[](const IDashboardDataViewEntry& InData) { return FText::AsNumber(CastEntry(InData).MessageID); },
						nullptr /* GetIconName */,
						!VisibleColumns.GetIsVisible(MessageIDColumnID) /* bDefaultHidden */,
						0.1f /* FillWidth */
					}
				},
				{
					TimestampColumnID,
					{
						LOCTEXT("AudioEventLog_TimestampColumnDisplayName", "Timestamp"),
						[this](const IDashboardDataViewEntry& InData) { return FText::AsNumber(GetTimestampRelativeToAnalysisStart(CastEntry(InData).Timestamp), FSlateStyle::Get().GetTimeFormat()); },
						nullptr /* GetIconName */,
						!VisibleColumns.GetIsVisible(TimestampColumnID) /* bDefaultHidden */,
						0.15f /* FillWidth */
					}
				},
				{
					PlayOrderColumnID,
					{
						LOCTEXT("AudioEventLog_PlayOrderColumnDisplayName", "Play Order"),
						[](const IDashboardDataViewEntry& InData)
						{ 
							const uint32 PlayOrder = AudioEventLogPrivate::CastEntry(InData).PlayOrder;
							if (PlayOrder == INDEX_NONE)
							{
								return FText::GetEmpty();
							}

							return FText::AsNumber(AudioEventLogPrivate::CastEntry(InData).PlayOrder);
						},
						nullptr /* GetIconName */,
						!VisibleColumns.GetIsVisible(PlayOrderColumnID) /* bDefaultHidden */,
						0.15f /* FillWidth */
					}
				},
				{
					EventColumnID,
					{
						LOCTEXT("AudioEventLog_EventColumnDisplayName", "Event"),
						[](const IDashboardDataViewEntry& InData) { return FText::FromString(CastEntry(InData).EventName); },
						nullptr /* GetIconName */,
						!VisibleColumns.GetIsVisible(EventColumnID) /* bDefaultHidden */,
						0.2f  /* FillWidth */
					}
				},
				{
					AssetNameColumnID,
					{
						LOCTEXT("AudioEventLog_AssetColumnDisplayName", "Asset"),
						[](const IDashboardDataViewEntry& InData) { return CastEntry(InData).GetDisplayName(); },
						[](const IDashboardDataViewEntry& InData) -> FName
						{
							const FAudioEventLogDashboardEntry& Entry = CastEntry(InData);

							switch (Entry.Category)
							{
								case EAudioEventLogSoundCategory::MetaSound:
									return FName("AudioInsights.Icon.SoundDashboard.MetaSound");
								case EAudioEventLogSoundCategory::SoundCue:
									return FName("AudioInsights.Icon.SoundDashboard.SoundCue");
								case EAudioEventLogSoundCategory::ProceduralSource:
									return FName("AudioInsights.Icon.SoundDashboard.ProceduralSource");
								case EAudioEventLogSoundCategory::SoundWave:
									return FName("AudioInsights.Icon.SoundDashboard.SoundWave");
								case EAudioEventLogSoundCategory::SoundCueTemplate:
									return FName("AudioInsights.Icon.SoundDashboard.SoundCue");
								case EAudioEventLogSoundCategory::None:
								default:
									break;
							}

							return NAME_None;
						},
						!VisibleColumns.GetIsVisible(AssetNameColumnID) /* bDefaultHidden */,
						0.3f  /* FillWidth */
					}
				},
				{
					ActorColumnID,
					{
						LOCTEXT("AudioEventLog_ActorColumnDisplayName", "Actor"),
						[](const IDashboardDataViewEntry& InData) { return FText::FromString(CastEntry(InData).ActorLabel); },
						[](const IDashboardDataViewEntry& InData) -> FName
						{
							return CastEntry(InData).ActorIconName;
						},
						!VisibleColumns.GetIsVisible(ActorColumnID) /* bDefaultHidden */,
						0.3f  /* FillWidth */
					}
				},
				{
					CategoryColumnID,
					{
						LOCTEXT("AudioEventLog_CategoryColumnDisplayName", "Category"),
						[](const IDashboardDataViewEntry& InData) { return FText::FromString(CastEntry(InData).CategoryName); },
						nullptr /* GetIconName */,
						!VisibleColumns.GetIsVisible(CategoryColumnID) /* bDefaultHidden */,
						0.3f  /* FillWidth */
					}
				}
			};
		};

		static const TMap<FName, FTraceTableDashboardViewFactory::FColumnData> ColumnData = CreateColumnData();
		return ColumnData;
	}

	void FAudioEventLogDashboardViewFactory::SortTable()
	{
		using namespace AudioEventLogPrivate;

		auto SortByMessageID = [](const FAudioEventLogDashboardEntry& First, const FAudioEventLogDashboardEntry& Second)
		{
			return First.MessageID < Second.MessageID;
		};

		auto SortByTimestamp = [&SortByMessageID](const FAudioEventLogDashboardEntry& First, const FAudioEventLogDashboardEntry& Second)
		{
			const double ComparisonDiff = First.Timestamp - Second.Timestamp;

			if (FMath::IsNearlyZero(ComparisonDiff, UE_KINDA_SMALL_NUMBER))
			{
				return SortByMessageID(First, Second);
			}

			return ComparisonDiff < 0.0;
		};

		SortByPredicate(SortByTimestamp);
	}

	bool FAudioEventLogDashboardViewFactory::IsColumnSortable(const FName& ColumnId) const
	{
		using namespace AudioEventLogPrivate;

		return ColumnId == TimestampColumnID;
	}

	void FAudioEventLogDashboardViewFactory::OnHiddenColumnsListChanged()
	{
		if (VisibleColumnsSettingsMenu.IsValid())
		{
			VisibleColumnsSettingsMenu->OnHiddenColumnsListChanged();
		}
	}

	TSharedPtr<SWidget> FAudioEventLogDashboardViewFactory::CreateFilterBarButtonWidget()
	{
		if (!FilterBarButton.IsValid())
		{
			CreateFilterBarWidget();
			FilterBarButton = SBasicFilterBar<FEventLogFilterID>::MakeAddFilterButton(StaticCastSharedPtr<SAudioFilterBar<FEventLogFilterID>>(FilterBar).ToSharedRef()).ToSharedPtr();
		}

		return FilterBarButton;
	}

	TSharedRef<SWidget> FAudioEventLogDashboardViewFactory::CreateFilterBarWidget()
	{
		using namespace AudioEventLogPrivate;

		if (!FilterBar.IsValid())
		{
			TArray<TSharedRef<FFilterBase<FEventLogFilterID>>> Filters;

			for (const auto& [Category, EventTypes] : InitEventTypeFilters)
			{
				const TSharedPtr<FFilterCategory>& FilterCategory = FilterCategories.FindOrAdd(Category, MakeShared<FFilterCategory>(FText::FromString(Category), FText::GetEmpty()));
				for (const FString& EventType : EventTypes)
				{
					Filters.Add(CreateNewEventFilterType(FilterCategory, EventType));
				}
			}
			
			SAssignNew(FilterBar, SAudioFilterBar<FEventLogFilterID>)
			.CustomFilters(Filters)
			.UseSectionsForCategories(false)
			.Visibility(EVisibility::Collapsed) // Filter bar buttons intentionally hidden in UI
			.OnFilterChanged_Lambda([this, Filters]()
			{
				UpdateFilterReason = EProcessReason::FilterUpdated;
			});
		}

		return FilterBar.ToSharedRef();
	}

	TSharedRef<SWidget> FAudioEventLogDashboardViewFactory::CreateSettingsButtonWidget()
	{
		if (!SettingsAreaWidget.IsValid())
		{
#if WITH_EDITOR
			constexpr FLinearColor StatTitleColor(1.0f, 1.0f, 1.0f, 0.5f);
#endif // WITH_EDITOR

			SAssignNew(SettingsAreaWidget, SHorizontalBox)
			
#if WITH_EDITOR
			// Cache paused icon
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.ToolTipText(LOCTEXT("AudioEventLog_CacheStats_PausedTooltip", "Caching has been paused - Audio Insights is no longer collecting data from the current session.\n\nClick/ESC to resume."))
				.Visibility_Lambda([]()
				{
					FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();
					return AudioInsightsModule.GetTimingViewExtender().GetMessageCacheAndProcessingStatus() == ECacheAndProcess::None ? EVisibility::Visible : EVisibility::Hidden;
				})
				.OnClicked_Lambda([this]()
				{
					ResetInspectTimestamp();
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FSlateStyle::Get().GetBrush("AudioInsights.Icon.Pause"))
					.ColorAndOpacity(StatTitleColor)
					.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
				]
				
			]

			// Cache stats
			+ SHorizontalBox::Slot()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(StatTitleColor)
				.Text(LOCTEXT("AudioEventLog_CacheStats_Title", "Cache Size: "))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Center)
				.Text_Lambda([]()
				{
					FAudioInsightsCacheManager& CacheManager = FAudioInsightsModule::GetChecked().GetCacheManager();
					const float CacheSizeMb = static_cast<float>(CacheManager.GetUsedCacheSize()) / 1000000.0f;
					const float MaxCacheSizeMb = static_cast<float>(CacheManager.GetMaxCacheSize()) / 1000000.0f;

					return FText::Format(LOCTEXT("AudioEventLog_CacheStats_Memory", "{0} / {1}")
										, FText::AsNumber(CacheSizeMb, FSlateStyle::Get().GetMemoryFormat())
										, FSlateStyle::Get().FormatMemoryAsMegabytes(MaxCacheSizeMb));
				})
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(StatTitleColor)
				.Text(LOCTEXT("AudioEventLog_CacheStats_Duration", " : Duration: "))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Justification(ETextJustify::Center)
				.Text_Lambda([]()
				{
					FAudioInsightsCacheManager& CacheManager = FAudioInsightsModule::GetChecked().GetCacheManager();
					const float CacheDuration = CacheManager.GetCacheDuration();

					return FText::Format(LOCTEXT("AudioEventLog_CacheStats_DurationValue", "{0}s")
										, FText::AsNumber(CacheDuration, FSlateStyle::Get().GetShortTimeFormat()));
				})
			]
#endif // WITH_EDITOR

			// Clear log Button
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(15.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.ToolTipText(AudioEventLogPrivate::ClearLogTooltipText)
				.IsEnabled_Lambda([]()
				{
					return AudioEventLogPrivate::CanClearLog();
				})
				.OnClicked_Lambda([this]()
				{
					ClearActiveAudioDeviceEntries();

					return FReply::Handled();
				})
				[
					SNew(SBox)
					[
						SNew(SHorizontalBox)
						// Clear log icon
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FSlateStyle::Get().GetBrush("AudioInsights.Icon.ClearLog"))
						]
						// Clear log text
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(6.0f, 1.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Justification(ETextJustify::Center)
							.Text(AudioEventLogPrivate::ClearLogText)
						]
					]
				]
			]

			// Settings button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			.HAlign(HAlign_Right)
			[
				FTraceObjectTableDashboardViewFactory::CreateSettingsButtonWidget()
			];
		}

		return SettingsAreaWidget.ToSharedRef();
	}

	TSharedRef<SWidget> FAudioEventLogDashboardViewFactory::OnGetSettingsMenuContent()
	{
		FMenuBuilder MenuBuilder(false /*bShouldCloseWindowAfterMenuSelection*/, CommandList);

		MenuBuilder.BeginSection("AudioEventLogSettingActions", LOCTEXT("AudioEventLog_Settings_HeaderText", "Settings"));
		{
			// Visible columns
			MenuBuilder.AddSubMenu
			(
				LOCTEXT("AudioEventLog_Settings_VisibleColumns", "Visible Columns"),
				LOCTEXT("AudioEventLog_Settings_VisibleColumnsTooltip", "Show/hide columns"),
				FNewMenuDelegate::CreateRaw(this, &FAudioEventLogDashboardViewFactory::BuildVisibleColumnsMenuContent)
			);

#if WITH_EDITOR
			// Auto-stop monitoring
			MenuBuilder.AddSubMenu
			(
				LOCTEXT("AudioEventLog_Settings_AutoStopCaching", "Automatically Stop Caching"),
				LOCTEXT("AudioEventLog_Settings_AutoStopCachingTooltip", "Choose if/how the event log automatically stops caching when inspecting an event."),
				FNewMenuDelegate::CreateRaw(this, &FAudioEventLogDashboardViewFactory::BuildAutoStopCachingMenuContent)
			);
#endif // WITH_EDITOR
		}

		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	FText FAudioEventLogDashboardViewFactory::OnGetSettingsMenuToolTip()
	{
		return LOCTEXT("AudioEventLog_Settings_TooltipText", "Event log settings");
	}

	TSharedPtr<SWidget> FAudioEventLogDashboardViewFactory::OnConstructContextMenu()
	{
		constexpr bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

#if WITH_EDITOR
		const FAudioEventLogEditorCommands& Commands = FAudioEventLogEditorCommands::Get();

		MenuBuilder.BeginSection("EventLogDashboardActions", LOCTEXT("EventLogDashboard_Actions_HeaderText", "Editor Options"));
		{
			MenuBuilder.AddMenuEntry(Commands.GetBrowseCommand(), NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateStyle::Get().CreateIcon("AudioInsights.Icon.SoundDashboard.Browse"));
			MenuBuilder.AddMenuEntry(Commands.GetEditCommand(), NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateStyle::Get().CreateIcon("AudioInsights.Icon.SoundDashboard.Edit"));
		}

		MenuBuilder.EndSection();

		MenuBuilder.AddSeparator();
#endif // WITH_EDITOR

		MenuBuilder.AddMenuEntry(
			AudioEventLogPrivate::ClearLogText,
			AudioEventLogPrivate::ClearLogTooltipText,
			FSlateStyle::Get().CreateIcon("AudioInsights.Icon.ClearLog"),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAudioEventLogDashboardViewFactory::ClearActiveAudioDeviceEntries),
				FCanExecuteAction::CreateLambda([]()
				{
					return AudioEventLogPrivate::CanClearLog();
				})
			)
		);

		return MenuBuilder.MakeWidget();
	}

	void FAudioEventLogDashboardViewFactory::ClearActiveAudioDeviceEntries()
	{
		if (AudioEventLogTraceProvider.IsValid())
		{
			AudioEventLogTraceProvider->ClearActiveAudioDeviceEntries();
			DataViewEntries.Reset();

			if (FilteredEntriesListView.IsValid())
			{
				RefreshFilteredEntriesListView();
			}
		}
	}

	void FAudioEventLogDashboardViewFactory::OnUpdateAutoScroll()
	{
		if (!FilteredEntriesListView.IsValid())
		{
			return;
		}

		const TArrayView<const TSharedPtr<IDashboardDataViewEntry>> FilteredItems = FilteredEntriesListView->GetItems();
		const int32 NumItems = FilteredItems.Num();
		if (NumItems == 0)
		{
			return;
		}

		if (SortMode == EColumnSortMode::Ascending)
		{
			bAutoScroll = FilteredEntriesListView->IsItemVisible(FilteredItems[NumItems - 1]);
		}
		else if (SortMode == EColumnSortMode::Descending)
		{
			bAutoScroll = FilteredEntriesListView->IsItemVisible(FilteredItems[0]);
		}
	}

	double FAudioEventLogDashboardViewFactory::GetTimestampRelativeToAnalysisStart(const double Timestamp) const
	{
		return Timestamp - BeginTimestamp;
	}

	void FAudioEventLogDashboardViewFactory::OnSelectionChanged(TSharedPtr<IDashboardDataViewEntry> SelectedItem, ESelectInfo::Type SelectInfo)
	{
		using namespace AudioEventLogPrivate;

		if (!FilteredEntriesListView.IsValid())
		{
			return;
		}

		if (!SelectedItem.IsValid())
		{
			ResetInspectTimestamp();
			return;
		}

		FocusedItem = SelectedItem;

		const FAudioEventLogDashboardEntry& SelectedEventLogEntry = CastEntry(*SelectedItem);
		if (SelectedEventLogEntry.PlayOrder == INDEX_NONE)
		{
			FilteredEntriesListView->ClearHighlightedItems();
		}
		else
		{
			for (const TSharedPtr<IDashboardDataViewEntry>& Entry : DataViewEntries)
			{
				if (!Entry.IsValid())
				{
					continue;
				}

				FilteredEntriesListView->SetItemHighlighted(Entry, CastEntry(*Entry).PlayOrder == SelectedEventLogEntry.PlayOrder);
			}
		}

		if (SelectInfo != ESelectInfo::Type::Direct)
		{
			FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();
			AudioInsightsModule.GetTimingViewExtender().SetTimeMarker(SelectedEventLogEntry.Timestamp, ESystemControllingTimeMarker::EventLog);
		}
	}

	void FAudioEventLogDashboardViewFactory::OnListViewScrolled(double InScrollOffset)
	{
		if (!FilteredEntriesListView.IsValid())
		{
			return;
		}

		if (FilteredEntriesListView->IsUserScrolling())
		{
			OnUpdateAutoScroll();
		}
	}

	void FAudioEventLogDashboardViewFactory::OnFinishedScrolling()
	{
		if (FilteredEntriesListView.IsValid() && FilteredEntriesListView->GetNumItemsBeingObserved() > 0)
		{
			const TSharedPtr<IDashboardDataViewEntry> FocusedItemShared = FocusedItem.Pin();
			
			// Only set a focused item if the user has previously focused on a row
			if (FocusedItemShared.IsValid())
			{
				for (const TSharedPtr<IDashboardDataViewEntry>& Entry : FilteredEntriesListView->GetItems())
				{
					if (FilteredEntriesListView->IsItemVisible(Entry))
					{
						FocusedItem = Entry;
						break;
					}
				}
			}
		}
	}

	const FTableRowStyle* FAudioEventLogDashboardViewFactory::GetRowStyle() const
	{
		return &FSlateStyle::Get().GetWidgetStyle<FTableRowStyle>("TreeDashboard.TableViewRow");
	}

	void FAudioEventLogDashboardViewFactory::OnAnalysisStarting(const double Timestamp)
	{
#if WITH_EDITOR
		BeginTimestamp = Timestamp - GStartTime;
#else
		BeginTimestamp = 0.0;
#endif // WITH_EDITOR
	}

	void FAudioEventLogDashboardViewFactory::UpdateCustomEventLogFilters(const IDashboardDataViewEntry& Entry)
	{
		using namespace AudioEventLogPrivate;

		const FAudioEventLogDashboardEntry& EventLogEntry = CastEntry(Entry);

		if (!EventFilterTypes.Contains(EventLogEntry.EventName))
		{
			CreateFilterBarWidget();

			const TSharedPtr<FFilterCategory>& CustomCategory = FilterCategories.FindOrAdd(CategoryCustom, MakeShared<FFilterCategory>(FText::FromString(CategoryCustom), FText::GetEmpty()));
			FilterBar->AddFilter(CreateNewEventFilterType(CustomCategory, EventLogEntry.EventName));
		}
	}

	TSharedRef<FAudioEventLogFilter> FAudioEventLogDashboardViewFactory::CreateNewEventFilterType(const TSharedPtr<FFilterCategory>& FilterCategory, const FString& EventType)
	{
		static FEventLogFilterID FilterID = 0;

		TSharedPtr<FAudioEventLogFilter> Filter = MakeShared<FAudioEventLogFilter>(FilterID,
																				   EventType,
																				   FText::FromString(EventType),
																				   NAME_None,
																				   FText::GetEmpty(),
																				   FLinearColor::MakeRandomColor(),
																				   FilterCategory);
		EventFilterTypes.Add(EventType, FilterID++);
		return Filter.ToSharedRef();
	}

	bool FAudioEventLogDashboardViewFactory::PassesEventTypeFilter(const FEventLogFilterID EventLogID) const
	{
		if (!FilterBar.IsValid())
		{
			return true;
		}

		TSharedPtr<TFilterCollection<FEventLogFilterID>> ActiveFilters = FilterBar->GetAllActiveFilters();
		
		if (!ActiveFilters.IsValid() || ActiveFilters->Num() == 0)
		{
			return true;
		}
		
		for (int32 FilterIndex = 0; FilterIndex < ActiveFilters->Num(); ++FilterIndex)
		{
			if (ActiveFilters->GetFilterAtIndex(FilterIndex)->PassesFilter(EventLogID))
			{
				return true;
			}
		}

		return false;
	}

	void FAudioEventLogDashboardViewFactory::BuildVisibleColumnsMenuContent(FMenuBuilder& MenuBuilder)
	{
		if (VisibleColumnsSettingsMenu.IsValid())
		{
			VisibleColumnsSettingsMenu->BuildVisibleColumnsMenuContent(MenuBuilder);
		}
	}

	void FAudioEventLogDashboardViewFactory::BuildAutoStopCachingMenuContent(FMenuBuilder& MenuBuilder)
	{
		const FAudioEventLogEditorCommands& Commands = FAudioEventLogEditorCommands::Get();

		MenuBuilder.AddMenuEntry(Commands.GetAutoStopCachingWhenLastInCacheCommand());
		MenuBuilder.AddMenuEntry(Commands.GetAutoStopCachingOnInspectCommand());
		MenuBuilder.AddMenuEntry(Commands.GetAutoStopCachingDisabledCommand());
	}

	void FAudioEventLogDashboardViewFactory::SortByPredicate(TFunctionRef<bool(const FAudioEventLogDashboardEntry&, const FAudioEventLogDashboardEntry&)> Predicate)
	{
		using namespace AudioEventLogPrivate;

		auto SortDashboardEntries = [this](const TSharedPtr<IDashboardDataViewEntry>& First, const TSharedPtr<IDashboardDataViewEntry>& Second, TFunctionRef<bool(const FAudioEventLogDashboardEntry&, const FAudioEventLogDashboardEntry&)> Predicate)
		{
			return Predicate(CastEntry(*First), CastEntry(*Second));
		};

		if (SortMode == EColumnSortMode::Ascending)
		{
			DataViewEntries.Sort([&SortDashboardEntries, &Predicate](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
			{
				return SortDashboardEntries(A, B, Predicate);
			});
		}
		else if (SortMode == EColumnSortMode::Descending)
		{
			DataViewEntries.Sort([&SortDashboardEntries, &Predicate](const TSharedPtr<IDashboardDataViewEntry>& A, const TSharedPtr<IDashboardDataViewEntry>& B)
			{
				return SortDashboardEntries(B, A, Predicate);
			});
		}
	}

#if WITH_EDITOR
	void FAudioEventLogDashboardViewFactory::OnReadEditorSettings(const FAudioEventLogSettings& InSettings)
	{
		if (FilterBar.IsValid())
		{
			RemoveDeletedCustomEvents(InSettings.CustomCategoriesToEvents);
			AddNewCustomEvents(InSettings.CustomCategoriesToEvents);

			CachedCustomEventSettings = InSettings.CustomCategoriesToEvents;
			
			RefreshFilterBarFromSettings(InSettings.EventFilters);
		}

		if (VisibleColumnsSettingsMenu.IsValid())
		{
			VisibleColumnsSettingsMenu->ReadFromSettings(InSettings.VisibleColumns);
		}
	}

	void FAudioEventLogDashboardViewFactory::OnWriteEditorSettings(FAudioEventLogSettings& OutSettings)
	{
		if (FilterBar.IsValid())
		{
			TSharedPtr<TFilterCollection<FEventLogFilterID>> ActiveFilters = FilterBar->GetAllActiveFilters();

			if (!ActiveFilters.IsValid() || ActiveFilters->Num() == 0)
			{
				OutSettings.EventFilters.Empty();
			}
			else
			{
				for (const auto& [EventName, FilterID] : EventFilterTypes)
				{
					if (PassesEventTypeFilter(FilterID))
					{
						OutSettings.EventFilters.Add(EventName);
					}
					else
					{
						OutSettings.EventFilters.Remove(EventName);
					}
				}
			}
		}

		if (VisibleColumnsSettingsMenu.IsValid())
		{
			VisibleColumnsSettingsMenu->WriteToSettings(OutSettings.VisibleColumns);
		}
	}

	void FAudioEventLogDashboardViewFactory::OnCacheChunkOverwritten(const double NewCacheStartTimestamp)
	{
		if (IsInGameThread())
		{
			UpdateCacheMessageProcessMethod();
		}
		else
		{
			ExecuteOnGameThread(TEXT("FAudioEventLogDashboardViewFactory::UpdateCacheMessageProcessMethod"), [this] { UpdateCacheMessageProcessMethod(); });
		}
	}

	void FAudioEventLogDashboardViewFactory::RemoveDeletedCustomEvents(const TMap<FString, FAudioEventLogCustomEvents>& CustomEventsFromSettings)
	{
		if (!FilterBar.IsValid())
		{
			return;
		}

		// Run through our cached custom events, remove any that are not in the settings
		for (const auto& [CachedCategory, CachedEventTypes] : CachedCustomEventSettings)
		{
			// Can be null if the category has been deleted
			const FAudioEventLogCustomEvents* FoundEventTypesFromSettingsCategory = CustomEventsFromSettings.Find(CachedCategory);

			int32 NumRemoved = 0;
			for (const FString& CachedEvent : CachedEventTypes.EventNames)
			{
				if (FoundEventTypesFromSettingsCategory == nullptr || !FoundEventTypesFromSettingsCategory->EventNames.Contains(CachedEvent))
				{
					// The category or event has been destroyed, remove it here
					EventFilterTypes.Remove(CachedEvent);
					NumRemoved++;

					if (const TSharedPtr<FFilterBase<FEventLogFilterID>> Filter = FilterBar->GetFilter(CachedEvent))
					{
						FilterBar->DeleteFromFilter(Filter.ToSharedRef());
					}
				}
			}

			// If every event in this category has been deleted, remove the category too
			if (NumRemoved == CachedEventTypes.EventNames.Num())
			{
				if (const TSharedPtr<FFilterCategory>* FilterCategory = FilterCategories.Find(CachedCategory))
				{
					FilterBar->DeleteCategory(FilterCategory->ToSharedRef());
					FilterCategories.Remove(CachedCategory);
				}
			}
		}
	}

	void FAudioEventLogDashboardViewFactory::AddNewCustomEvents(const TMap<FString, FAudioEventLogCustomEvents>& CustomEventsFromSettings)
	{
		if (!FilterBar.IsValid())
		{
			return;
		}

		// Run through all of the events in the settings - detect any new ones and add them to the filter
		for (const auto& [SettingsCategoryName, SettingsEventTypes] : CustomEventsFromSettings)
		{
			if (SettingsEventTypes.EventNames.IsEmpty())
			{
				// Do not re-add the category if the event name collection is empty
				continue;
			}

			// Can be null if the category does not exist yet
			const FAudioEventLogCustomEvents* FoundEventTypesFromCachedCategory = CachedCustomEventSettings.Find(SettingsCategoryName);

			// Find the category if it exists, or create a new one if it doesn't
			const TSharedPtr<FFilterCategory>& FilterCategory = FilterCategories.FindOrAdd(SettingsCategoryName, MakeShared<FFilterCategory>(FText::FromString(SettingsCategoryName), FText::GetEmpty()));

			for (const FString& SettingsEvent : SettingsEventTypes.EventNames)
			{
				if (FoundEventTypesFromCachedCategory == nullptr || !FoundEventTypesFromCachedCategory->EventNames.Contains(SettingsEvent))
				{
					// This is a new category or event, add the event to the filter
					FilterBar->AddFilter(CreateNewEventFilterType(FilterCategory, SettingsEvent));
				}
			}
		}
	}

	void FAudioEventLogDashboardViewFactory::RefreshFilterBarFromSettings(const TSet<FString>& EventFilters)
	{
		if (!FilterBar.IsValid())
		{
			return;
		}

		if (EventFilters.IsEmpty())
		{
			FilterBar->RemoveAllFilters();
		}
		else
		{
			for (const auto& [EventName, FilterID] : EventFilterTypes)
			{
				const bool bFilterIsActive = EventFilters.Contains(EventName);
				FilterBar->SetFilterCheckState(FilterBar->GetFilter(EventName), bFilterIsActive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
			}
		}
	}
#endif // WITH_EDITOR

	void FAudioEventLogDashboardViewFactory::OnTimingViewTimeMarkerChanged(double InTimeMarker)
	{
		using namespace AudioEventLogPrivate;

#if WITH_EDITOR
		FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();

		if (AutoStopCachingMode == EAutoStopCachingMode::OnInspect)
		{
			AudioInsightsModule.GetTimingViewExtender().StopCachingAndProcessingNewMessages();
			bAutoScroll = false;
		}
		else if (AutoStopCachingMode == EAutoStopCachingMode::WhenInLastChunk)
		{
			if (TimestampIsInChunkMarkedToDelete(InTimeMarker))
			{
				AudioInsightsModule.GetTimingViewExtender().StopCachingAndProcessingNewMessages();
				bAutoScroll = false;
			}
			else if (AudioInsightsModule.GetTimingViewExtender().GetMessageCacheAndProcessingStatus() != ECacheAndProcess::None)
			{
				AudioInsightsModule.GetTimingViewExtender().StopProcessingNewMessages();
			}
		}
		else
		{
			AudioInsightsModule.GetTimingViewExtender().StopProcessingNewMessages();
		}
#endif // WITH_EDITOR

		bAutoScroll = false;

		if (!FilteredEntriesListView.IsValid())
		{
			return;
		}

		// We assume the data entries are in timestamp order (earliest to latest)
		for (int32 Index = 0; Index < DataViewEntries.Num(); ++Index)
		{
			const TSharedPtr<IDashboardDataViewEntry>& Entry = DataViewEntries[Index];
			if (!Entry.IsValid())
			{
				continue;
			}

			const FAudioEventLogDashboardEntry& EventLogEntry = CastEntry(*Entry);
			if (EventLogEntry.Timestamp == InTimeMarker)
			{
				FilteredEntriesListView->SetSelection(Entry);
				break;
			}
			else if (EventLogEntry.Timestamp > InTimeMarker)
			{
				const int32 SelectedIndex = Index - 1;
				if (SelectedIndex < 0)
				{
					FilteredEntriesListView->ClearSelection();
				}
				else
				{
					FilteredEntriesListView->SetSelection(DataViewEntries[SelectedIndex]);
				}

				break;
			}
			// If we've reached the last entry, the timemarker must be beyond the final entry
			// Select the last entry in the list in this case
			else if (Index == DataViewEntries.Num() - 1)
			{
				FilteredEntriesListView->SetSelection(Entry);
			}
		}
	}

	void FAudioEventLogDashboardViewFactory::UpdateCacheMessageProcessMethod()
	{
		using namespace AudioEventLogPrivate;

#if WITH_EDITOR
		if (AutoStopCachingMode != EAutoStopCachingMode::WhenInLastChunk || !FilteredEntriesListView.IsValid())
		{
			return;
		}

		FAudioInsightsModule& AudioInsightsModule = FAudioInsightsModule::GetChecked();
		if (AudioInsightsModule.GetTimingViewExtender().GetMessageCacheAndProcessingStatus() != ECacheAndProcess::CacheLatestNoProcess)
		{
			return;
		}

		const TArray<TSharedPtr<IDashboardDataViewEntry>>& SelectedItems = FilteredEntriesListView->GetSelectedItems();
		if (SelectedItems.Num() == 0 || !SelectedItems[0].IsValid())
		{
			return;
		}

		const FAudioEventLogDashboardEntry& SelectedEventLogEntry = CastEntry(*SelectedItems[0]);

		if (TimestampIsInChunkMarkedToDelete(SelectedEventLogEntry.Timestamp))
		{
			AudioInsightsModule.GetTimingViewExtender().StopCachingAndProcessingNewMessages();

			if (FilteredEntriesListView->IsItemVisible(SelectedItems[0]))
			{
				// Make sure the selected row remains visible if the top of the cache is removed
				FocusedItem = SelectedItems[0];
			}
		}
#endif // WITH_EDITOR
	}

} // namespace UE::Audio::Insights

#undef LOCTEXT_NAMESPACE
