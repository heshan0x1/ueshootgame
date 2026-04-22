// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/CircularBuffer.h"
#include "MoverLog.h"
#include "UObject/Interface.h"
#include "UObject/Object.h"
#include "Templates/SharedPointer.h"
#include "Templates/Casts.h"
#include "RollbackBlackboard.generated.h"

struct FMoverTimeStep;

#define UE_API MOVER_API


/** RollbackBlackboard: this is a generic map that stores any type of data for local-only access. It will not be replicated.
 *  It can be used as a way for decoupled systems to store calculations or transient state data that isn't 
 *  necessary to reconstitute the movement simulation from scratch.  
 *  Examples: the normal of the current walkable floor, or time of the last movement mode change
 *  
 * Key Features:
 *  - Rollback support: when movement simulations roll back during a correction, this blackboard also rolls back to previous values.
 *  - Concurrent access: for async simulations, it provides concurrent access support so external systems / scripting can access data while new data is being authored
 *  - Policies: there are a variety of policy options to control buffer sizing, invalidation behavior, and entry persistence.
 * 
 * Notes:
 *  - This is implemented under-the-hood using circular buffers, alleviating the need for mem alloc/frees during use. It's important to create entries with policy settings appropriate for their use pattern.
 *  - We use an "InternalWrapper" class to allow in-simulation objects like movement modes to use the same API, without needing to choose between internal- or external-facing functions. 
 *  - The "InternalWrapper" class will be replaced by a different pattern to govern access levels
 */

 // TODO: 
 // - Implement stronger concurrency controls at time of frame changes, and when new in-sim entries are written
 // - Implement sizing policies that rely on knowing a max number of elements to buffer (the backend may know these details)
 // - Consider using TArrays instead of circular buffers, due to the power-of-2 trick potentially wasting a lot of memory
 // - Expose for Blueprint use

/**
 * Determines how a blackboard's entry buffer is sized
 */
UENUM()
enum class EBlackboardSizingPolicy : uint8
{
	/** Buffer size is set at a given number and and never changes */
	FixedDeclaredSize,

	/** Buffer size matches the backend simulation's history size, determined by ticking rate and history settings.
	 * A simulation running a fixed 30 fps with a 1 second history will need a buffer size of 30. For variable rate simulations, buffer size cam only be estimated. 
	 * NOT YET IMPLEMENTED
	 */
	FixedBackendBufferSize UMETA(Hidden),

	/** Minimizes data size for blackboard entries where thread contention and rolling back shouldn't change the value.
	 * Example: a statistic tracking the maximum time spent in a particular mode's simulation tick. 
	 * Note this will still have multiple elements for asynchronous simulations, to prevent thread contention over the entry.
	 * NOT YET IMPLEMENTED
	 */
	SingleEntry UMETA(Hidden),
};

/**
 * Determines how long a blackboard entry is valid once its been set.
 */
UENUM()
enum class EBlackboardPersistencePolicy : uint8
{
	/** Any edits to the entry remain valid and readable indefinitely */
	Forever,

	/** Any edits remain valid until the end of the next simulation frame. If an edit occurs during frame N, the entry will be readable for the remainder of frame N and frame N+1. */
	NextFrameOnly,

	/** NOT YET IMPLEMENTED. Any edits remain valid until a specified amount of simulation time has passed. */
	LimitedTime UMETA(Hidden),
};


/**
 * Determines how a blackboard's buffered entries are treated when a rollback occurs
 */
UENUM()
enum class EBlackboardRollbackPolicy : uint8
{
	/** Entries occurring after the rollback time are effectively erased and no longer retrievable. New entries may be written during resimulation. */
	InvalidatedOnRollback,

	/** Entries are not invalidated during rollback, and may be read/written during resim. If left alone, they will persist. NOT YET IMPLEMENTED. */
	WritableDuringResimulation UMETA(Hidden),

	/** Entries are not invalidated during rollback, and may be read during resim. But writes cannot be performed during resim. NOT YET IMPLEMENTED. */
	LockedDuringResimulation UMETA(Hidden),
};




/**
 * This is the core rollback blackboard, with API for external access.
 * See @URollbackBlackboard_InternalWrapper for in-simulation use.
 */ 
UCLASS(MinimalAPI)
class URollbackBlackboard : public UObject
{
	GENERATED_BODY()

public:
	struct EntrySettings
	{
		EBlackboardSizingPolicy      SizingPolicy      = EBlackboardSizingPolicy::FixedDeclaredSize;
		EBlackboardPersistencePolicy PersistencePolicy = EBlackboardPersistencePolicy::Forever;
		EBlackboardRollbackPolicy    RollbackPolicy    = EBlackboardRollbackPolicy::InvalidatedOnRollback;

		uint32                       FixedSize = 0;
		double                       MaxLifetimeSecs = -1.0;
	};

private:

	struct EntryTimeStamp
	{
		EntryTimeStamp() {}
		EntryTimeStamp(double InTimeMs, uint32 InFrame) : TimeMs(InTimeMs), Frame(InFrame) {}

		double TimeMs = -1.0;
		uint32 Frame = 0;

		bool IsValid() const
		{
			return TimeMs >= 0.0;
		}

		void Invalidate()
		{
			TimeMs = -1.0;
			Frame = 0;
		}
	};

	enum EEntryIndexType : uint8
	{
		External,	// entry index for outside systems to read
		Internal	// entry index for in-simulation system to read/write
	};

	// untyped base, so we can use entries of multiple types in a single-typed container
	struct BlackboardEntryBase
	{
	public:
		BlackboardEntryBase() = delete;
		UE_API BlackboardEntryBase(const EntrySettings& InSettings, uint32 BufferSize);

		virtual ~BlackboardEntryBase()
		{
		}
		
		void OnSimulationFrameEnd()
		{
			//Advances the external index to match
			ExternalIdx = InternalIdx;
		}

		// Note that NewPendingFrame means the frame that will now be re-simulated. So any existing entries with a Frame >= NewPendingFrame can be invalidated.
		UE_API void RollBack(uint32 NewPendingFrame);

		UE_API static uint32 ComputeBufferSize(EBlackboardSizingPolicy InSizingPolicy, uint32 FixedBufferSize = -1);

	protected:
		UE_API bool CanReadEntryAt(const EntryTimeStamp& ReaderTimeStamp, EEntryIndexType IndexType) const;

		EntrySettings Settings;

		TCircularBuffer<EntryTimeStamp> Timestamps;

		uint32 ExternalIdx;	// Indexes to the last entry on a committed frame
		uint32 InternalIdx;	// Indexes to where in-progress simulations should read/write (matches ExternalIdx when not mid-simulation, and advances if written during simulation)
	};

	template<typename EntryT>
	struct BlackboardEntry : BlackboardEntryBase
	{
	private:

		BlackboardEntry() = delete;	// disallow use of default constructor

	public:

		// TODO: consider what options need to be specified
		BlackboardEntry(const EntrySettings& InSettings, uint32 InBufferSize, const EntryT* OptionalInitialObj=nullptr)
			: BlackboardEntryBase(InSettings, InBufferSize)
			, EntryBuffer(InBufferSize)
		{
			// JAH TODO: Consider making the initial object required, so we can also initialize where in the buffer we point to
			if (OptionalInitialObj)
			{
				for (uint32 i = 0; i < EntryBuffer.Capacity(); i++)
				{
					EntryBuffer[i] = *OptionalInitialObj;
				}
			}
		}


		bool TryGetEntryValue(const EntryTimeStamp& CurrentExternalTime, EntryT& ValueOut)
		{
			if (CanReadEntryAt(CurrentExternalTime, EEntryIndexType::External))
			{
				ValueOut = EntryBuffer[ExternalIdx];
				return true;
			}

			return false;
		}

		// TODO: Consider whether it's worthwhile to have this function... dangerous to let external folks hold onto references to internal storage, but may allow more efficient editing
		EntryT* TryGetEntryRef(const EntryTimeStamp& CurrentExternalTime)
		{
			if (CanReadEntryAt(CurrentExternalTime, EEntryIndexType::External))
			{ 
				return &EntryBuffer[ExternalIdx];
			}

			return nullptr;
		}

		bool TryGetEntryValue_Internal(const EntryTimeStamp& InternalTime, EntryT& ValueOut)
		{
			if (CanReadEntryAt(InternalTime, EEntryIndexType::Internal))
			{
				ValueOut = EntryBuffer[InternalIdx];
				return true;
			}

			return false;
		}

		EntryT* TryGetEntryRef_Internal(const EntryTimeStamp& InternalTime)
		{
			if (CanReadEntryAt(InternalTime, EEntryIndexType::Internal))
			{
				return &EntryBuffer[InternalIdx];
			}

			return nullptr;
		}

		void SetEntryValue_Internal(const EntryT& Value, const EntryTimeStamp& TimeStamp)
		{
			InternalIdx = ExternalIdx + 1;	// always writing internal, one slot ahead of external slot

			EntryBuffer[InternalIdx] = Value;
			Timestamps[InternalIdx] = TimeStamp;
		}

	
		// TODO: Consider whether these need to be TCircularBuffers, or whether a TArray would be just fine.  TCircularBuffers round up to the next power of 2 in capacity, so may be wasting significant memory for larger structures.
		// Note that these EntryBuffer is always the same capacity as Timestamps
		TCircularBuffer<EntryT> EntryBuffer;


	};	// end BlackboardEntry
	


public:
	template<typename EntryT>
	bool CreateEntry(FName EntryName, const EntrySettings& InSettings)
	{
		if (EntryMap.Contains(EntryName))
		{
			UE_LOG(LogMover, Warning, TEXT("Skipping attempt to create a new blackboard entry named %s since it already exists. Policy settings from the first entry will be retained."), *EntryName.ToString());
			return false;
		}

		const uint32 BufferSize = BlackboardEntryBase::ComputeBufferSize(InSettings.SizingPolicy, InSettings.FixedSize);
		EntryMap.Emplace(EntryName, MakeUnique<BlackboardEntry<EntryT>>(InSettings, BufferSize, nullptr));
		return true;
	}

	bool HasEntry(FName EntryName) const
	{
		return EntryMap.Contains(EntryName);
	}

	/** Store object by a named key, overwriting any existing object */
	template<typename EntryT>
	bool TrySet(FName ObjName, EntryT& Obj)
	{
		// TODO: Verify this isn't happening mid-sim on the sim thread (_Internal should be used instead)

		if (BlackboardEntry<EntryT>* Entry = static_cast<BlackboardEntry<EntryT>*>(FindEntry(ObjName)))
		{
			Entry->SetEntryValue(Obj);
			return true;
		}

		return false;
	}


	/** Attempt to retrieve an object from the blackboard. If found, OutFoundValue will be set. Returns true/false to indicate whether it was found. */
	template<typename EntryT>
	bool TryGet(FName ObjName, EntryT& OutFoundValue) const
	{
		if (BlackboardEntry<EntryT>* Entry = static_cast<BlackboardEntry<EntryT>*>(FindEntry(ObjName)))
		{
			return Entry->TryGetEntryValue(CurrentSimTimeStamp, OutFoundValue);
		}

		return false;
	}

	/** Users may access the reference of the entry, which may be more efficient than copy - out, copy - in.
	 * Although you can get access to the reference, it is not safe to hold references over time. 
	 */
	template<typename EntryT>
	EntryT* TryGetRef(FName ObjName) const
	{
		if (BlackboardEntry<EntryT>* Entry = static_cast<BlackboardEntry<EntryT>*>(FindEntry(ObjName)))
		{
			return Entry->TryGetEntryRef(CurrentSimTimeStamp);
		}

		return nullptr;
	}


private:
	/** Store object by a named key, overwriting any existing object. Should only be called from within the simulation. */
	template<typename EntryT>
	bool TrySet_Internal(FName ObjName, const EntryT& Obj)
	{
		check((bIsSimulationInProgress && (InProgressSimFrameThreadId == FPlatformTLS::GetCurrentThreadId())) || 
			  (bIsRollbackInProgress && (InRollbackThreadId == FPlatformTLS::GetCurrentThreadId()))) ;

		if (BlackboardEntry<EntryT>* Entry = static_cast<BlackboardEntry<EntryT>*>(FindEntry(ObjName)))
		{
			Entry->SetEntryValue_Internal(Obj, InProgressSimTimeStamp);
			return true;
		}

		return false;
	}

	template<typename EntryT>
	bool TryGet_Internal(FName ObjName, EntryT& OutFoundValue) const
	{
		check((bIsSimulationInProgress && (InProgressSimFrameThreadId == FPlatformTLS::GetCurrentThreadId())) ||
			  (bIsRollbackInProgress && (InRollbackThreadId == FPlatformTLS::GetCurrentThreadId())));

		if (BlackboardEntry<EntryT>* Entry = static_cast<BlackboardEntry<EntryT>*>(FindEntry(ObjName)))
		{
			return Entry->TryGetEntryValue_Internal(InProgressSimTimeStamp, OutFoundValue);
		}

		return false;
	}

	template<typename EntryT>
	EntryT* TryGetRef_Internal(FName ObjName) const
	{
		check((bIsSimulationInProgress && (InProgressSimFrameThreadId == FPlatformTLS::GetCurrentThreadId())) ||
			  (bIsRollbackInProgress && (InRollbackThreadId == FPlatformTLS::GetCurrentThreadId())));

		if (BlackboardEntry<EntryT>* Entry = static_cast<BlackboardEntry<EntryT>*>(FindEntry(ObjName)))
		{
			return Entry->TryGetEntryRef_Internal(InProgressSimTimeStamp);
		}

		return nullptr;
	}



	BlackboardEntryBase* FindEntry(FName EntryName) const
	{
		if (const TUniquePtr<BlackboardEntryBase>* EntryPtr = EntryMap.Find(EntryName))
		{
			return EntryPtr->Get();
		}

		return nullptr;
	}


private:
	UE_API void BeginSimulationFrame(const FMoverTimeStep& PendingTimeStep);
	UE_API void EndSimulationFrame();

	UE_API void BeginRollback(const FMoverTimeStep& NewBaseTimeStep);	// NewBaseTimeStep represents the current time and the PENDING frame that's next to be resimulated
	UE_API void EndRollback();


private:
	
	TMap<FName, TUniquePtr<BlackboardEntryBase>> EntryMap;


	bool bIsSimulationInProgress = false;
	bool bIsResimulating = false;
	uint32 InProgressSimFrameThreadId;

	bool bIsRollbackInProgress = false;
	uint32 InRollbackThreadId;


	EntryTimeStamp CurrentSimTimeStamp;	// this is the "committed" simulation time, and will lag behind InProgressSimTimeStamp while in the middle of a simulation frame
	EntryTimeStamp InProgressSimTimeStamp;	// this is the sim time that's actively being used mid-simulation. It is only valid during a simulation step.

	friend class URollbackBlackboard_InternalWrapper;	// expose non-public API to this wrapper class
};



/** 
 * Wrapper class for in-simulation access to the blackboard. This exposes otherwise private API, and redirects
 * set/get operations to the internal versions.
 */
UCLASS(MinimalAPI)
class URollbackBlackboard_InternalWrapper : public UObject
{
	GENERATED_BODY()

public:
	void Init(URollbackBlackboard& InBlackboard)
	{
		Blackboard = &InBlackboard;
	}

	template<typename EntryT>
	bool CreateEntry(FName EntryName, const URollbackBlackboard::EntrySettings& InSettings)
	{
		return Blackboard->CreateEntry<EntryT>(EntryName, InSettings);
	}

	bool HasEntry(FName EntryName) const
	{
		return Blackboard->HasEntry(EntryName);
	}

	template<typename EntryT>
	bool TrySet(FName ObjName, const EntryT& Obj)
	{
		return Blackboard->TrySet_Internal<EntryT>(ObjName, Obj);
	}

	template<typename EntryT>
	bool TryGet(FName ObjName, EntryT& OutFoundValue) const
	{
		return Blackboard->TryGet_Internal(ObjName, OutFoundValue);
	}

	template<typename EntryT>
	EntryT* TryGetRef(FName ObjName) const
	{
		return Blackboard->TryGetRef_Internal<EntryT>(ObjName);
	}

	UE_API void BeginSimulationFrame(const FMoverTimeStep& PendingTimeStep);
	UE_API void EndSimulationFrame();

	UE_API void BeginRollback(const FMoverTimeStep& NewBaseTimeStep);
	UE_API void EndRollback();

private: 
	UPROPERTY()
	TObjectPtr<URollbackBlackboard> Blackboard;
};

#undef UE_API
