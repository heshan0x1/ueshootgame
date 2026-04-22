// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CHAOS_VISUAL_DEBUGGER
#include <atomic>
#include "Containers/Array.h"
#include "Containers/SpscQueue.h"
#include "Delegates/Delegate.h"
#include "Serialization/BitWriter.h"
#include "Templates/UniquePtr.h"

#pragma once

class FBufferArchive;

namespace Chaos::VD
{
/**
 * Writer object that chunks trace data into smaller bunches and queues them so a relay instance
 * can dequeue and send them over the network later
 */
class FRelayTraceWriter
{
public:
	FRelayTraceWriter();
	~FRelayTraceWriter();

	/**
	 * Sets max number of bytes a queued bunch can have
	 * @param MaxBytes Greater than zero number of bytes to set as limit
	 */
	CHAOSVDRUNTIME_API void SetMaxBytesPerBunch(uint32 MaxBytes);

	static bool WriteHelper(UPTRINT WriterHandle, const void* Data, uint32 Size)
	{
		if (!WriterHandle)
		{
			return false;
		}

		return reinterpret_cast<FRelayTraceWriter*>(WriterHandle)->Write_Internal(Data, Size);
	}

	static void CloseHelper(UPTRINT WriterHandle)
	{
		if (!WriterHandle)
		{
			return;
		}

		reinterpret_cast<FRelayTraceWriter*>(WriterHandle)->Close();
	}

	/**
	 * Dequeues a bunch ready to send over the network, if any
	 */
	CHAOSVDRUNTIME_API TOptional<TUniquePtr<FBufferArchive>> DequeuePendingBunch();

	/**
	 * Returns the size of the next bunch in the queue
	 */
	CHAOSVDRUNTIME_API int32 NextPendingBunchSize() const;

	/**
	 * Returns true if we have any data pending to be sent over the network
	 */
	bool HasPendingBunches() const
	{
		return PendingBunchesCount > 0;
	}

	/**
	 * returns the number of bytes overall that are in the queue waiting to be sent
	 */
	CHAOSVDRUNTIME_API int64 GetQueuedBytesNum() const;

	/**
	 * Closes this Writer. After this is called, no new data will be processed, but any pending data can still be dequeued and sent over
	 */
	void Close();

	/**
	 * Returns true if this writer is closed and no new data is being processed
	 */
	bool IsClosed() const
	{
		return bClosed;
	}

	DECLARE_DELEGATE(FNewDataAvailableDelegate)
	FNewDataAvailableDelegate& OnNewDataAvailable()
	{
		return NewDataAvailableDelegate;
	}

	private:

	FNewDataAvailableDelegate NewDataAvailableDelegate;

	TUniquePtr<FBufferArchive> CreateBunch();

	bool Write_Internal(const void* Data, uint32 Size);

	TSpscQueue<TUniquePtr<FBufferArchive>> PendingBunchesToRelay;
	TUniquePtr<FBufferArchive> CurrentPendingBunch;
	std::atomic<int32> PendingBunchesCount = 0;
	std::atomic<bool> bClosed = false;
	uint32 MaxPendingBytesPerBunch = 0;
	std::atomic<int64> QueuedBytesNum = 0;
};
}
#endif // WITH_CHAOS_VISUAL_DEBUGGER

