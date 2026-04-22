// Copyright Epic Games, Inc. All Rights Reserved.

#include "Providers/AudioBusTraceProvider.h"

#include "AudioInsightsModule.h"
#include "Async/ParallelFor.h"
#include "Cache/AudioInsightsCacheManager.h"

#if WITH_EDITOR
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#else
#include "Common/PagedArray.h"
#endif // WITH_EDITOR

namespace UE::Audio::Insights
{
	namespace FAudioBusTraceProviderPrivate
	{
#if !WITH_EDITOR
		template<typename T>
		const T* FindClosestMessageToTimestamp(const TraceServices::TPagedArray<T>& InCachedMessages, const double InTimeMarker, const uint32 InAudioBusId)
		{
			const int32 ClosestMessageToTimeStampIndex = TraceServices::PagedArrayAlgo::BinarySearchClosestBy(InCachedMessages, InTimeMarker, [](const T& Msg) { return Msg.Timestamp; });

			// Iterate backwards from TimeMarker until we find the matching AudioBusId
			for (auto It = InCachedMessages.GetIteratorFromItem(ClosestMessageToTimeStampIndex); It; --It)
			{
				if (It->AudioBusId == InAudioBusId)
				{
					return &(*It);
				}
			}

			return nullptr;
		}
#else
		TSharedPtr<FAudioBusDashboardEntry> CreateEntryFromAsset(const FString& InAssetPath)
		{
			TSharedPtr<FAudioBusDashboardEntry> Entry = MakeShared<FAudioBusDashboardEntry>();
			
			const TObjectPtr<UAudioBus> AudioBus = Cast<UAudioBus>(FSoftObjectPath(InAssetPath).ResolveObject());
	
			Entry->Timestamp = FPlatformTime::Seconds() - GStartTime;
			Entry->AudioBusId = AudioBus ? AudioBus->GetUniqueID() : INDEX_NONE;

			Entry->Name = InAssetPath;

			Entry->AudioMeterInfo->NumChannels = AudioBus ? AudioBus->GetNumChannels() : 0;
			Entry->AudioBusType = EAudioBusType::AssetBased;

			return Entry;
		}
#endif // !WITH_EDITOR
	}

	FAudioBusTraceProvider::FAudioBusTraceProvider()
		: TDeviceDataMapTraceProvider<uint32, TSharedPtr<FAudioBusDashboardEntry>>(GetName_Static())
	{
#if WITH_EDITOR
		AudioBusAssetProvider.OnAssetAdded.BindRaw(this, &FAudioBusTraceProvider::HandleOnAudioBusAssetAdded);
		AudioBusAssetProvider.OnAssetRemoved.BindRaw(this, &FAudioBusTraceProvider::HandleOnAudioBusAssetRemoved);
		AudioBusAssetProvider.OnAssetListUpdated.BindRaw(this, &FAudioBusTraceProvider::HandleOnAudioBusAssetListUpdated);
#endif // WITH_EDITOR
	}

	FAudioBusTraceProvider::~FAudioBusTraceProvider()
	{
#if WITH_EDITOR
		AudioBusAssetProvider.OnAssetAdded.Unbind();
		AudioBusAssetProvider.OnAssetRemoved.Unbind();
		AudioBusAssetProvider.OnAssetListUpdated.Unbind();
#endif // WITH_EDITOR
	}
	
	FName FAudioBusTraceProvider::GetName_Static()
	{
		static const FLazyName AudioBusTraceProviderName = "AudioBusProvider";
		return AudioBusTraceProviderName;
	}

	UE::Trace::IAnalyzer* FAudioBusTraceProvider::ConstructAnalyzer(TraceServices::IAnalysisSession& InSession)
	{
		class FAudioBusTraceAnalyzer : public FTraceAnalyzerBase
		{
		public:
			FAudioBusTraceAnalyzer(TSharedRef<FAudioBusTraceProvider> InProvider, TraceServices::IAnalysisSession& InSession)
				: FTraceAnalyzerBase(InProvider)
				, Session(InSession)
			{
			}

			virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override
			{
				FTraceAnalyzerBase::OnAnalysisBegin(Context);

				UE::Trace::IAnalyzer::FInterfaceBuilder& Builder = Context.InterfaceBuilder;

				Builder.RouteEvent(RouteId_Start,                   "Audio", "AudioBusStart");
				Builder.RouteEvent(RouteId_HasActivity,             "Audio", "AudioBusHasActivity");
				Builder.RouteEvent(RouteId_EnvelopeFollowerEnabled, "Audio", "AudioBusEnvelopeFollowerEnabled");
				Builder.RouteEvent(RouteId_EnvelopeValues,          "Audio", "AudioBusEnvelopeValues");
				Builder.RouteEvent(RouteId_Stop,                    "Audio", "AudioBusStop");
			}

			virtual bool OnHandleEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override
			{
				LLM_SCOPE_BYNAME(TEXT("Insights/FAudioBusTraceAnalyzer"));

				FAudioBusMessages& Messages = GetProvider<FAudioBusTraceProvider>().TraceMessages;

				switch (RouteId)
				{
					case RouteId_Start:
					{
#if WITH_EDITOR
						CacheMessage<FAudioBusStartMessage>(Context, Messages.StartMessages);
#else
						Messages.StartMessages.Enqueue(FAudioBusStartMessage{ Context });
#endif // WITH_EDITOR
						break;
					}

					case RouteId_HasActivity:
					{
#if WITH_EDITOR
						CacheMessage<FAudioBusHasActivityMessage>(Context, Messages.HasActivityMessages);
#else
						Messages.HasActivityMessages.Enqueue(FAudioBusHasActivityMessage{ Context });
#endif // WITH_EDITOR
						break;
					}

					case RouteId_EnvelopeFollowerEnabled:
					{
#if WITH_EDITOR
						CacheMessage<FAudioBusEnvelopeFollowerEnabledMessage>(Context, Messages.EnvelopeFollowerEnabledMessages);
#else
						Messages.EnvelopeFollowerEnabledMessages.Enqueue(FAudioBusEnvelopeFollowerEnabledMessage{ Context });
#endif // WITH_EDITOR
						break;
					}

					case RouteId_EnvelopeValues:
					{
#if WITH_EDITOR
						CacheMessage<FAudioBusEnvelopeValuesMessage>(Context, Messages.EnvelopeValuesMessages);
#else
						Messages.EnvelopeValuesMessages.Enqueue(FAudioBusEnvelopeValuesMessage{ Context });
#endif // WITH_EDITOR
						break;
					}

					case RouteId_Stop:
					{
#if WITH_EDITOR
						CacheMessage<FAudioBusStopMessage>(Context, Messages.StopMessages);
#else
						Messages.StopMessages.Enqueue(FAudioBusStopMessage{ Context });
#endif // WITH_EDITOR
						break;
					}

					default:
					{
						return OnEventFailure(RouteId, Style, Context);
					}
				}

				const double Timestamp = Context.EventTime.AsSeconds(Context.EventData.GetValue<uint64>("Timestamp"));

				{
					TraceServices::FAnalysisSessionEditScope SessionEditScope(Session);
					Session.UpdateDurationSeconds(Timestamp);
				}

				return OnEventSuccess(RouteId, Style, Context);
			}

		private:
			enum : uint16
			{
				RouteId_Start,
				RouteId_HasActivity,
				RouteId_EnvelopeFollowerEnabled,
				RouteId_EnvelopeValues,
				RouteId_Stop
			};

			TraceServices::IAnalysisSession& Session;
		};

		return new FAudioBusTraceAnalyzer(AsShared(), InSession);
	}

	void FAudioBusTraceProvider::OnTraceChannelsEnabled()
	{
		FTraceProviderBase::OnTraceChannelsEnabled();

#if WITH_EDITOR
		AudioBusAssetProvider.RequestEntriesUpdate();
		OnAudioBusListUpdated.ExecuteIfBound();
#endif // WITH_EDITOR
	}

	bool FAudioBusTraceProvider::ProcessMessages()
	{
		// Helper lambdas
		auto CreateEntry = [this](const FAudioBusMessageBase& Msg)
		{
			TSharedPtr<FAudioBusDashboardEntry>* ToReturn = nullptr;
			
			UpdateDeviceEntry(Msg.DeviceId, Msg.AudioBusId, [&ToReturn, &Msg](TSharedPtr<FAudioBusDashboardEntry>& Entry)
			{
				if (!Entry.IsValid())
				{
					Entry = MakeShared<FAudioBusDashboardEntry>();
					Entry->AudioBusId = Msg.AudioBusId;
				}

				Entry->Timestamp = Msg.Timestamp;

				ToReturn = &Entry;
			});

			return ToReturn;
		};

		auto GetEntry = [this](const FAudioBusMessageBase& Msg)
		{
			return FindDeviceEntry(Msg.DeviceId, Msg.AudioBusId);
		};

		// Process messages
		ProcessMessageQueue<FAudioBusStartMessage>(TraceMessages.StartMessages, CreateEntry,
		[this](const FAudioBusStartMessage& Msg, TSharedPtr<FAudioBusDashboardEntry>* OutEntry)
		{
#if !WITH_EDITOR
			if (SessionCachedMessages.IsValid())
			{
				SessionCachedMessages->StartCachedMessages.EmplaceBack(Msg);
			}
#endif // !WITH_EDITOR

			if (OutEntry)
			{
				FAudioBusDashboardEntry& EntryRef = *OutEntry->Get();

				if (EntryRef.Name.IsEmpty())
				{
					EntryRef.Name = Msg.Name;
				}

				if (EntryRef.AudioMeterInfo->NumChannels == 0)
				{
					EntryRef.AudioMeterInfo->NumChannels = Msg.NumChannels;
				}

				if (EntryRef.AudioBusType == EAudioBusType::None)
				{
					EntryRef.AudioBusType = Msg.AudioBusType;
				}

				OnAudioBusStarted.ExecuteIfBound(Msg.AudioBusId);
			}
		});

		ProcessMessageQueue<FAudioBusHasActivityMessage>(TraceMessages.HasActivityMessages, GetEntry,
		[this](const FAudioBusHasActivityMessage& Msg, TSharedPtr<FAudioBusDashboardEntry>* OutEntry)
		{
#if !WITH_EDITOR
			if (SessionCachedMessages.IsValid())
			{
				SessionCachedMessages->HasActivityCachedMessages.EmplaceBack(Msg);
			}
#endif // !WITH_EDITOR

			if (OutEntry)
			{
				FAudioBusDashboardEntry& EntryRef = *OutEntry->Get();
				EntryRef.bHasActivity = Msg.bHasActivity;
				EntryRef.Timestamp = Msg.Timestamp;
			}
		});

		ProcessMessageQueue<FAudioBusEnvelopeFollowerEnabledMessage>(TraceMessages.EnvelopeFollowerEnabledMessages, GetEntry,
		[this](const FAudioBusEnvelopeFollowerEnabledMessage& Msg, TSharedPtr<FAudioBusDashboardEntry>* OutEntry)
		{
#if !WITH_EDITOR
			if (SessionCachedMessages.IsValid())
			{
				SessionCachedMessages->EnvelopeFollowerEnabledCachedMessages.EmplaceBack(Msg);
			}
#endif // !WITH_EDITOR

			if (OutEntry)
			{
				FAudioBusDashboardEntry& EntryRef = *OutEntry->Get();
				EntryRef.bEnvelopeFollowerEnabled = Msg.bEnvelopeFollowerEnabled;
				EntryRef.Timestamp = Msg.Timestamp;
			}
		});

		ProcessMessageQueue<FAudioBusEnvelopeValuesMessage>(TraceMessages.EnvelopeValuesMessages, GetEntry,
		[this](const FAudioBusEnvelopeValuesMessage& Msg, TSharedPtr<FAudioBusDashboardEntry>* OutEntry)
		{
#if !WITH_EDITOR
			if (SessionCachedMessages.IsValid())
			{
				SessionCachedMessages->EnvelopeValuesCachedMessages.EmplaceBack(Msg);
			}
#endif // !WITH_EDITOR

			if (OutEntry)
			{
				FAudioBusDashboardEntry& EntryRef = *OutEntry->Get();

				EntryRef.AudioMeterInfo->EnvelopeValues = Msg.EnvelopeValues;
				EntryRef.Timestamp = Msg.Timestamp;
			}
		});

		ProcessMessageQueue<FAudioBusStopMessage>(TraceMessages.StopMessages, GetEntry,
		[this](const FAudioBusStopMessage& Msg, TSharedPtr<FAudioBusDashboardEntry>* OutEntry)
		{
#if !WITH_EDITOR
			if (SessionCachedMessages.IsValid())
			{
				SessionCachedMessages->StopCachedMessages.EmplaceBack(Msg);
			}
#endif // !WITH_EDITOR

			if (OutEntry && (*OutEntry)->Timestamp < Msg.Timestamp)
			{
#if WITH_EDITOR
				// Turn off audio bus entry activity, do not remove it since we are in Editor mode.
				// We keep in the dashboard all registered audio bus assets from Content Browser
				FAudioBusDashboardEntry& EntryRef = *OutEntry->Get();

				EntryRef.bHasActivity = false;

				TArray<float>& EnvelopeValues = EntryRef.AudioMeterInfo->EnvelopeValues;
				FMemory::Memzero(EnvelopeValues.GetData(), EnvelopeValues.Num() * sizeof(float));

				if (EntryRef.AudioBusType == EAudioBusType::CodeGenerated)
#endif  // WITH_EDITOR
				{
					OnAudioBusRemoved.ExecuteIfBound(Msg.AudioBusId);
					RemoveDeviceEntry(Msg.DeviceId, Msg.AudioBusId);
				}
			}
		});

#if WITH_EDITOR
		if (bAssetsUpdated)
		{
			OnAudioBusListUpdated.ExecuteIfBound();
			bAssetsUpdated = false;
		}
#endif // WITH_EDITOR

		return true;
	}

#if WITH_EDITOR
	void FAudioBusTraceProvider::RequestEntriesUpdate()
	{
		AudioBusAssetProvider.RequestEntriesUpdate();
	}

	void FAudioBusTraceProvider::HandleOnAudioBusAssetAdded(const FString& InAssetPath)
	{
		using namespace FAudioBusTraceProviderPrivate;

		if (const FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
		{
			const TArray<FAudioDevice*> AudioDevices = DeviceManager->GetAudioDevices();

			for (const FAudioDevice* AudioDevice : AudioDevices)
			{
				if (AudioDevice == nullptr)
				{
					continue;
				}

				const TObjectPtr<UAudioBus> AudioBus = Cast<UAudioBus>(FSoftObjectPath(InAssetPath).ResolveObject());
				const uint32 AudioBusId = AudioBus ? AudioBus->GetUniqueID() : INDEX_NONE;

				const TSharedPtr<FAudioBusDashboardEntry>* FoundAudioBusDashboardEntry = FindDeviceEntry(AudioDevice->DeviceID, AudioBusId);

				if (FoundAudioBusDashboardEntry == nullptr)
				{
					UpdateDeviceEntry(AudioDevice->DeviceID, AudioBusId, [&InAssetPath](TSharedPtr<FAudioBusDashboardEntry>& Entry)
					{
						if (!Entry.IsValid())
						{
							Entry = CreateEntryFromAsset(InAssetPath);
						}
					});
				}

				++LastUpdateId;
				OnAudioBusAdded.ExecuteIfBound(AudioBusId);
				bAssetsUpdated = true;
			}
		}
	}

	void FAudioBusTraceProvider::HandleOnAudioBusAssetRemoved(const FString& InAssetPath)
	{
		if (const FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
		{
			const TArray<FAudioDevice*> AudioDevices = DeviceManager->GetAudioDevices();

			for (const FAudioDevice* AudioDevice : AudioDevices)
			{
				if (AudioDevice)
				{
					const TObjectPtr<UAudioBus> AudioBus = Cast<UAudioBus>(FSoftObjectPath(InAssetPath).ResolveObject());
					const uint32 AudioBusId = AudioBus ? AudioBus->GetUniqueID() : INDEX_NONE;

					RemoveDeviceEntry(AudioDevice->DeviceID, AudioBusId);

					++LastUpdateId;
					OnAudioBusRemoved.ExecuteIfBound(AudioBusId);
					bAssetsUpdated = true;
				}
			}
		}
	}

	void FAudioBusTraceProvider::HandleOnAudioBusAssetListUpdated(const TArray<FString>& InAssetPaths)
	{
		using namespace FAudioBusTraceProviderPrivate;

		const FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get();

		if (DeviceManager == nullptr)
		{
			return;
		}

		for (const FAudioDevice* AudioDevice : DeviceManager->GetAudioDevices())
		{
			if (AudioDevice == nullptr)
			{
				continue;
			}

			for (const FString& AssetPath : InAssetPaths)
			{
				const TObjectPtr<UAudioBus> AudioBus = Cast<UAudioBus>(FSoftObjectPath(AssetPath).ResolveObject());
				const uint32 AudioBusId = AudioBus ? AudioBus->GetUniqueID() : INDEX_NONE;

				const TSharedPtr<FAudioBusDashboardEntry>* FoundAudioBusDashboardEntry = FindDeviceEntry(AudioDevice->DeviceID, AudioBusId);

				if (FoundAudioBusDashboardEntry == nullptr)
				{
					UpdateDeviceEntry(AudioDevice->DeviceID, AudioBusId, [&AssetPath](TSharedPtr<FAudioBusDashboardEntry>& Entry)
					{
						if (!Entry.IsValid())
						{
							Entry = CreateEntryFromAsset(AssetPath);
						}
					});
				}
			}
		}

		++LastUpdateId;
		OnAudioBusListUpdated.ExecuteIfBound();
		bAssetsUpdated = true;
	}
#else
	void FAudioBusTraceProvider::InitSessionCachedMessages(TraceServices::IAnalysisSession& InSession)
	{
		SessionCachedMessages = MakeUnique<FAudioBusSessionCachedMessages>(InSession);
	}
#endif // WITH_EDITOR

	void FAudioBusTraceProvider::OnTimingViewTimeMarkerChanged(double TimeMarker)
	{
		using namespace FAudioBusTraceProviderPrivate;

#if !WITH_EDITOR
		if (!SessionCachedMessages.IsValid())
		{
			return;
		}
#endif // !WITH_EDITOR

		DeviceDataMap.Empty();

		// Collect all the audio bus start messages registered until this point in time 
		auto ProcessBusStartMessage = [this](const FAudioBusStartMessage& Msg)
		{
			UpdateDeviceEntry(Msg.DeviceId, Msg.AudioBusId, [&Msg](TSharedPtr<FAudioBusDashboardEntry>& Entry)
			{
				if (!Entry.IsValid())
				{
					Entry = MakeShared<FAudioBusDashboardEntry>();
					Entry->AudioBusId = Msg.AudioBusId;
				}

				Entry->Timestamp = Msg.Timestamp;

				Entry->Name = *Msg.Name;
				Entry->AudioMeterInfo->NumChannels = Msg.NumChannels;
				Entry->AudioBusType = Msg.AudioBusType;
			});
		};

#if WITH_EDITOR
		const FAudioInsightsCacheManager& CacheManager = FAudioInsightsModule::GetChecked().GetCacheManager();
		CacheManager.IterateTo<FAudioBusStartMessage>(AudioBusMessageNames::Start, TimeMarker, [&ProcessBusStartMessage](const FAudioBusStartMessage& AudioBusStartCachedMessage)
		{
			ProcessBusStartMessage(AudioBusStartCachedMessage);
		});
#else
		for (const FAudioBusStartMessage& AudioBusStartCachedMessage : SessionCachedMessages->StartCachedMessages)
		{
			if (AudioBusStartCachedMessage.Timestamp > TimeMarker)
			{
				break;
			}

			ProcessBusStartMessage(AudioBusStartCachedMessage);
		}
#endif // WITH_EDITOR

		// Selectively remove start messages collected in the step above by knowing which audio buses were stopped.
		// With this we will know what are the available audio buses at this point in time.
		auto ProcessBusStopMessage = [this](const FAudioBusStopMessage& Msg)
		{
			auto* OutEntry = FindDeviceEntry(Msg.DeviceId, Msg.AudioBusId);

			if (OutEntry && (*OutEntry)->Timestamp < Msg.Timestamp)
			{
				RemoveDeviceEntry(Msg.DeviceId, Msg.AudioBusId);
			}
		};

#if WITH_EDITOR
		CacheManager.IterateTo<FAudioBusStopMessage>(AudioBusMessageNames::Stop, TimeMarker, [&ProcessBusStopMessage](const FAudioBusStopMessage& AudioBusStopCachedMessage)
		{
			ProcessBusStopMessage(AudioBusStopCachedMessage);
		});
#else
		for (const FAudioBusStopMessage& AudioBusStopCachedMessage : SessionCachedMessages->StopCachedMessages)
		{
			if (AudioBusStopCachedMessage.Timestamp > TimeMarker)
			{
				break;
			}

			ProcessBusStopMessage(AudioBusStopCachedMessage);
		}
#endif // WITH_EDITOR

		const FDeviceData* DeviceData = FindFilteredDeviceData();
		if (DeviceData)
		{
			// Collect has activity messages from audio buses (based on active audio buses AudioBusId)
			struct CachedEntryInfo
			{
				TOptional<FAudioBusHasActivityMessage> HasActivityMessage;
				TOptional<FAudioBusEnvelopeFollowerEnabledMessage> EnvelopeFollowerEnabledMessage;
				TOptional<FAudioBusEnvelopeValuesMessage> EnvelopeValuesMessage;
			};

			TArray<uint32> AudioBusIdArray;
			(*DeviceData).GenerateKeyArray(AudioBusIdArray);

			TArray<CachedEntryInfo> CachedEntryInfos;
			CachedEntryInfos.SetNum(AudioBusIdArray.Num());

			ParallelFor(AudioBusIdArray.Num(),
#if WITH_EDITOR
			[&AudioBusIdArray, &CachedEntryInfos, &CacheManager, TimeMarker, this](const int32 Index)
#else
			[&AudioBusIdArray, &CachedEntryInfos, TimeMarker, this](const int32 Index)
#endif // WITH_EDITOR
			{
				const uint32 AudioBusId = AudioBusIdArray[Index];

				// HasActivity
#if WITH_EDITOR
				const FAudioBusHasActivityMessage* FoundHasActivityCachedMessage = CacheManager.FindClosestMessage<FAudioBusHasActivityMessage>(AudioBusMessageNames::HasActivity, TimeMarker, AudioBusId);
#else
				const FAudioBusHasActivityMessage* FoundHasActivityCachedMessage = FindClosestMessageToTimestamp(SessionCachedMessages->HasActivityCachedMessages, TimeMarker, AudioBusId);
#endif // WITH_EDITOR
				if (FoundHasActivityCachedMessage)
				{
					CachedEntryInfos[Index].HasActivityMessage = *FoundHasActivityCachedMessage;
				}

				// EnvelopeFollowerEnabled
#if WITH_EDITOR
				const FAudioBusEnvelopeFollowerEnabledMessage* FoundEnvelopeFollowerEnabledCachedMessage = CacheManager.FindClosestMessage<FAudioBusEnvelopeFollowerEnabledMessage>(AudioBusMessageNames::EnvelopeFollowerEnabled, TimeMarker, AudioBusId);
#else
				const FAudioBusEnvelopeFollowerEnabledMessage* FoundEnvelopeFollowerEnabledCachedMessage = FindClosestMessageToTimestamp(SessionCachedMessages->EnvelopeFollowerEnabledCachedMessages, TimeMarker, AudioBusId);
#endif // WITH_EDITOR
				if (FoundEnvelopeFollowerEnabledCachedMessage)
				{
					CachedEntryInfos[Index].EnvelopeFollowerEnabledMessage = *FoundEnvelopeFollowerEnabledCachedMessage;
				}

				// Envelope Values
#if WITH_EDITOR
				const FAudioBusEnvelopeValuesMessage* FoundEnvelopeValuesCachedMessage = CacheManager.FindClosestMessage<FAudioBusEnvelopeValuesMessage>(AudioBusMessageNames::EnvelopeValues, TimeMarker, AudioBusId);
#else
				const FAudioBusEnvelopeValuesMessage* FoundEnvelopeValuesCachedMessage = FindClosestMessageToTimestamp(SessionCachedMessages->EnvelopeValuesCachedMessages, TimeMarker, AudioBusId);
#endif // WITH_EDITOR
				if (FoundEnvelopeValuesCachedMessage)
				{
					CachedEntryInfos[Index].EnvelopeValuesMessage = *FoundEnvelopeValuesCachedMessage;
				}
			});

			// Update the device entries with the collected info
			for (const CachedEntryInfo& CachedEntryInfo : CachedEntryInfos)
			{
				if (CachedEntryInfo.HasActivityMessage.IsSet())
				{
					const FAudioBusHasActivityMessage& HasActivityMessage = CachedEntryInfo.HasActivityMessage.GetValue();

					UpdateDeviceEntry(HasActivityMessage.DeviceId, HasActivityMessage.AudioBusId, [&HasActivityMessage](TSharedPtr<FAudioBusDashboardEntry>& Entry)
					{
						if (!Entry.IsValid())
						{
							Entry = MakeShared<FAudioBusDashboardEntry>();
							Entry->AudioBusId = HasActivityMessage.AudioBusId;
						}

						Entry->Timestamp = HasActivityMessage.Timestamp;

						Entry->bHasActivity = HasActivityMessage.bHasActivity;
					});
				}

				if (CachedEntryInfo.EnvelopeFollowerEnabledMessage.IsSet())
				{
					const FAudioBusEnvelopeFollowerEnabledMessage& EnvelopeFollowerEnabledMessage = CachedEntryInfo.EnvelopeFollowerEnabledMessage.GetValue();

					UpdateDeviceEntry(EnvelopeFollowerEnabledMessage.DeviceId, EnvelopeFollowerEnabledMessage.AudioBusId, [&EnvelopeFollowerEnabledMessage](TSharedPtr<FAudioBusDashboardEntry>& Entry)
					{
						if (!Entry.IsValid())
						{
							Entry = MakeShared<FAudioBusDashboardEntry>();
							Entry->AudioBusId = EnvelopeFollowerEnabledMessage.AudioBusId;
						}

						Entry->Timestamp = EnvelopeFollowerEnabledMessage.Timestamp;

						Entry->bEnvelopeFollowerEnabled = EnvelopeFollowerEnabledMessage.bEnvelopeFollowerEnabled;
					});
				}

				if (CachedEntryInfo.EnvelopeValuesMessage.IsSet())
				{
					const FAudioBusEnvelopeValuesMessage& EnvelopeValuesMessage = CachedEntryInfo.EnvelopeValuesMessage.GetValue();

					UpdateDeviceEntry(EnvelopeValuesMessage.DeviceId, EnvelopeValuesMessage.AudioBusId, [&EnvelopeValuesMessage](TSharedPtr<FAudioBusDashboardEntry>& Entry)
					{
						if (!Entry.IsValid())
						{
							Entry = MakeShared<FAudioBusDashboardEntry>();
							Entry->AudioBusId = EnvelopeValuesMessage.AudioBusId;
						}

						Entry->Timestamp = EnvelopeValuesMessage.Timestamp;

						Entry->AudioMeterInfo->NumChannels = EnvelopeValuesMessage.EnvelopeValues.Num();
						Entry->AudioMeterInfo->EnvelopeValues = EnvelopeValuesMessage.EnvelopeValues;
					});
				}
			}
		}

		// Call parent method to update LastMessageId
		FTraceProviderBase::OnTimingViewTimeMarkerChanged(TimeMarker);

		OnTimeMarkerUpdated.ExecuteIfBound();
	}

#if WITH_EDITOR
	void FAudioBusTraceProvider::OnTimeControlMethodReset()
	{
		AudioBusAssetProvider.RequestEntriesUpdate();
		OnAudioBusListUpdated.ExecuteIfBound();
	}
#endif // WITH_EDITOR
} // namespace UE::Audio::Insights
