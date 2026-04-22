// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/RelayTraceWriter.h"

#if WITH_CHAOS_VISUAL_DEBUGGER
#include "ChaosVDRuntimeModule.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace Chaos::VD
{
	FRelayTraceWriter::FRelayTraceWriter()
	{
	}

	FRelayTraceWriter::~FRelayTraceWriter()
	{
		Close();
	}

	void FRelayTraceWriter::SetMaxBytesPerBunch(uint32 MaxBytes)
	{
		if (MaxBytes == 0)
		{
			UE_LOG(LogChaosVDRuntime, Error, TEXT("[%hs] Only values greater than 0 are supported"), __func__);
			return;
		}

		if (MaxBytes != MaxPendingBytesPerBunch)
		{
			if (!ensure(PendingBunchesToRelay.IsEmpty()))
			{
				return;
			}
		}

		MaxPendingBytesPerBunch = MaxBytes;
	}


	TOptional<TUniquePtr<FBufferArchive>> FRelayTraceWriter::DequeuePendingBunch()
	{
		if (PendingBunchesCount == 0)
		{
			return TOptional<TUniquePtr<FBufferArchive>>();
		}

		QueuedBytesNum -= NextPendingBunchSize();
	
		--PendingBunchesCount;
		return PendingBunchesToRelay.Dequeue();
	}

	int32 FRelayTraceWriter::NextPendingBunchSize() const
	{
		if (PendingBunchesCount == 0)
		{
			return 0;
		}

		TUniquePtr<FBufferArchive>* PendingBunch = PendingBunchesToRelay.Peek();
		
		if (PendingBunch && PendingBunch->IsValid())
		{
			return (*PendingBunch)->Num();
		}

		return 0;
	}

	TUniquePtr<FBufferArchive> FRelayTraceWriter::CreateBunch()
	{
		TUniquePtr<FBufferArchive> NewBunch = MakeUnique<FBufferArchive>();
		return NewBunch;
	}

	bool FRelayTraceWriter::Write_Internal(const void* Data, uint32 Size)
	{
		if (bClosed)
		{
			return false;
		}

		const bool bHasValidLimits = MaxPendingBytesPerBunch != 0;
		if (!ensure(bHasValidLimits))
		{
			return false;
		}

		if (!CurrentPendingBunch)
		{
			CurrentPendingBunch = CreateBunch();
		}

		uint32 RemainingSizeToCopy = Size;

		while (RemainingSizeToCopy > 0)
		{
			int32 AvailableSpaceInCurrentBunch = MaxPendingBytesPerBunch - CurrentPendingBunch->Num();

			if (AvailableSpaceInCurrentBunch <= 0)
			{
				QueuedBytesNum += CurrentPendingBunch->Num();
				PendingBunchesToRelay.Enqueue(MoveTemp(CurrentPendingBunch));
				++PendingBunchesCount;
				CurrentPendingBunch = CreateBunch();
				AvailableSpaceInCurrentBunch = MaxPendingBytesPerBunch;

				NewDataAvailableDelegate.ExecuteIfBound();
			}

			const uint32 BytesToCopyNum = static_cast<uint32>(AvailableSpaceInCurrentBunch) > RemainingSizeToCopy ? RemainingSizeToCopy : static_cast<uint32>(AvailableSpaceInCurrentBunch);

			const uint8* DestBufferPtr = static_cast<const uint8*>(Data);
			const int32 Offset = Size - RemainingSizeToCopy;

			const bool bIsValidOffset = Offset >= 0 && static_cast<uint32>(Offset) < Size;
			if (!ensureMsgf(bIsValidOffset, TEXT("Unexpected calculated offset [%d] | Size [%u]"), Offset, Size))
			{
				return false;
			}

			CurrentPendingBunch->Serialize(const_cast<uint8*>(DestBufferPtr + Offset) , BytesToCopyNum);

			if (CurrentPendingBunch->IsError())
			{
				return false;
			}

			RemainingSizeToCopy -= BytesToCopyNum;
		}

		return !CurrentPendingBunch->IsError();
	}

	int64 FRelayTraceWriter::GetQueuedBytesNum() const
	{
		return QueuedBytesNum;
	}

	void FRelayTraceWriter::Close()
	{
		int64 PendingBytes = QueuedBytesNum;
		if (PendingBytes > 0)
		{
			// TODO: We should handle the case where we close the trace session but we still have data pending in the send queue (UE-320563).
			// We can ignore it for now as it is not a critical failure
			UE_LOG(LogChaosVDRuntime, Warning, TEXT("Closing Trace Session with [%lld] bytes in the send queue. This data will be discarded"), PendingBytes);
		}

		bClosed = true;
	}
}
#endif // WITH_CHAOS_VISUAL_DEBUGGER