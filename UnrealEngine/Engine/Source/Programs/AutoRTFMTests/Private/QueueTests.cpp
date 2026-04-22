// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutoRTFMTesting.h"
#include "Catch2Includes.h"
#include "Containers/TransactionallySafeSpscQueue.h"

#include <thread>

// Test loosely based on code from Containers/ConcurrentQueuesTest.cpp.
template <typename QueueType>
void TestSpscQueueCorrectness(int Num, bool bTransact)
{
	QueueType Queue;
	uint32 NumProduced = 0;
	uint32 NumConsumed = 0;
	std::thread ConsumeTask;

	// Consumer
	auto ConsumeLoop = [&Queue, &NumConsumed, Num]
	{
		while (NumConsumed != Num)
		{
			if (Queue.Dequeue().IsSet())
			{
				++NumConsumed;
			}
		}
	};

	// Producer
	auto ProduceLoop = [&Queue, &NumProduced, Num]
	{
		do
		{
			Queue.Enqueue(typename QueueType::ElementType{});
		} 
		while (++NumProduced != Num);
	};
	
	// Run the consumer loop on a helper thread.
	UE_AUTORTFM_OPEN
	{
		ConsumeTask = std::thread{ConsumeLoop};
	};

	// Run the producer loop within a transaction.
	if (bTransact)
	{
		AutoRTFM::Testing::Commit([&]
		{
			ProduceLoop();
		});
	}
	else
	{
		ProduceLoop();
	}

	// Join the consumer thread to allow it to finish.
	UE_AUTORTFM_OPEN
	{
		ConsumeTask.join();
	};

	// We should have produced and consumed exactly Num items, and the queue should be empty.
	REQUIRE(NumProduced == Num);
	REQUIRE(NumConsumed == Num);
	REQUIRE(!Queue.Dequeue().IsSet());
}

TEST_CASE("TransactionallySafeSpscQueue")
{
	// Non-transactional, trivial and complex types
	TestSpscQueueCorrectness<TTransactionallySafeSpscQueue<int>>(10000, /*bTransact=*/false);
	TestSpscQueueCorrectness<TTransactionallySafeSpscQueue<FString>>(10000, /*bTransact=*/false);

	// Transactional, trivial and complex types
	TestSpscQueueCorrectness<TTransactionallySafeSpscQueue<int>>(10000, /*bTransact=*/true);
	TestSpscQueueCorrectness<TTransactionallySafeSpscQueue<FString>>(10000, /*bTransact=*/true);
}
