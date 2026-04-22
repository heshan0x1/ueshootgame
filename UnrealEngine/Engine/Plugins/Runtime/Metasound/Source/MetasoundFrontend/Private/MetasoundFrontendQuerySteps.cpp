// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendQuerySteps.h"

#include "Algo/MaxElement.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendNodeClassRegistry.h"
#include "MetasoundFrontendNodeClassRegistryPrivate.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendRegistryTransaction.h"
#include "MetasoundLog.h"

namespace Metasound
{
	class FNodeClassRegistrationEventsPimpl : public IFrontendQueryStreamStep
	{
	public:
		FNodeClassRegistrationEventsPimpl()
		{
			TransactionStream = Frontend::FNodeClassRegistry::Get().CreateTransactionStream();
		}

		virtual void Stream(TArray<FFrontendQueryValue>& OutValues) override
		{
			using namespace Frontend;

			auto AddEntry = [&OutValues](const FNodeRegistryTransaction& InTransaction)
			{
				OutValues.Emplace(TInPlaceType<FNodeClassRegistryTransaction>(), InTransaction);
			};

			if (TransactionStream.IsValid())
			{
				TransactionStream->Stream(AddEntry);
			}
		}

	private:
		TUniquePtr<Frontend::FNodeClassRegistryTransactionStream> TransactionStream;
	};

	FNodeClassRegistrationEvents::FNodeClassRegistrationEvents()
	: Pimpl(MakePimpl<FNodeClassRegistrationEventsPimpl>())
	{
	}

	void FNodeClassRegistrationEvents::Stream(TArray<FFrontendQueryValue>& OutValues)
	{
		Pimpl->Stream(OutValues);
	}

	FFrontendQueryKey FMapRegistrationEventsToNodeRegistryKeys::Map(const FFrontendQueryEntry& InEntry) const 
	{
		using namespace Frontend;

		FNodeRegistryKey RegistryKey;

		if (ensure(InEntry.Value.IsType<FNodeRegistryTransaction>()))
		{
			RegistryKey = InEntry.Value.Get<FNodeRegistryTransaction>().GetNodeRegistryKey();
		}

		return FFrontendQueryKey(RegistryKey.ToString());
	}

	void FReduceRegistrationEventsToCurrentStatus::Reduce(const FFrontendQueryKey& InKey, FFrontendQueryPartition& InOutEntries) const
	{
		using namespace Frontend;

		// Track number of times each type of entry has been seen
		struct FTransactionStats
		{
			int32 Num = 0;
			const FFrontendQueryEntry* MostRecent = nullptr;

			void Update(const FFrontendQueryEntry& InEntry)
			{
				Num++;
				if (nullptr == MostRecent)
				{
					MostRecent = &InEntry;
				}
				else
				{
					if (FReduceRegistrationEventsToCurrentStatus::GetTransactionTimestamp(InEntry) > FReduceRegistrationEventsToCurrentStatus::GetTransactionTimestamp(*MostRecent))
					{
						MostRecent = &InEntry;
					}
				}
			}
		};

		// Accumulate the counts of each transaction type
		FTransactionStats NodeRegistrationStats;
		FTransactionStats NodeUnregistrationStats;
		FTransactionStats MigrationRegistrationStats;
		FTransactionStats MigrationUnregistrationStats;

		for (const FFrontendQueryEntry& Entry : InOutEntries)
		{
			const FNodeClassRegistryTransaction& Transaction = Entry.Value.Get<FNodeRegistryTransaction>();
			switch (Transaction.GetTransactionType())
			{
				case FNodeClassRegistryTransaction::ETransactionType::NodeRegistration:
					NodeRegistrationStats.Update(Entry);
					break;

				case FNodeClassRegistryTransaction::ETransactionType::NodeUnregistration:
					NodeUnregistrationStats.Update(Entry);
					break;

				case FNodeClassRegistryTransaction::ETransactionType::NodeMigrationRegistration:
					MigrationRegistrationStats.Update(Entry);
					break;

				case FNodeClassRegistryTransaction::ETransactionType::NodeMigrationUnregistration:
					MigrationUnregistrationStats.Update(Entry);
					break;

				default:
					{
						checkNoEntry();
					}
			}
		}

		// Use the final number of each transaction type to determine the final state
		FFrontendQueryPartition RemainingEntries;
		if (NodeRegistrationStats.Num > NodeUnregistrationStats.Num)
		{
			// Registration trumps migration, so we do not consider migration at all here.
			RemainingEntries.Add(*NodeRegistrationStats.MostRecent);
		}
		else 
		{
			// Make sure to propagate unregistration actions since reduce steps may get called
			// on subpartitions. 
			if (NodeUnregistrationStats.Num > NodeRegistrationStats.Num)
			{
				RemainingEntries.Add(*NodeUnregistrationStats.MostRecent);
			}
			
			// If  not registered, check if migrated
			if (MigrationRegistrationStats.Num > MigrationUnregistrationStats.Num)
			{
				RemainingEntries.Add(*MigrationRegistrationStats.MostRecent);
			}
			else if (MigrationUnregistrationStats.Num > MigrationRegistrationStats.Num)
			{
				RemainingEntries.Add(*MigrationUnregistrationStats.MostRecent);
			}
		}

		InOutEntries = MoveTemp(RemainingEntries);
	}

	FReduceRegistrationEventsToCurrentStatus::FTimeType FReduceRegistrationEventsToCurrentStatus::GetTransactionTimestamp(const FFrontendQueryEntry& InEntry)
	{
		using namespace Frontend;

		if (ensure(InEntry.Value.IsType<FNodeRegistryTransaction>()))
		{
			return InEntry.Value.Get<FNodeRegistryTransaction>().GetTimestamp();
		}
		return 0;
	}

	bool FReduceRegistrationEventsToCurrentStatus::IsValidTransactionOfType(Frontend::FNodeRegistryTransaction::ETransactionType InType, const FFrontendQueryEntry* InEntry)
	{
		using namespace Frontend;

		if (nullptr != InEntry)
		{
			if (InEntry->Value.IsType<FNodeRegistryTransaction>())
			{
				return InEntry->Value.Get<FNodeRegistryTransaction>().GetTransactionType() == InType;
			}
		}
		return false;
	}

	void FTransformRegistrationEventsToClasses::Transform(FFrontendQueryEntry::FValue& InValue) const
	{
		using namespace Frontend;

		FMetasoundFrontendClass FrontendClass;

		if (ensure(InValue.IsType<FNodeRegistryTransaction>()))
		{
			const FNodeRegistryTransaction& Transaction = InValue.Get<FNodeRegistryTransaction>();
			
			if (Transaction.GetTransactionType() == Frontend::FNodeRegistryTransaction::ETransactionType::NodeRegistration)
			{
				// It's possible that the node is no longer registered (we're processing removals) 
				// but that's okay because the returned default FrontendClass will be processed out later
				FMetasoundFrontendRegistryContainer::Get()->FindFrontendClassFromRegistered(Transaction.GetNodeRegistryKey(), FrontendClass);
			}
		}
		InValue.Set<FMetasoundFrontendClass>(MoveTemp(FrontendClass));
	}

	FFilterClassesByInputVertexDataType::FFilterClassesByInputVertexDataType(const FName& InTypeName)
	:	InputVertexTypeName(InTypeName)
	{
	}

	bool FFilterClassesByInputVertexDataType::Filter(const FFrontendQueryEntry& InEntry) const
	{
		check(InEntry.Value.IsType<FMetasoundFrontendClass>());

		return InEntry.Value.Get<FMetasoundFrontendClass>().GetDefaultInterface().Inputs.ContainsByPredicate(
			[this](const FMetasoundFrontendClassInput& InDesc)
			{
				return InDesc.TypeName == InputVertexTypeName;
			}
		);
	}

	FFilterClassesByOutputVertexDataType::FFilterClassesByOutputVertexDataType(const FName& InTypeName)
	:	OutputVertexTypeName(InTypeName)
	{
	}

	bool FFilterClassesByOutputVertexDataType::Filter(const FFrontendQueryEntry& InEntry) const
	{
		return InEntry.Value.Get<FMetasoundFrontendClass>().GetDefaultInterface().Outputs.ContainsByPredicate(
			[this](const FMetasoundFrontendClassOutput& InDesc)
			{
				return InDesc.TypeName == OutputVertexTypeName;
			}
		);
	}

	FFrontendQueryKey FMapClassesToClassName::Map(const FFrontendQueryEntry& InEntry) const 
	{
		return FFrontendQueryKey(InEntry.Value.Get<FMetasoundFrontendClass>().Metadata.GetClassName().GetFullName());
	}

	FFilterClassesByClassID::FFilterClassesByClassID(const FGuid InClassID)
		: ClassID(InClassID)
	{
	}

	bool FFilterClassesByClassID::Filter(const FFrontendQueryEntry& InEntry) const
	{
		return InEntry.Value.Get<FMetasoundFrontendClass>().ID == ClassID;
	}

	FFrontendQueryKey FMapToFullClassName::Map(const FFrontendQueryEntry& InEntry) const
	{
		const FMetasoundFrontendClass& FrontendClass = InEntry.Value.Get<FMetasoundFrontendClass>();
		return FFrontendQueryKey(FrontendClass.Metadata.GetClassName().GetFullName());
	}

	void FReduceClassesToHighestVersion::Reduce(const FFrontendQueryKey& InKey, FFrontendQueryPartition& InOutEntries) const
	{
		FFrontendQueryEntry* HighestVersionEntry = nullptr;
		FMetasoundFrontendVersionNumber HighestVersion;

		for (FFrontendQueryEntry& Entry : InOutEntries)
		{
			const FMetasoundFrontendVersionNumber& Version = Entry.Value.Get<FMetasoundFrontendClass>().Metadata.GetVersion();

			if (!HighestVersionEntry || HighestVersion < Version)
			{
				HighestVersionEntry = &Entry;
				HighestVersion = Version;
			}
		}

		if (HighestVersionEntry)
		{
			FFrontendQueryEntry Entry = *HighestVersionEntry;
			InOutEntries.Reset();
			InOutEntries.Add(Entry);
		}
	}

	bool FSortClassesByVersion::Sort(const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS) const
	{
		const FMetasoundFrontendVersionNumber& VersionLHS = InEntryLHS.Value.Get<FMetasoundFrontendClass>().Metadata.GetVersion();
		const FMetasoundFrontendVersionNumber& VersionRHS = InEntryRHS.Value.Get<FMetasoundFrontendClass>().Metadata.GetVersion();
		return VersionLHS > VersionRHS;
	}
}
