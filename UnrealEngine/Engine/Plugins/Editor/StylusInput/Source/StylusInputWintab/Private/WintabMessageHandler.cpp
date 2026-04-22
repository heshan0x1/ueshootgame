// Copyright Epic Games, Inc. All Rights Reserved.

#include "WintabMessageHandler.h"

#include <StylusInputPacket.h>
#include <StylusInputUtils.h>

#include "WintabAPI.h"
#include "WintabTabletContext.h"

#define LOG_PREAMBLE "WintabMessageHandler"

#define ENABLE_DEBUG_EVENTS_FOR_PACKET_MESSAGES 0
#define ENABLE_DEBUG_EVENTS_FOR_CONTEXT_OPEN_CLOSE_UPDATE_MESSAGES 0
#define ENABLE_DEBUG_EVENTS_FOR_CONTEXT_OVERLAP_MESSAGES 0
#define ENABLE_DEBUG_EVENTS_FOR_PROXIMITY_CSRCHANGE_MESSAGES 0
#define ENABLE_DEBUG_EVENTS_FOR_INFOCHANGE_MESSAGES 0
#define ENABLE_DEBUG_EVENTS_FOR_PACKETEXT_MESSAGES 0
#define ENABLE_DEBUG_EVENTS_FOR_INVALID_PACKETS 0

using namespace UE::StylusInput::Private;

namespace UE::StylusInput::Wintab
{
FWintabMessageHandler::FWintabMessageHandler(IStylusInputInstance* Instance, FGetTabletContextCallback&& GetTabletContextCallback,
                                             FGetStylusIDCallback&& GetStylusIDCallback, FUpdateWindowRectCallback&& UpdateWindowRectCallback,
                                             FUpdateTabletContextsCallback&& UpdateTabletContextsCallback)
	: Instance(Instance)
	, WintabAPI(FWintabAPI::GetInstance())
	, GetTabletContextCallback(MoveTemp(GetTabletContextCallback))
	, GetStylusIDCallback(MoveTemp(GetStylusIDCallback))
	, UpdateWindowRectCallback(MoveTemp(UpdateWindowRectCallback))
	, UpdateTabletContextsCallback(MoveTemp(UpdateTabletContextsCallback))
{
	check(this->GetTabletContextCallback.IsBound())
	check(this->GetStylusIDCallback.IsBound())
	check(this->UpdateWindowRectCallback.IsBound())
	check(this->UpdateTabletContextsCallback.IsBound())
}

FWintabMessageHandler::~FWintabMessageHandler()
{
}

bool FWintabMessageHandler::ProcessMessage(const HWND Hwnd, const uint32 Msg, const WPARAM WParam, const LPARAM LParam, int32& OutResult)
{
	bool bHandled = false;

	switch (Msg)
	{
	case WT_PACKET:
		{
#if ENABLE_DEBUG_EVENTS_FOR_PACKET_MESSAGES
			DebugEvent(FString::Format(TEXT("Received WT_PACKET(SerialNumber={0}, Context={1}) message."), {static_cast<uint32>(WParam), static_cast<uint32>(LParam)}));
#endif

			if (WParam != 0)
			{
				PACKET WintabPacket = {};
				if (WintabAPI.WtPacket(reinterpret_cast<HCTX>(LParam), WParam, &WintabPacket))
				{
					if (bCursorChange)
					{
						ProcessCursorChange(static_cast<uint32>(LParam), &WintabPacket);
					}

					// Todo Process more than one packet? 

					PacketStats.NewPacket(WintabPacket.pkSerialNumber);

					const FStylusInputPacket Packet = ProcessPacket(static_cast<uint32>(LParam), &WintabPacket);

					if (Packet.Type != EPacketType::Invalid)
					{
						for (IStylusInputEventHandler* EventHandler : EventHandlers)
						{
							EventHandler->OnPacket(Packet, Instance);
						}
					}
				}
				else
				{
					PacketStats.InvalidPacket();
#if ENABLE_DEBUG_EVENTS_FOR_INVALID_PACKETS
					DebugEvent(FString::Format(TEXT("Failed to retrieve Wintab packet number {0}."), {WParam}));
#endif
				}
			}

			bHandled = true;
			break;
		}
	case WT_CTXOPEN:
		{
#if ENABLE_DEBUG_EVENTS_FOR_CONTEXT_OPEN_CLOSE_UPDATE_MESSAGES
			DebugEvent(FString::Format(TEXT("Received WT_CTXOPEN message (Context={0}, Status={1})."), {WParam, LParam}));
#endif

			bHandled = true;
			break;
		}
	case WT_CTXCLOSE:
		{
#if ENABLE_DEBUG_EVENTS_FOR_CONTEXT_OPEN_CLOSE_UPDATE_MESSAGES
			DebugEvent(FString::Format(TEXT("Received WT_CTXCLOSE message (Context={0}, Status={1})."), {WParam, LParam}));
#endif

			bHandled = true;
			break;
		}
	case WT_CTXUPDATE:
		{
#if ENABLE_DEBUG_EVENTS_FOR_CONTEXT_OPEN_CLOSE_UPDATE_MESSAGES
			DebugEvent(FString::Format(TEXT("Received WT_CTXUPDATE message (Context={0}, Status={1})."), {WParam, LParam}));
#endif

			bHandled = true;
			break;
		}
	case WT_CTXOVERLAP:
		{
#if ENABLE_DEBUG_EVENTS_FOR_CONTEXT_OVERLAP_MESSAGES
			DebugEvent(FString::Format(TEXT("Received WT_CTXOVERLAP message (Context={0}, Status={1})."), {WParam, LParam}));
#endif

			bHandled = true;
			break;
		}
	case WT_PROXIMITY:
		{
#if ENABLE_DEBUG_EVENTS_FOR_PROXIMITY_CSRCHANGE_MESSAGES
			DebugEvent(FString::Format(TEXT("Received WT_PROXIMITY message (Context={0}, ContextEnter={1}, ProximityEnter={2})."), {
											   static_cast<uint32>(WParam), LOWORD(LParam), HIWORD(LParam)
										   }));
#endif

			if (LOWORD(LParam) || HIWORD(LParam))
			{
				// While the Wintab documentation is somewhat confusing about what is encoded in LParam, it appears that the high-order word is effectively
				// indicating that the event is triggered by hardware proximity, and the low-order word is triggered both with and without hardware proximity
				// support. Just in case, we trigger a cursor change if either of the flags are set.

				bCursorChange = true;

#if CHECK_MESSAGE_ORDER_ASSUMPTIONS
				CursorChangeTabletContextID = static_cast<uint32>(WParam);
#endif
			}

			bHandled = true;
			break;
		}
	case WT_INFOCHANGE:
		{
#if ENABLE_DEBUG_EVENTS_FOR_INFOCHANGE_MESSAGES
			DebugEvent(FString::Format(TEXT("Received WT_INFOCHANGE(Context={0}, Category={1}, Index={2}) message."), {WParam, LOWORD(LParam), HIWORD(LParam)}));
#endif

			WintabAPI.UpdateNumberOfDevices();

			UpdateTabletContextsCallback.Execute();

			bHandled = true;
			break;
		}
	case WT_CSRCHANGE:
		{
#if ENABLE_DEBUG_EVENTS_FOR_PROXIMITY_CSRCHANGE_MESSAGES
			DebugEvent(FString::Format(TEXT("Received WT_CSRCHANGE message (SerialNumber={0}, Context={1})."), {static_cast<uint32>(WParam), static_cast<uint32>(LParam)}));
#endif

			bCursorChange = true;

#if CHECK_MESSAGE_ORDER_ASSUMPTIONS
			CursorChangeTabletContextID = static_cast<uint32>(LParam);
			CursorChangeSerialNumber = static_cast<uint32>(WParam);
#endif

			bHandled = true;
			break;
		}
	case WT_PACKETEXT:
		{
#if ENABLE_DEBUG_EVENTS_FOR_PACKETEXT_MESSAGES
			DebugEvent("Received WT_PACKETEXT message.");
#endif

			bHandled = true;
			break;
		}
	case WM_WINDOWPOSCHANGED:
		{
			UpdateWindowRectCallback.Execute();
			break;
		}
	default:
		;
	}

	return bHandled;
}

bool FWintabMessageHandler::AddEventHandler(IStylusInputEventHandler* EventHandler)
{
	check(EventHandler);

	if (EventHandlers.Contains(EventHandler))
	{
		LogWarning(LOG_PREAMBLE, FString::Format(TEXT("Event handler '{0}' already exists."), {EventHandler->GetName()}));
		return false;
	}

	EventHandlers.Add(EventHandler);

	LogVerbose(LOG_PREAMBLE, FString::Format(TEXT("Event handler '{0}' was added."), {EventHandler->GetName()}));

	return true;
}

bool FWintabMessageHandler::RemoveEventHandler(IStylusInputEventHandler* EventHandler)
{
	check(EventHandler);

	const bool bWasRemoved = EventHandlers.Remove(EventHandler) > 0;

	if (bWasRemoved)
	{
		LogVerbose(LOG_PREAMBLE, FString::Format(TEXT("Event handler '{0}' was removed."), {EventHandler->GetName()}));
	}

	return bWasRemoved;
}

void FWintabMessageHandler::DebugEvent(const FString& Message) const
{
	for (IStylusInputEventHandler* EventHandler : EventHandlers)
	{
		EventHandler->OnDebugEvent(Message, Instance);
	}
}

void FWintabMessageHandler::ProcessCursorChange(const uint32 TabletContextID, const void *const WintabPacketPtr)
{
	const PACKET& WintabPacket = *static_cast<const PACKET*>(WintabPacketPtr);

	check(bCursorChange);

#if CHECK_MESSAGE_ORDER_ASSUMPTIONS
	if (CursorChangeTabletContextID != TabletContextID || !(CursorChangeSerialNumber == 0 || CursorChangeSerialNumber == WintabPacket.pkSerialNumber))
	{
		DebugEvent("Cursor change assumptions violated: The next packet is not matching the previous WT_PROXIMITY/WT_CSRCHANGE messages.");
	}
#endif

	CurrentStylusID = GetStylusIDCallback.Execute(TabletContextID, WintabPacket.pkCursor);

	bCursorChange = false;

#if CHECK_MESSAGE_ORDER_ASSUMPTIONS
	CursorChangeTabletContextID = 0;
	CursorChangeSerialNumber = 0;
#endif
}

FStylusInputPacket FWintabMessageHandler::ProcessPacket(const uint32 TabletContextID, const void *const WintabPacketPtr)
{
	FStylusInputPacket Packet{TabletContextID};

	if (const FTabletContext* TabletContext = GetTabletContextCallback.Execute(TabletContextID))
	{
		const PACKET& WintabPacket = *static_cast<const PACKET*>(WintabPacketPtr);

		const bool bPacketOnDigitizer = !static_cast<bool>(TabletContext->SupportedProperties & ETabletSupportedProperties::Z) || WintabPacket.pkZ == 0;
		Packet.Type = bPacketOnDigitizer
						  ? bLastPacketOnDigitizer ? EPacketType::OnDigitizer : EPacketType::StylusDown
						  : bLastPacketOnDigitizer ? EPacketType::StylusUp : EPacketType::AboveDigitizer;
		bLastPacketOnDigitizer = bPacketOnDigitizer;

		for (int32 Index = 0, Num = TabletContext->NumPacketProperties; Index < Num; ++Index)
		{
			const FPacketPropertyHandler& PropertyHandler = TabletContext->PacketPropertyHandlers[Index];
			PropertyHandler.SetProperty(Packet, WintabPacket, PropertyHandler.SetPropertyData);
		}
	}

	return Packet;
}
}
