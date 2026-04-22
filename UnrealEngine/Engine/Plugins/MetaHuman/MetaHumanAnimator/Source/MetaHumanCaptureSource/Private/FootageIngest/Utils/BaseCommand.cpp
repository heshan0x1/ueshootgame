// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commands/BaseCommand.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FBaseCommandArgs::FBaseCommandArgs(const FString& InCommandName)
	: CommandName(InCommandName)
{
}

const FString& FBaseCommandArgs::GetCommandName() const
{
	return CommandName;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS