// Copyright Epic Games, Inc. All Rights Reserved.
#include "Cache/AudioInsightsCacheManager.h"

#include "AudioInsightsModule.h"

namespace UE::Audio::Insights
{
	namespace CacheManagerPrivate
	{
		constexpr uint32 MaxChunkSize = 1 << 20; // 1mB
		constexpr uint32 NumChunks = 32u; // 32 mB total
	}

	FAudioInsightsCacheManager::FAudioInsightsCacheManager()
		: Allocator(8 << 20) // 8mB for storage of pointers to messages
		, Cache(CacheManagerPrivate::NumChunks)
	{
		check(CacheManagerPrivate::NumChunks > 0u);

		CreateCache();

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("FAudioInsightsCacheManager::ProcessPendingMessages"), 0.0f, [this](float DeltaTime)
		{
			ProcessPendingMessages(DeltaTime);
			return true;
		});
	}

	FAudioInsightsCacheManager::~FAudioInsightsCacheManager()
	{
		FTSTicker::RemoveTicker(TickerHandle);
	}

	void FAudioInsightsCacheManager::AddMessageToCache(TSharedPtr<IAudioCachedMessage> Message)
	{
		NewMessageQueue.Enqueue(MoveTemp(Message));
	}

	void FAudioInsightsCacheManager::ClearCache()
	{
		NewMessageQueue.DequeueAll();

		CreateCache();
	}

	float FAudioInsightsCacheManager::GetCacheDuration() const
	{
		return FMath::Max(static_cast<float>(GetCacheEndTimeStamp() - GetCacheStartTimeStamp()), 0.0f);
	}

	double FAudioInsightsCacheManager::GetCacheStartTimeStamp() const
	{
		check(StartIndex < Cache.Capacity());
		check(Cache[StartIndex].IsValid());

		return Cache[StartIndex]->GetChunkTimeRangeStart();
	}

	double FAudioInsightsCacheManager::GetCacheEndTimeStamp() const
	{
		check(WriteIndex < Cache.Capacity());
		check(Cache[WriteIndex].IsValid());

		return Cache[WriteIndex]->GetChunkTimeRangeEnd();
	}

	uint32 FAudioInsightsCacheManager::GetUsedCacheSize() const
	{
		uint32 UsedSpace = 0u;
		for (uint32 Index = 0u; Index < Cache.Capacity(); ++Index)
		{
			UsedSpace += Cache[Index]->GetCurrentChunkSize();
		}

		return UsedSpace;
	}

	uint32 FAudioInsightsCacheManager::GetMaxCacheSize() const
	{
		return CacheManagerPrivate::MaxChunkSize * CacheManagerPrivate::NumChunks;
	}

	uint32 FAudioInsightsCacheManager::GetNumChunks() const
	{
		return CacheManagerPrivate::NumChunks;
	}

	uint32 FAudioInsightsCacheManager::GetNumUsedChunks() const
	{
		uint32 NumUsedChunks = 0u;
		for (uint32 Index = 0u; Index < Cache.Capacity(); ++Index)
		{
			if (Cache[Index]->HasAnyData())
			{
				++NumUsedChunks;
			}
		}

		return NumUsedChunks;
	}

	TOptional<uint32> FAudioInsightsCacheManager::TryGetNumChunksFromStartForTimestamp(const double Timestamp) const
	{
		uint32 ReadIndex = StartIndex;
		for (uint32 Index = 0u; Index < Cache.Capacity(); ++Index)
		{
			if (Cache[ReadIndex]->TimestampIsInChunkRange(Timestamp))
			{
				return Index;
			}
			else
			{
				ReadIndex = Cache.GetNextIndex(ReadIndex);
			}
		}

		return TOptional<uint32>();
	}

	const FAudioCachedMessageChunk* const FAudioInsightsCacheManager::GetChunk(const uint32 NumChunksFromStart) const
	{
		ensure(NumChunksFromStart < Cache.Capacity());
		if (NumChunksFromStart >= Cache.Capacity())
		{
			return nullptr;
		}

		uint32 ReadIndex = StartIndex;
		for (uint32 Index = 0u; Index < NumChunksFromStart; ++Index)
		{
			ReadIndex = Cache.GetNextIndex(ReadIndex);
		}

		if (!Cache[ReadIndex]->HasAnyData())
		{
			return nullptr;
		}

		return Cache[ReadIndex].Get();
	}

	void FAudioInsightsCacheManager::CreateCache()
	{
		for (uint32 Index = 0u; Index < Cache.Capacity(); ++Index)
		{
			Cache[Index] = MakeUnique<FAudioCachedMessageChunk>(Allocator, CacheManagerPrivate::MaxChunkSize);
		}

		WriteIndex = 0u;
		StartIndex = 0u;
	}

	void FAudioInsightsCacheManager::ProcessPendingMessages(float DeltaTime)
	{
		check(WriteIndex < Cache.Capacity());

		FAudioCachedMessageChunk* Chunk = Cache[WriteIndex].Get();

		const TArray<TSharedPtr<IAudioCachedMessage>> NewMessages = NewMessageQueue.DequeueAll();
		for (const TSharedPtr<IAudioCachedMessage>& Message : NewMessages)
		{
			if (Chunk->IsChunkFull())
			{
				// Move to next chunk
				WriteIndex = Cache.GetNextIndex(WriteIndex);

				Chunk = Cache[WriteIndex].Get();

				// If this new chunk already has data, clear it before we begin writing
				if (Chunk->HasAnyData())
				{
					StartIndex = Cache.GetNextIndex(WriteIndex);
					Chunk->ClearChunk();
				}

				Chunk->AddMessageToChunk(Message);

				OnChunkOverwritten.Broadcast(GetCacheStartTimeStamp());
			}
			else
			{
				Chunk->AddMessageToChunk(Message);
			}
		}
	}
} // namespace UE::Audio::Insights
