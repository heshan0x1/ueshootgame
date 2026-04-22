// Copyright Epic Games, Inc. All Rights Reserved.

#include "Extensions/Text3DDefaultCharacterExtension.h"

#include "Characters/Text3DDefaultCharacter.h"
#include "Logs/Text3DLogs.h"
#include "Misc/EnumerateRange.h"
#include "Text3DComponent.h"
#include "Utilities/Text3DUtilities.h"

uint16 UText3DDefaultCharacterExtension::GetCharacterCount() const
{
	return TextCharacters.Num();
}

UText3DCharacterBase* UText3DDefaultCharacterExtension::GetCharacter(uint16 InIndex) const
{
	if (TextCharacters.IsValidIndex(InIndex))
	{
		return TextCharacters[InIndex];
	}

	return nullptr;
}

TConstArrayView<UText3DCharacterBase*> UText3DDefaultCharacterExtension::GetCharacters() const
{
	return TextCharacters;
}

void UText3DDefaultCharacterExtension::AllocateCharacters(uint16 InCount)
{
	AllocateTextCharacters(InCount);
}

void UText3DDefaultCharacterExtension::AllocateTextCharacters(uint16 InCharacterCount)
{
	TextCharacters.RemoveAll([](const UText3DCharacterBase* InCharacter)
		{
			return !InCharacter;
		}
	);

	if (TextCharacters.Num() == InCharacterCount)
	{
		return;
	}

	TextCharacters.Reserve(InCharacterCount);

	const int32 RemainingCharacterCount = TextCharacters.Num() - InCharacterCount;

	if (RemainingCharacterCount > 0)
	{
		for (int32 CharacterIndex = InCharacterCount; CharacterIndex < TextCharacters.Num(); CharacterIndex++)
		{
			if (UText3DCharacterBase* Character = TextCharacters[CharacterIndex])
			{
				Character->ResetCharacterState();
				TextCharactersPool.Add(Character);
			}
		}
	}
	else if (RemainingCharacterCount < 0)
	{
		for (int32 CharacterIndex = 0; CharacterIndex < FMath::Abs(RemainingCharacterCount); ++CharacterIndex)
		{
			UText3DCharacterBase* TextCharacter = nullptr;

			while (!TextCharactersPool.IsEmpty() && !TextCharacter)
			{
				TextCharacter = TextCharactersPool.Pop();
			}

			if (!TextCharacter)
			{
				const FName ObjectName = MakeUniqueObjectName(this, UText3DDefaultCharacter::StaticClass(), FName(TEXT("Char")));
				TextCharacter = NewObject<UText3DCharacterBase>(this, UText3DDefaultCharacter::StaticClass(), ObjectName, RF_NoFlags);
			}

			TextCharacters.Add(TextCharacter);
		}
	}

	TextCharacters.SetNum(InCharacterCount);
}
