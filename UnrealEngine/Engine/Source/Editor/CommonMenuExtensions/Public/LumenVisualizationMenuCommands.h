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

UE_DECLARE_TCOMMANDS(class FLumenVisualizationMenuCommands, UE_API)

class FLumenVisualizationMenuCommands : public TCommands<FLumenVisualizationMenuCommands>
{
public:
	enum class FLumenVisualizationType : uint8
	{
		Overview,
		Standard
	};

	struct FLumenVisualizationRecord
	{
		FName Name;
		TSharedPtr<FUICommandInfo> Command;
		FLumenVisualizationType Type;

		FLumenVisualizationRecord()
		: Name()
		, Command()
		, Type(FLumenVisualizationType::Overview)
		{
		}
	};

	typedef TMultiMap<FName, FLumenVisualizationRecord> TLumenVisualizationModeCommandMap;
	typedef TLumenVisualizationModeCommandMap::TConstIterator TCommandConstIterator;

	UE_API FLumenVisualizationMenuCommands();

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
	bool AddCommandTypeToMenu(FMenuBuilder& Menu, const FLumenVisualizationType Type) const;

	static void ChangeLumenVisualizationMode(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);
	static bool IsLumenVisualizationModeSelected(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);

private:
	TLumenVisualizationModeCommandMap CommandMap;
};

#undef UE_API