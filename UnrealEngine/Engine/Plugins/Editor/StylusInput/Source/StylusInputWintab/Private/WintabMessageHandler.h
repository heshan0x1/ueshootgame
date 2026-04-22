// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <StylusInput.h>
#include <Windows/WindowsApplication.h>

#include "WintabStats.h"

#define CHECK_MESSAGE_ORDER_ASSUMPTIONS NDEBUG

namespace UE::StylusInput::Wintab
{
	struct FPacketProperty;
	class FTabletContext;
	class FStylusInfo;
	struct FWindowContext;
	class FWintabAPI;

	DECLARE_DELEGATE_RetVal(const FWindowContext&, FGetWindowContextCallback);
	DECLARE_DELEGATE_RetVal_OneParam(const FTabletContext*, FGetTabletContextCallback, uint32);
	DECLARE_DELEGATE_RetVal_TwoParams(uint32, FGetStylusIDCallback, uint32, UINT);
	DECLARE_DELEGATE(FUpdateWindowRectCallback);
	DECLARE_DELEGATE(FUpdateTabletContextsCallback);

	class FWintabMessageHandler : public IWindowsMessageHandler
	{
	public:
		FWintabMessageHandler(IStylusInputInstance* Instance, FGetTabletContextCallback&& GetTabletContextCallback, FGetStylusIDCallback&& GetStylusIDCallback,
		                      FUpdateWindowRectCallback&& UpdateWindowRectCallback, FUpdateTabletContextsCallback&& UpdateTabletContextsCallback);
		virtual ~FWintabMessageHandler();

		virtual bool ProcessMessage(HWND Hwnd, uint32 Msg, WPARAM WParam, LPARAM LParam, int32& OutResult) override;

		bool AddEventHandler(IStylusInputEventHandler* EventHandler);
		bool RemoveEventHandler(IStylusInputEventHandler* EventHandler);

		uint32 GetCurrentStylusID() const { return CurrentStylusID; }
		const FPacketStats& GetPacketStats() const { return PacketStats; }

	private:
		void DebugEvent(const FString& Message) const;

		void ProcessCursorChange(uint32 TabletContextID, const void* WintabPacketPtr);
		FStylusInputPacket ProcessPacket(uint32 TabletContextID, const void* WintabPacketPtr);

		IStylusInputInstance *const Instance;
		const FWintabAPI& WintabAPI;
		FGetTabletContextCallback GetTabletContextCallback;
		FGetStylusIDCallback GetStylusIDCallback;
		FUpdateWindowRectCallback UpdateWindowRectCallback;
		FUpdateTabletContextsCallback UpdateTabletContextsCallback;
		TArray<IStylusInputEventHandler*> EventHandlers;
		FPacketStats PacketStats;

		uint32 CurrentStylusID = 0;

		bool bCursorChange = false;
		bool bLastPacketOnDigitizer = false;

#if CHECK_MESSAGE_ORDER_ASSUMPTIONS
		uint32 CursorChangeTabletContextID = 0;
		uint32 CursorChangeSerialNumber = 0;
#endif
	};
}
