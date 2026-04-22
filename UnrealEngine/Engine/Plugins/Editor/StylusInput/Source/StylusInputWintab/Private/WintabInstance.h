// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <StylusInput.h>
#include <Containers/Array.h>
#include <Microsoft/MinimalWindowsApi.h>

#include "WintabMessageHandler.h"
#include "WintabTabletContext.h"
#include "WintabStylus.h"

namespace UE::StylusInput
{
	class IStylusInputEventHandler;
}

namespace UE::StylusInput::Wintab
{
	class FWintabAPI;

	class FWintabInstance : public IStylusInputInstance
	{
	public:
		explicit FWintabInstance(uint32 ID, HWND OSWindowHandle);
		virtual ~FWintabInstance() override;

		virtual bool AddEventHandler(IStylusInputEventHandler* EventHandler, EEventHandlerThread Thread) override;
		virtual bool RemoveEventHandler(IStylusInputEventHandler* EventHandler) override;

		virtual const TSharedPtr<IStylusInputTabletContext> GetTabletContext(uint32 TabletContextID) override;
		virtual const TSharedPtr<IStylusInputStylusInfo> GetStylusInfo(uint32 StylusID) override;

		virtual float GetPacketsPerSecond(EEventHandlerThread EventHandlerThread) const override;

		virtual FName GetInterfaceName() override;
		virtual FText GetName() override;

		virtual bool WasInitializedSuccessfully() const override;

	private:
		const FTabletContext* GetTabletContextInternal(uint32 TabletContextID) const;
		uint32 GetStylusID(uint32 TabletContextID, uint32 CursorIndex);

		void ClearTabletContexts();
		void UpdateTabletContexts();
		void UpdateWindowRect();

		const uint32 ID;
		const FWintabAPI& WintabAPI;
		const HWND OSWindowHandle;
		RECT WindowRect = {};

		FTabletContextContainer TabletContexts;
		FStylusInfoContainer StylusInfos;

		FWintabMessageHandler MessageHandler;
		TArray<TTuple<uint64, uint32>> CursorIDToStylusIDMappings;
	};
}
