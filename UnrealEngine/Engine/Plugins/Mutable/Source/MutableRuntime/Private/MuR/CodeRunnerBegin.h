// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/SystemPrivate.h"


namespace UE::Mutable::Private
{
	class FProgramCache;
	class FInstance;
	class FExtensionData;
	struct FProgram;
	class FModel;
	class FParameters;
	class FSystem;
	class String;

	struct FScheduledOpInline
	{
		explicit FScheduledOpInline(OP::ADDRESS InAt = 0, const uint16 InStage = 0, const uint16 InExecutionIndex = 0) :
			At(InAt),
			Stage(InStage),
		 	ExecutionIndex(InExecutionIndex)
		{
		}
		
		explicit FScheduledOpInline(OP::ADDRESS InAt, const FScheduledOpInline& InOpTemplate) :
			At(InAt),
     		ExecutionIndex(InOpTemplate.ExecutionIndex)
		{
		}
		
		FScheduledOpInline(const FScheduledOpInline& InOpTemplate, uint16 InStage) :
			At(InOpTemplate.At),
         	Stage(InStage),
            ExecutionIndex(InOpTemplate.ExecutionIndex)
		{
		}

		bool operator==(const FScheduledOpInline& Other) const;

		OP::ADDRESS At = 0;
		uint16 Stage = 0;
		uint16 ExecutionIndex = 0;
		OP::ADDRESS CustomState = 0;
	};


	uint32 GetTypeHash(const FScheduledOpInline& Op);


	struct FCacheAddressInline
	{
		FCacheAddressInline(const OP::ADDRESS& InAt)
		{
			At = InAt;
		}
		
		FCacheAddressInline(const FScheduledOpInline& Item)
		{
			At = Item.At;
			ExecutionIndex = Item.ExecutionIndex;
		}
		
		OP::ADDRESS At = 0;
		uint16 ExecutionIndex = 0;

		bool operator==(const FCacheAddressInline&) const = default;
	};


	uint32 GetTypeHash(const FCacheAddressInline& Address);


	class FOpSet
	{
	public:
		FOpSet(int32 NumElements);

		bool Contains(const FCacheAddressInline& Item);

		void Add(const FCacheAddressInline& Item);

	private:
		TBitArray<> Index0;
		TSet<FCacheAddressInline> IndexOther;
	};
	
	using FStackValue = TVariant<bool, int32, float, TSharedPtr<const String>, TSharedPtr<const FExtensionData>, TSharedPtr<const FInstance>>;

	class FStack : public TArray<FStackValue, TInlineAllocator<32>>
	{
	public:
		FStackValue Pop();
	};
	
	class CodeRunnerBegin
	{
	public:
		CodeRunnerBegin(FSystem& InSystem, const TSharedPtr<const FModel>& InModel, const TSharedPtr<const FParameters>& InParams, uint32 InLodMask);

		TSharedPtr<const FInstance> RunCode(const FScheduledOpInline& Root);

		FProgramCache& GetMemory() const;
		
		FScheduledOpInline PopOp();
		
		void PushOp(const FScheduledOpInline& Item);
		
		void StoreInt(const FCacheAddressInline& To, int32 Value);
		
		void StoreFloat(const FCacheAddressInline& To, float Value);
		
		void StoreBool(const FCacheAddressInline& To, bool Value);
		
		void StoreInstance(const FCacheAddressInline& To, const TSharedPtr<const FInstance>& Value);
		
		void StoreString(const FCacheAddressInline& To, const TSharedPtr<const String>& Value);
		
		void StoreExtensionData(const FCacheAddressInline& To, const TSharedPtr<const FExtensionData>& Value);

		void StoreNone(const FCacheAddressInline& To);

		int32 LoadInt(const FCacheAddressInline& To);

		float LoadFloat(const FCacheAddressInline& To);

		bool LoadBool(const FCacheAddressInline& To);
		
		TSharedPtr<const FInstance> LoadInstance(const FCacheAddressInline& To);

		TSharedPtr<const String> LoadString(const FCacheAddressInline& To);

		TSharedPtr<const FExtensionData> LoadExtensionData(const FCacheAddressInline& To);
		
		TSharedPtr<const FModel> Model;
		TSharedPtr<const FParameters> Params;

		FSystem& System;
		FProgram& Program;
		
		uint32 LodMask;

		TArray<FScheduledOpInline, TInlineAllocator<256, TAlignedHeapAllocator<PLATFORM_CACHE_LINE_SIZE>>> Items;

		FStack Stack;
		
		FOpSet Executed;

		TMap<FCacheAddressInline, float> ResultsFloat;
		TMap<FCacheAddressInline, bool> ResultsBool;
		TMap<FCacheAddressInline, int32> ResultsInt;
		TMap<FCacheAddressInline, TSharedPtr<const FInstance>> ResultsInstance;
		TMap<FCacheAddressInline, TSharedPtr<const String>> ResultsString;
		TMap<FCacheAddressInline, TSharedPtr<const FExtensionData>> ResultsExtensionData;
		
		TArray<int32> MI_REFERENCE_ID;
		TArray<int32> IM_REFERENCE_ID;
		TArray<int32> ME_REFERENCE_ID;
    	
		TArray<int32> ME_SKELETON_ID;
	};
}
