// Copyright Epic Games, Inc. All Rights Reserved.
#include "Providers/VirtualLoopTraceProvider.h"

#include "Async/ParallelFor.h"
#include "AudioInsightsModule.h"
#include "Trace/Analyzer.h"
#include "TraceServices/Model/AnalysisSession.h"

#if WITH_EDITOR
#include "Cache/AudioInsightsCacheManager.h"
#else
#include "Common/PagedArray.h"
#endif // WITH_EDITOR

namespace UE::Audio::Insights
{
	namespace FVirtualLoopTraceProviderPrivate
	{
#if !WITH_EDITOR
		template<typename T>
		const T* FindClosestMessageToTimestamp(const TraceServices::TPagedArray<T>& InCachedMessages, const double InTimeMarker, const uint32 InPlayOrder)
		{
			const int32 ClosestMessageToTimeStampIndex = TraceServices::PagedArrayAlgo::BinarySearchClosestBy(InCachedMessages, InTimeMarker, [](const T& Msg) { return Msg.Timestamp; });

			// Iterate backwards from TimeMarker until we find the matching PlayOrder
			for (auto It = InCachedMessages.GetIteratorFromItem(ClosestMessageToTimeStampIndex); It; --It)
			{
				if (It->PlayOrder == InPlayOrder)
				{
					return &(*It);
				}
			}

			return nullptr;
		}
#endif // !WITH_EDITOR
	}

	FName FVirtualLoopTraceProvider::GetName_Static()
	{
		return "AudioVirtualLoopProvider";
	}

	void FVirtualLoopTraceProvider::OnTimingViewTimeMarkerChanged(double TimeMarker)
	{
#if !WITH_EDITOR
		if (!SessionCachedMessages.IsValid())
		{
			return;
		}
#endif // !WITH_EDITOR

		// Helper lambdas
		auto ProcessVirtualizeMessage = [this](const FVirtualLoopVirtualizeMessage& VirtualizeCachedMessage)
		{
			UpdateDeviceEntry(VirtualizeCachedMessage.DeviceId, VirtualizeCachedMessage.PlayOrder, [&VirtualizeCachedMessage](TSharedPtr<FVirtualLoopDashboardEntry>& Entry)
			{
				if (!Entry.IsValid())
				{
					Entry = MakeShared<FVirtualLoopDashboardEntry>();
					Entry->DeviceId  = VirtualizeCachedMessage.DeviceId;
					Entry->PlayOrder = VirtualizeCachedMessage.PlayOrder;
				}
				
				Entry->Timestamp = VirtualizeCachedMessage.Timestamp;

				Entry->Name        = *VirtualizeCachedMessage.Name;
				Entry->ComponentId = VirtualizeCachedMessage.ComponentId;
			});
		};

		auto ProcessStopOrRealizeMessage = [this](const FVirtualLoopStopOrRealizeMessage& StopOrRealizeCachedMessage)
		{
			auto* OutEntry = FindDeviceEntry(StopOrRealizeCachedMessage.DeviceId, StopOrRealizeCachedMessage.PlayOrder);
			
			if (OutEntry && (*OutEntry)->Timestamp < StopOrRealizeCachedMessage.Timestamp)
			{
				RemoveDeviceEntry(StopOrRealizeCachedMessage.DeviceId, StopOrRealizeCachedMessage.PlayOrder);
			}
		};

		// Process cached messages
		DeviceDataMap.Empty();

#if WITH_EDITOR
		const FAudioInsightsCacheManager& CacheManager = FAudioInsightsModule::GetChecked().GetCacheManager();

		// Virtualize cached messages
		CacheManager.IterateTo<FVirtualLoopVirtualizeMessage>(VirtualLoopMessageNames::Virtualize, TimeMarker, [&ProcessVirtualizeMessage](const FVirtualLoopVirtualizeMessage& VirtualizeCachedMessage)
		{
			ProcessVirtualizeMessage(VirtualizeCachedMessage);
		});

		// Stop or realize cached messages
		CacheManager.IterateTo<FVirtualLoopStopOrRealizeMessage>(VirtualLoopMessageNames::StopOrRealize, TimeMarker, [&ProcessStopOrRealizeMessage](const FVirtualLoopStopOrRealizeMessage& StopOrRealizeCachedMessage)
		{
			ProcessStopOrRealizeMessage(StopOrRealizeCachedMessage);
		});
#else
		// Virtualize cached messages
		for (const FVirtualLoopVirtualizeMessage& VirtualizeCachedMessage : SessionCachedMessages->VirtualizeCachedMessages)
		{
			if (VirtualizeCachedMessage.Timestamp > TimeMarker)
			{
				break;
			}

			ProcessVirtualizeMessage(VirtualizeCachedMessage);
		}

		// Stop or realize cached messages
		for (const FVirtualLoopStopOrRealizeMessage& StopOrRealizeCachedMessage : SessionCachedMessages->StopOrRealizeCachedMessages)
		{
			if (StopOrRealizeCachedMessage.Timestamp > TimeMarker)
			{
				break;
			}

			ProcessStopOrRealizeMessage(StopOrRealizeCachedMessage);
		}
#endif // WITH_EDITOR

		CollectParamsForTimestamp(TimeMarker);

		// Call parent method to update LastMessageId
		FTraceProviderBase::OnTimingViewTimeMarkerChanged(TimeMarker);
	}

	void FVirtualLoopTraceProvider::CollectParamsForTimestamp(const double InTimeMarker)
	{
		using namespace FVirtualLoopTraceProviderPrivate;

		const FDeviceData* DeviceData = FindFilteredDeviceData();
		if (DeviceData == nullptr)
		{
			return;
		}

		// Collect update messages from virtualized sounds (based on active sounds's PlayOrder)
		struct CachedEntryInfo
		{
			TOptional<FVirtualLoopUpdateMessage> UpdateMessage;
		};

		TArray<uint32> PlayOrderArray;
		DeviceData->GenerateKeyArray(PlayOrderArray);

		TArray<CachedEntryInfo> CachedEntryInfos;
		CachedEntryInfos.SetNum(PlayOrderArray.Num());

#if WITH_EDITOR
		const FAudioInsightsCacheManager& CacheManager = FAudioInsightsModule::GetChecked().GetCacheManager();

		ParallelFor(PlayOrderArray.Num(), [&PlayOrderArray, &CachedEntryInfos, &CacheManager, InTimeMarker, this](const int32 Index)
#else
		ParallelFor(PlayOrderArray.Num(), [&PlayOrderArray, &CachedEntryInfos, InTimeMarker, this](const int32 Index)
#endif // WITH_EDITOR
		{
			const uint32 PlayOrder = PlayOrderArray[Index];

#if WITH_EDITOR
			const FVirtualLoopUpdateMessage* FoundUpdateCachedMessage = CacheManager.FindClosestMessage<FVirtualLoopUpdateMessage>(VirtualLoopMessageNames::Update, InTimeMarker, PlayOrder);
#else
			const FVirtualLoopUpdateMessage* FoundUpdateCachedMessage = FindClosestMessageToTimestamp(SessionCachedMessages->UpdateCachedMessages, InTimeMarker, PlayOrder);
#endif // WITH_EDITOR

			if (FoundUpdateCachedMessage)
			{
				CachedEntryInfos[Index].UpdateMessage = *FoundUpdateCachedMessage;
			}
		});

		// Update the device entries with the collected info
		for (const CachedEntryInfo& CachedEntryInfo : CachedEntryInfos)
		{
			if (CachedEntryInfo.UpdateMessage.IsSet())
			{
				const FVirtualLoopUpdateMessage& VirtualLoopUpdateMessage = CachedEntryInfo.UpdateMessage.GetValue();

				UpdateDeviceEntry(VirtualLoopUpdateMessage.DeviceId, VirtualLoopUpdateMessage.PlayOrder, [&VirtualLoopUpdateMessage](TSharedPtr<FVirtualLoopDashboardEntry>& Entry)
				{
					if (ensure(Entry.IsValid()))
					{
						Entry->Timestamp = VirtualLoopUpdateMessage.Timestamp;

						Entry->PlaybackTime    = VirtualLoopUpdateMessage.PlaybackTime;
						Entry->TimeVirtualized = VirtualLoopUpdateMessage.TimeVirtualized;
						Entry->UpdateInterval  = VirtualLoopUpdateMessage.UpdateInterval;
						Entry->Location        = FVector{ VirtualLoopUpdateMessage.LocationX, VirtualLoopUpdateMessage.LocationY, VirtualLoopUpdateMessage.LocationZ };
						Entry->Rotator         = FRotator{ VirtualLoopUpdateMessage.RotatorPitch, VirtualLoopUpdateMessage.RotatorYaw, VirtualLoopUpdateMessage.RotatorRoll };
					}
				});
			}
		}
	}

	bool FVirtualLoopTraceProvider::ProcessMessages()
	{
		auto RemoveEntryFunc = [this](const FVirtualLoopStopOrRealizeMessage& Msg, TSharedPtr<FVirtualLoopDashboardEntry>* OutEntry)
		{
#if !WITH_EDITOR
			if (SessionCachedMessages.IsValid())
			{
				SessionCachedMessages->StopOrRealizeCachedMessages.EmplaceBack(Msg);
			}
#endif // !WITH_EDITOR

			if (OutEntry && (*OutEntry)->Timestamp < Msg.Timestamp)
			{
				RemoveDeviceEntry(Msg.DeviceId, Msg.PlayOrder);
			}
		};

		auto GetEntryFunc = [this](const FVirtualLoopMessageBase& Msg)
		{
			return FindDeviceEntry(Msg.DeviceId, Msg.PlayOrder);
		};

		auto BumpEntryFunc = [this](const FVirtualLoopMessageBase& Msg)
		{
			TSharedPtr<FVirtualLoopDashboardEntry>* ToReturn = nullptr;
			UpdateDeviceEntry(Msg.DeviceId, Msg.PlayOrder, [&ToReturn, &Msg](TSharedPtr<FVirtualLoopDashboardEntry>& Entry)
			{
				if (!Entry.IsValid())
				{
					Entry = MakeShared<FVirtualLoopDashboardEntry>();
					Entry->DeviceId  = Msg.DeviceId;
					Entry->PlayOrder = Msg.PlayOrder;
				}
				Entry->Timestamp = Msg.Timestamp;

				ToReturn = &Entry;
			});

			return ToReturn;
		};

		ProcessMessageQueue<FVirtualLoopVirtualizeMessage>(TraceMessages.VirtualizeMessages,
		BumpEntryFunc,
		[this](const FVirtualLoopVirtualizeMessage& Msg, TSharedPtr<FVirtualLoopDashboardEntry>* OutEntry)
		{
#if !WITH_EDITOR
			if (SessionCachedMessages.IsValid())
			{
				SessionCachedMessages->VirtualizeCachedMessages.EmplaceBack(Msg);
			}
#endif // !WITH_EDITOR

			FVirtualLoopDashboardEntry& EntryRef = *OutEntry->Get();
			EntryRef.Name = Msg.Name;
			EntryRef.ComponentId = Msg.ComponentId;
		});

		ProcessMessageQueue<FVirtualLoopUpdateMessage>(TraceMessages.UpdateMessages,
		GetEntryFunc,
		[this](const FVirtualLoopUpdateMessage& Msg, TSharedPtr<FVirtualLoopDashboardEntry>* OutEntry)
		{
#if !WITH_EDITOR
			if (SessionCachedMessages.IsValid())
			{
				SessionCachedMessages->UpdateCachedMessages.EmplaceBack(Msg);
			}
#endif // !WITH_EDITOR

			if (OutEntry)
			{
				FVirtualLoopDashboardEntry& EntryRef = *OutEntry->Get();
				EntryRef.PlaybackTime = Msg.PlaybackTime;
				EntryRef.TimeVirtualized = Msg.TimeVirtualized;
				EntryRef.UpdateInterval = Msg.UpdateInterval;
				EntryRef.Location = FVector{ Msg.LocationX, Msg.LocationY, Msg.LocationZ };
				EntryRef.Rotator = FRotator{ Msg.RotatorPitch, Msg.RotatorYaw, Msg.RotatorRoll };
			}
		});

		ProcessMessageQueue<FVirtualLoopStopOrRealizeMessage>(TraceMessages.StopOrRealizeMessages, GetEntryFunc, RemoveEntryFunc);

		return true;
	}

#if !WITH_EDITOR
	void FVirtualLoopTraceProvider::InitSessionCachedMessages(TraceServices::IAnalysisSession& InSession)
	{
		SessionCachedMessages = MakeUnique<FVirtualLoopSessionCachedMessages>(InSession);
	}
#endif // !WITH_EDITOR

	UE::Trace::IAnalyzer* FVirtualLoopTraceProvider::ConstructAnalyzer(TraceServices::IAnalysisSession& InSession)
	{
		class FVirtualLoopTraceAnalyzer : public FTraceAnalyzerBase
		{
		public:
			FVirtualLoopTraceAnalyzer(TSharedRef<FVirtualLoopTraceProvider> InProvider, TraceServices::IAnalysisSession& InSession)
				: FTraceAnalyzerBase(InProvider)
				, Session(InSession)
			{
			}

			virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override
			{
				FTraceAnalyzerBase::OnAnalysisBegin(Context);

				UE::Trace::IAnalyzer::FInterfaceBuilder& Builder = Context.InterfaceBuilder;
				Builder.RouteEvent(RouteId_Stop, "Audio", "VirtualLoopStopOrRealize");
				Builder.RouteEvent(RouteId_Update, "Audio", "VirtualLoopUpdate");
				Builder.RouteEvent(RouteId_Virtualize, "Audio", "VirtualLoopVirtualize");
				Builder.RouteEvent(RouteId_IsStillVirtualizedPing, "Audio", "VirtualLoopIsVirtualizedPing");
			}

			virtual bool OnHandleEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override
			{
				LLM_SCOPE_BYNAME(TEXT("Insights/FVirtualLoopTraceAnalyzer"));

				FVirtualLoopMessages& Messages = GetProvider<FVirtualLoopTraceProvider>().TraceMessages;
				switch (RouteId)
				{
					case RouteId_Virtualize:
					case RouteId_IsStillVirtualizedPing:
					{
#if WITH_EDITOR
						CacheMessage<FVirtualLoopVirtualizeMessage>(Context, Messages.VirtualizeMessages);
#else
						Messages.VirtualizeMessages.Enqueue(FVirtualLoopVirtualizeMessage{ Context });
#endif // WITH_EDITOR
						break;
					}

					case RouteId_Stop:
					{
#if WITH_EDITOR
						CacheMessage<FVirtualLoopStopOrRealizeMessage>(Context, Messages.StopOrRealizeMessages);
#else
						Messages.StopOrRealizeMessages.Enqueue(FVirtualLoopStopOrRealizeMessage{ Context });
#endif // WITH_EDITOR
						break;
					}

					case RouteId_Update:
					{
#if WITH_EDITOR
						CacheMessage<FVirtualLoopUpdateMessage>(Context, Messages.UpdateMessages);
#else
						Messages.UpdateMessages.Enqueue(FVirtualLoopUpdateMessage{ Context });
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
				RouteId_Virtualize,
				RouteId_Update,
				RouteId_Stop,
				RouteId_IsStillVirtualizedPing
			};

			TraceServices::IAnalysisSession& Session;
		};

		return new FVirtualLoopTraceAnalyzer(AsShared(), InSession);
	}
} // namespace UE::Audio::Insights
