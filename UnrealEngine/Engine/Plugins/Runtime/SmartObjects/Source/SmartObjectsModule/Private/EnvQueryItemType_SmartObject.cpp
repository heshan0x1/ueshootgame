// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvQueryItemType_SmartObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EnvQueryItemType_SmartObject)


UEnvQueryItemType_SmartObject::UEnvQueryItemType_SmartObject() 
{
	ValueSize = sizeof(FSmartObjectSlotEQSItem);
}

const FSmartObjectSlotEQSItem& UEnvQueryItemType_SmartObject::GetValue(const uint8* RawData)
{
	return GetValueFromMemory<FSmartObjectSlotEQSItem>(RawData);
}

void UEnvQueryItemType_SmartObject::SetValue(uint8* RawData, const FSmartObjectSlotEQSItem& Value)
{
	return SetValueInMemory<FSmartObjectSlotEQSItem>(RawData, Value);
}

FVector UEnvQueryItemType_SmartObject::GetItemLocation(const uint8* RawData) const
{
	return UEnvQueryItemType_SmartObject::GetValue(RawData).Location;
}
