// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

#define UE_API COMMONMENUEXTENSIONS_API

class FEditorViewportClient;
class FMenuBuilder;
class FUICommandInfo;
class FUICommandList;

UE_DECLARE_TCOMMANDS(class FNaniteVisualizationMenuCommands, UE_API)

class FNaniteVisualizationMenuCommands : public TCommands<FNaniteVisualizationMenuCommands>
{
public:
	enum class FNaniteVisualizationType : uint8
	{
		Overview,
		Standard,
		Advanced,
	};

	struct FNaniteVisualizationRecord
	{
		FName Name;
		TSharedPtr<FUICommandInfo> Command;
		FNaniteVisualizationType Type;

		FNaniteVisualizationRecord()
		: Name()
		, Command()
		, Type(FNaniteVisualizationType::Overview)
		{
		}
	};

	typedef TMultiMap<FName, FNaniteVisualizationRecord> TNaniteVisualizationModeCommandMap;
	typedef TNaniteVisualizationModeCommandMap::TConstIterator TCommandConstIterator;

	UE_API FNaniteVisualizationMenuCommands();

	UE_API TCommandConstIterator CreateCommandConstIterator() const;

	static UE_API void BuildVisualisationSubMenu(FMenuBuilder& Menu);

	UE_API virtual void RegisterCommands() override;

	UE_API void BindCommands(FUICommandList& CommandList, const TSharedPtr<FEditorViewportClient>& Client) const;
	
	inline bool IsPopulated() const
	{
		return CommandMap.Num() > 0;
	}

private:
	void BuildCommandMap();
	bool AddCommandTypeToMenu(FMenuBuilder& Menu, const FNaniteVisualizationType Type) const;

	static void ChangeNaniteVisualizationMode(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);
	static bool IsNaniteVisualizationModeSelected(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);

private:
	TNaniteVisualizationModeCommandMap CommandMap;
};

#undef UE_API