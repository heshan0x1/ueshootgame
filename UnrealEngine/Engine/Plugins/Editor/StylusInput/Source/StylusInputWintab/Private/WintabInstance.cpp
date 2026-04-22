// Copyright Epic Games, Inc. All Rights Reserved.

#include "WintabInstance.h"

#include <StylusInput.h>
#include <StylusInputUtils.h>
#include <Framework/Application/SlateApplication.h>

#include "WintabAPI.h"

#define LOCTEXT_NAMESPACE "WintabInstance"
#define LOG_PREAMBLE "WintabInstance"

using namespace UE::StylusInput::Private;

namespace UE::StylusInput::Wintab
{
	bool SetupWintabContext(const FWintabAPI& WintabAPI, UINT WintabContextIndex, LOGCONTEXT& WintabContext)
	{
		bool bSuccess = true;

		WintabContext.lcPktData = PACKETDATA;
		WintabContext.lcPktMode = PACKETMODE;
		WintabContext.lcOptions |= CXO_CSRMESSAGES | CXO_MESSAGES | CXO_SYSTEM;

		AXIS AxisX = {};
		AXIS AxisY = {};
		AXIS AxisZ = {};

		bSuccess &= WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_X, &AxisX) == sizeof(AxisX);
		bSuccess &= WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_Y, &AxisY) == sizeof(AxisY);
		bSuccess &= WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_Z, &AxisZ) == sizeof(AxisZ);

		if (!bSuccess)
		{
			LogError(LOG_PREAMBLE, FString::Format(TEXT("Failed to query device properties for Wintab context with index {0}."), {WintabContextIndex}));
			return false;
		}

		WintabContext.lcOutExtX = AxisX.axMax - AxisX.axMin + 1;
		WintabContext.lcOutExtY = AxisY.axMax - AxisY.axMin + 1;
		WintabContext.lcOutExtZ = AxisZ.axMax - AxisZ.axMin + 1;

		// In Wintab, the tablet origin is lower left. Move origin to upper left so that it corresponds to screen origin.
		WintabContext.lcOutExtY = -WintabContext.lcOutExtY;

		return bSuccess;
	}

	bool SetupTabletContextMetadata(const FWintabAPI& WintabAPI, UINT WintabContextIndex, HCTX WintabContextHandle,
	                                const LOGCONTEXT& WintabContext, FTabletContext& TabletContext)
	{
		bool bSuccess = true;

		TabletContext.WintabContextIndex = WintabContextIndex;
		TabletContext.WintabContextHandle = WintabContextHandle;

		TabletContext.InputRectangle = {
			WintabContext.lcOutOrgX, WintabContext.lcOutOrgY,
			WintabContext.lcOutOrgX + WintabContext.lcOutExtX, WintabContext.lcOutOrgY - WintabContext.lcOutExtY
		};

		FWintabInfoOutputBuffer OutputBuffer;

		TabletContext.Name = WintabContext.lcName;
		if (TabletContext.Name.IsEmpty())
		{
			TCHAR *const OutputBufferPtr = OutputBuffer.Allocate(WTI_DEVICES + WintabContextIndex, DVC_NAME);
			if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_NAME, OutputBufferPtr) <= OutputBuffer.SizeInBytes())
			{
				TabletContext.Name = OutputBufferPtr;
			}
			else
			{
				LogWarning(LOG_PREAMBLE, FString::Format(TEXT("Failed to query device name for Wintab context with index {0}."), {WintabContextIndex}));
			}
		}

		TCHAR *const OutputBufferPtr = OutputBuffer.Allocate(WTI_DEVICES + WintabContextIndex, DVC_PNPID);
		if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_PNPID, OutputBufferPtr) <= OutputBuffer.SizeInBytes())
		{
			TabletContext.PlugAndPlayID = OutputBufferPtr;
		}
		else
		{
			TabletContext.PlugAndPlayID.Empty();
			LogWarning(LOG_PREAMBLE, FString::Format(TEXT("Failed to query Plug and Play ID for Wintab context with index {0}."), {WintabContextIndex}));
		}

		UINT HardwareCapabilities;
		if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_HARDWARE, &HardwareCapabilities) != sizeof(HardwareCapabilities))
		{
			LogError(LOG_PREAMBLE, FString::Format(TEXT("Failed to query hardware capabilities for Wintab context with index {0}."), {WintabContextIndex}));
			return false;
		}

		TabletContext.HardwareCapabilities =
			(HardwareCapabilities & HWC_INTEGRATED ? ETabletHardwareCapabilities::Integrated : ETabletHardwareCapabilities::None)
			| (HardwareCapabilities & HWC_TOUCH ? ETabletHardwareCapabilities::CursorMustTouch : ETabletHardwareCapabilities::None)
			| (HardwareCapabilities & HWC_HARDPROX ? ETabletHardwareCapabilities::HardProximity : ETabletHardwareCapabilities::None)
			| (HardwareCapabilities & HWC_PHYSID_CURSORS ? ETabletHardwareCapabilities::CursorsHavePhysicalIds : ETabletHardwareCapabilities::None);

		/* CURSORS */

		UINT FirstCursor, NumCursors;
		if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_FIRSTCSR, &FirstCursor) == sizeof(FirstCursor)
			&& WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_NCSRTYPES, &NumCursors) == sizeof(NumCursors))
		{
			TabletContext.WintabFirstCursor = FirstCursor;
			TabletContext.WintabNumCursors = NumCursors;
		}
		else
		{
			LogError(LOG_PREAMBLE, FString::Format(TEXT("Failed to query cursor range for Wintab context with index {0}."), {WintabContextIndex}));
			return false;
		}

		return bSuccess;
	}

	void SetPropertyStatus(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		// Todo incorporate if tablet context is actually able to support Z values (or proximity?); if not, probably any packet is touching the tablet 
		Packet.PenStatus = (WintabPacket.pkStatus & TPS_INVERT ? EPenStatus::CursorIsInverted : EPenStatus::None)
			| (WintabPacket.pkZ <= 0 ? EPenStatus::CursorIsTouching : EPenStatus::None);
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyStatus)>);

	void SetPropertyTime(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		Packet.TimerTick = WintabPacket.pkTime;
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyTime)>);

	void SetPropertySerialNumber(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		Packet.SerialNumber = WintabPacket.pkSerialNumber;
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertySerialNumber)>);

	void SetPropertyCursor(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		const FWintabMessageHandler* MessageHandler = *reinterpret_cast<FWintabMessageHandler *const *>(Data);
		Packet.CursorID = MessageHandler->GetCurrentStylusID();
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyCursor)>);

	void AssignSetPropertyCursorData(int8 *const Data, const FWintabMessageHandler* MessageHandler)
	{
		*reinterpret_cast<const FWintabMessageHandler**>(Data) = MessageHandler;
	}

	void SetPropertyButtons(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		// Todo 
		/*const WORD ButtonNumber = LOWORD(WintabPacket.pkButtons);
		const WORD ButtonState = HIWORD(WintabPacket.pkButtons);*/
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyButtons)>);

	void SetPropertyX(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		const int32& TabletContextInputWidth = reinterpret_cast<const int*>(Data)[0];
		const int32& VirtualScreenWidth = reinterpret_cast<const int*>(Data)[1];

		const RECT *const * WindowRect = reinterpret_cast<const RECT *const *>(Data + sizeof(int32) * 2);
		const int32& WindowOffsetX = (*WindowRect)->left;

		Packet.X = static_cast<float>(WintabPacket.pkX) * VirtualScreenWidth / TabletContextInputWidth - WindowOffsetX;
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyX)>);

	void AssignSetPropertyXData(int8 *const Data, const int32 TabletContextInputWidth, const int32 VirtualScreenWidth, const RECT* WindowRect)
	{
		int32* DataInt = reinterpret_cast<int32*>(Data);
		*DataInt++ = TabletContextInputWidth;
		*DataInt++ = VirtualScreenWidth;

		const RECT** DataRect = reinterpret_cast<const RECT**>(DataInt);
		*DataRect = WindowRect;
	}

	void SetPropertyY(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		const int32& TabletContextInputHeight = reinterpret_cast<const int*>(Data)[0];
		const int32& VirtualScreenHeight = reinterpret_cast<const int*>(Data)[1];

		const RECT *const * WindowRect = reinterpret_cast<const RECT *const *>(Data + sizeof(int32) * 2);
		const int32& WindowOffsetY = (*WindowRect)->top;

		Packet.Y = static_cast<float>(WintabPacket.pkY) * VirtualScreenHeight / TabletContextInputHeight - WindowOffsetY;
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyY)>);

	void AssignSetPropertyYData(int8 *const Data, const int32 TabletContextInputHeight, const int32 VirtualScreenHeight, const RECT* WindowRect)
	{
		int32* DataInt = reinterpret_cast<int32*>(Data);
		*DataInt++ = TabletContextInputHeight;
		*DataInt++ = VirtualScreenHeight;

		const RECT** DataRect = reinterpret_cast<const RECT**>(DataInt);
		*DataRect = WindowRect;
	}

	void SetPropertyZ(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		const float& InvResolution = *reinterpret_cast<const float*>(Data);

		Packet.Z = static_cast<float>(WintabPacket.pkZ) * InvResolution;
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyZ)>);

	void AssignSetPropertyZData(int8 *const Data, const float Resolution)
	{
		*reinterpret_cast<float*>(Data) = 1.0f / Resolution;
	}

	void SetPropertyNormalPressure(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		const float& InvExtent = *reinterpret_cast<const float*>(Data);

		Packet.NormalPressure = static_cast<float>(WintabPacket.pkNormalPressure) * InvExtent;
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyNormalPressure)>);

	void AssignSetPropertyNormalPressureData(int8 *const Data, const int32 Extent)
	{
		*reinterpret_cast<float*>(Data) = 1.0f / Extent;
	}

	void SetPropertyTangentPressure(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		const float& InvExtent = *reinterpret_cast<const float*>(Data);

		Packet.TangentPressure = static_cast<float>(WintabPacket.pkTangentPressure) * InvExtent;
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyTangentPressure)>);

	void AssignSetPropertyTangentPressureData(int8 *const Data, const int32 Extent)
	{
		*reinterpret_cast<float*>(Data) = 1.0f / Extent;
	}

	void SetPropertyOrientation(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		const float& InvScaleAzimuth = reinterpret_cast<const float*>(Data)[0];
		const float& InvScaleAltitude = reinterpret_cast<const float*>(Data)[1];
		const float& InvScaleTwist = reinterpret_cast<const float*>(Data)[2];

		Packet.AzimuthOrientation = WintabPacket.pkOrientation.orAzimuth * InvScaleAzimuth;
		Packet.AltitudeOrientation = WintabPacket.pkOrientation.orAltitude * InvScaleAltitude;
		Packet.TwistOrientation = WintabPacket.pkOrientation.orTwist * InvScaleTwist;

		const float AzimuthRadians = FMath::DegreesToRadians(Packet.AzimuthOrientation);
		const float AltitudeRadians = FMath::DegreesToRadians(Packet.AltitudeOrientation);

		const float X = FMath::Sin(AzimuthRadians) * FMath::Cos(AltitudeRadians);
		const float Y = FMath::Cos(AzimuthRadians) * FMath::Cos(AltitudeRadians);

		Packet.XTiltOrientation = FMath::RadiansToDegrees(FMath::Asin(-X));
		Packet.YTiltOrientation = FMath::RadiansToDegrees(FMath::Asin(-Y));
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyOrientation)>);

	void AssignSetPropertyOrientationData(int8 *const Data, const float AzimuthResolution, const float AltitudeResolution, const float TwistResolution)
	{
		float* DataFloat = reinterpret_cast<float*>(Data);
		*DataFloat++ = 1.0f / AzimuthResolution * 360.0f;
		*DataFloat++ = 1.0f / AltitudeResolution * 360.0f;
		*DataFloat = 1.0f / TwistResolution * 360.0f;
	}

	void SetPropertyRotation(FStylusInputPacket& Packet, const PACKET& WintabPacket, const int8* Data)
	{
		const float& InvScalePitch = reinterpret_cast<const float*>(Data)[0];
		const float& InvScaleRoll = reinterpret_cast<const float*>(Data)[1];
		const float& InvScaleYaw = reinterpret_cast<const float*>(Data)[2];

		Packet.PitchRotation = WintabPacket.pkRotation.roPitch * InvScalePitch;
		Packet.RollRotation = WintabPacket.pkRotation.roRoll * InvScaleRoll;
		Packet.YawRotation = WintabPacket.pkRotation.roYaw * InvScaleYaw;
	}
	static_assert(std::is_same_v<FPacketPropertyHandler::FFuncSetProperty, decltype(SetPropertyRotation)>);

	void AssignSetPropertyRotationData(int8 *const Data, const float PitchResolution, const float RollResolution, const float YawResolution)
	{
		float* DataFloat = reinterpret_cast<float*>(Data);
		*DataFloat++ = 1.0f / PitchResolution * 360.0f;
		*DataFloat++ = 1.0f / RollResolution * 360.0f;
		*DataFloat = 1.0f / YawResolution * 360.0f;
	}

	bool SetupTabletContextPacketDescriptionData(const FWintabAPI& WintabAPI, UINT WintabContextIndex, const LOGCONTEXT& WintabContext,
	                                             const RECT& WindowRect, FTabletContext& TabletContext, const FWintabMessageHandler& MessageHandler)
	{
		bool bSuccess = true;

		TabletContext.SupportedProperties = ETabletSupportedProperties::None;
		TabletContext.NumPacketProperties = 0;

		WTPKT DataAvailableForAllCursors;
		WTPKT DataAvailableForSomeCursors;
		bSuccess &= WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_PKTDATA, &DataAvailableForAllCursors) == sizeof(DataAvailableForAllCursors);
		bSuccess &= WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_CSRDATA, &DataAvailableForSomeCursors) == sizeof(DataAvailableForSomeCursors);

		if (!bSuccess)
		{
			LogError(LOG_PREAMBLE, FString::Format(TEXT("Failed to query available packet data for Wintab context with index {0}."), {WintabContextIndex}));
			return false;
		}

		const WTPKT DataAvailable = DataAvailableForAllCursors | DataAvailableForSomeCursors;

		auto AddProperty = [&TabletContext](EPacketPropertyType Type, ETabletPropertyMetricUnit MetricUnit, int32 Minimum, int32 Maximum,
		                                    float Resolution) -> FPacketPropertyHandler&
		{
			FPacketProperty& Property = TabletContext.PacketProperties[TabletContext.NumPacketProperties];
			Property.Type = Type;
			Property.MetricUnit = MetricUnit;
			Property.Minimum = Minimum;
			Property.Maximum = Maximum;
			Property.Resolution = Resolution;

			FPacketPropertyHandler& PropertyHandler = TabletContext.PacketPropertyHandlers[TabletContext.NumPacketProperties];

			++TabletContext.NumPacketProperties;

			return PropertyHandler;
		};

		if (DataAvailable & PK_STATUS)
		{
			TabletContext.SupportedProperties = ETabletSupportedProperties::PacketStatus;
			FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::Status, ETabletPropertyMetricUnit::Default, 0, 0, 0.0f);
			PropertyHandler.SetProperty = &SetPropertyStatus;
		}

		if (DataAvailable & PK_TIME)
		{
			TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::TimerTick;
			FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::Time, ETabletPropertyMetricUnit::Default, 0, 0, 0.0f);
			PropertyHandler.SetProperty = &SetPropertyTime;
		}

		if (DataAvailable & PK_SERIAL_NUMBER)
		{
			TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::SerialNumber;
			FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::SerialNumber, ETabletPropertyMetricUnit::Default, 0, 0, 0.0f);
			PropertyHandler.SetProperty = &SetPropertySerialNumber;
		}

		if (DataAvailable & PK_CURSOR)
		{
			FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::Cursor, ETabletPropertyMetricUnit::Default, 0, 0, 0.0f);
			PropertyHandler.SetProperty = &SetPropertyCursor;
			AssignSetPropertyCursorData(PropertyHandler.SetPropertyData, &MessageHandler);
		}

		if (DataAvailable & PK_BUTTONS)
		{
			FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::Buttons, ETabletPropertyMetricUnit::Default, 0, 0, 0.0f);
			PropertyHandler.SetProperty = &SetPropertyButtons;
		}

		if (DataAvailable & PK_X)
		{
			AXIS Axis;
			if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_X, &Axis) == sizeof(Axis))
			{
				TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::X;
				FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::X, static_cast<ETabletPropertyMetricUnit>(Axis.axUnits), Axis.axMin,
				                                                      Axis.axMax, Fix32ToFloat(Axis.axResolution));
				PropertyHandler.SetProperty = &SetPropertyX;
				AssignSetPropertyXData(PropertyHandler.SetPropertyData, WintabContext.lcInExtX, WintabAPI.GetSystemMetrics(SM_CXVIRTUALSCREEN), &WindowRect);
			}
			else
			{
				bSuccess = false;
			}
		}

		if (DataAvailable & PK_Y)
		{
			AXIS Axis;
			if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_Y, &Axis) == sizeof(Axis))
			{
				TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::Y;
				FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::Y, static_cast<ETabletPropertyMetricUnit>(Axis.axUnits), Axis.axMin,
				                                                      Axis.axMax, Fix32ToFloat(Axis.axResolution));
				PropertyHandler.SetProperty = &SetPropertyY;
				AssignSetPropertyYData(PropertyHandler.SetPropertyData, WintabContext.lcInExtY, WintabAPI.GetSystemMetrics(SM_CYVIRTUALSCREEN), &WindowRect);
			}
			else
			{
				bSuccess = false;
			}
		}

		if (DataAvailable & PK_Z)
		{
			AXIS Axis;
			if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_Z, &Axis) == sizeof(Axis))
			{
				TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::Z;
				FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::Z, static_cast<ETabletPropertyMetricUnit>(Axis.axUnits), Axis.axMin,
				                                                      Axis.axMax, Fix32ToFloat(Axis.axResolution));

				const auto& Property = TabletContext.PacketProperties[TabletContext.NumPacketProperties - 1]; 
				const float Resolution = [&Property]
				{
					switch (Property.MetricUnit)
					{
					case ETabletPropertyMetricUnit::Inches:
						return Property.Resolution * 2.54f;
					case ETabletPropertyMetricUnit::Centimeters:
						return Property.Resolution;
					default:
						return 1.0f;
					}
				}();

				PropertyHandler.SetProperty = &SetPropertyZ;
				AssignSetPropertyZData(PropertyHandler.SetPropertyData, Resolution);
			}
			else
			{
				bSuccess = false;
			}
		}

		if (DataAvailable & PK_NORMAL_PRESSURE)
		{
			AXIS Axis;
			if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_NPRESSURE, &Axis) == sizeof(Axis))
			{
				TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::NormalPressure;
				FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::NormalPressure, static_cast<ETabletPropertyMetricUnit>(Axis.axUnits), Axis.axMin,
				                                        Axis.axMax, Fix32ToFloat(Axis.axResolution));
				PropertyHandler.SetProperty = &SetPropertyNormalPressure;
				AssignSetPropertyNormalPressureData(PropertyHandler.SetPropertyData, Axis.axMax - Axis.axMin + 1);
			}
			else
			{
				bSuccess = false;
			}
		}

		if (DataAvailable & PK_TANGENT_PRESSURE)
		{
			AXIS Axis;
			if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_TPRESSURE, &Axis) == sizeof(Axis))
			{
				TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::TangentPressure;
				FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::TangentPressure, static_cast<ETabletPropertyMetricUnit>(Axis.axUnits), Axis.axMin,
				                                        Axis.axMax, Fix32ToFloat(Axis.axResolution));
				PropertyHandler.SetProperty = &SetPropertyTangentPressure;
				AssignSetPropertyTangentPressureData(PropertyHandler.SetPropertyData, Axis.axMax - Axis.axMin + 1);
			}
			else
			{
				bSuccess = false;
			}
		}

		if (DataAvailable & PK_ORIENTATION)
		{
			AXIS Orientation[3];
			if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_ORIENTATION, &Orientation) == sizeof(Orientation))
			{
				if (Orientation[0].axResolution && Orientation[1].axResolution)
				{
					TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::AzimuthOrientation |
						ETabletSupportedProperties::AltitudeOrientation | ETabletSupportedProperties::TwistOrientation |
						ETabletSupportedProperties::XTiltOrientation | ETabletSupportedProperties::YTiltOrientation;

					if (Orientation[0].axUnits != TU_CIRCLE || Orientation[1].axUnits != TU_CIRCLE || Orientation[2].axUnits != TU_CIRCLE)
					{
						LogWarning(LOG_PREAMBLE, FString::Format(
							           TEXT("Units for orientation are not all TU_CIRCLE for Wintab context with index {0}."), {WintabContextIndex}));
					}

					FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::Orientation, static_cast<ETabletPropertyMetricUnit>(Orientation[0].axUnits), 0,
					                                        0, Fix32ToFloat(Orientation[0].axResolution));
					PropertyHandler.SetProperty = &SetPropertyOrientation;
					AssignSetPropertyOrientationData(PropertyHandler.SetPropertyData, Fix32ToFloat(Orientation[0].axResolution),
					                                 Fix32ToFloat(Orientation[1].axResolution),
					                                 Fix32ToFloat(Orientation[2].axResolution));
				}
			}
			else
			{
				bSuccess = false;
			}
		}

		if (DataAvailable & PK_ROTATION)
		{
			AXIS Rotation[3];
			if (WintabAPI.WtInfo(WTI_DEVICES + WintabContextIndex, DVC_ROTATION, &Rotation) == sizeof(Rotation))
			{
				TabletContext.SupportedProperties = TabletContext.SupportedProperties | ETabletSupportedProperties::PitchRotation |
					ETabletSupportedProperties::RollRotation | ETabletSupportedProperties::YawRotation;

				if (Rotation[0].axUnits != TU_CIRCLE || Rotation[1].axUnits != TU_CIRCLE || Rotation[2].axUnits != TU_CIRCLE)
				{
					LogWarning(LOG_PREAMBLE, FString::Format(
								 TEXT("Units for rotation are not all TU_CIRCLE for Wintab context with index {0}."), {WintabContextIndex}));
				}

				FPacketPropertyHandler& PropertyHandler = AddProperty(EPacketPropertyType::Rotation, static_cast<ETabletPropertyMetricUnit>(Rotation[0].axUnits), 0, 0,
				                                        Fix32ToFloat(Rotation[0].axResolution));
				PropertyHandler.SetProperty = &SetPropertyRotation;
				AssignSetPropertyRotationData(PropertyHandler.SetPropertyData, Fix32ToFloat(Rotation[0].axResolution), Fix32ToFloat(Rotation[1].axResolution),
				                              Fix32ToFloat(Rotation[2].axResolution));
			}
			else
			{
				bSuccess = false;
			}
		}

		if (!bSuccess)
		{
			LogError(LOG_PREAMBLE, FString::Format(TEXT("Failed to query packet property information for Wintab context with index {0}."), {WintabContextIndex}));
		}

		return bSuccess;
	}

	bool SetupTabletContext(const FWintabAPI& WintabAPI, UINT WintabContextIndex, HCTX WintabContextHandle, const LOGCONTEXT& WintabContext,
	                        const RECT& WindowRect, FTabletContext& TabletContext, const FWintabMessageHandler& MessageHandler)
	{
		bool bSuccess = true;

		bSuccess &= SetupTabletContextMetadata(WintabAPI, WintabContextIndex, WintabContextHandle, WintabContext, TabletContext);
		bSuccess &= SetupTabletContextPacketDescriptionData(WintabAPI, WintabContextIndex, WintabContext, WindowRect, TabletContext, MessageHandler);

		return bSuccess;
	}

	FWintabInstance::FWintabInstance(const uint32 ID, const HWND OSWindowHandle)
        : ID(ID)
		, WintabAPI(FWintabAPI::GetInstance())
		, OSWindowHandle(OSWindowHandle) 
		, MessageHandler(this,
		                 FGetTabletContextCallback::CreateRaw(this, &FWintabInstance::GetTabletContextInternal),
		                 FGetStylusIDCallback::CreateRaw(this, &FWintabInstance::GetStylusID),
		                 FUpdateWindowRectCallback::CreateRaw(this, &FWintabInstance::UpdateWindowRect),
						 FUpdateTabletContextsCallback::CreateRaw(this, &FWintabInstance::UpdateTabletContexts))
	{
		if (FSlateApplication::IsInitialized())
		{
			if (const TSharedPtr<GenericApplication> WindowsApplication = FSlateApplication::Get().GetPlatformApplication())
			{
				static_cast<FWindowsApplication*>(WindowsApplication.Get())->AddMessageHandler(MessageHandler);
			}
		}

		UpdateWindowRect();
	    UpdateTabletContexts();
    }

	FWintabInstance::~FWintabInstance()
	{
		ClearTabletContexts();

		if (FSlateApplication::IsInitialized())
		{
			if (const TSharedPtr<GenericApplication> WindowsApplication = FSlateApplication::Get().GetPlatformApplication())
			{
				static_cast<FWindowsApplication*>(WindowsApplication.Get())->RemoveMessageHandler(MessageHandler);
			}
		}

		const FPacketStats& PacketStats = MessageHandler.GetPacketStats();

		if (const uint32 NumInvalidPackets = PacketStats.GetNumInvalidPackets())
		{
			Log(LOG_PREAMBLE, FString::Format(
				    TEXT("Wintab instance '{0}' encountered {1} invalid packets."), {FWintabInstance::GetName().ToString(), NumInvalidPackets}));
		}

		if (const uint32 NumLostPackets = PacketStats.GetNumLostPackets())
		{
			Log(LOG_PREAMBLE, FString::Format(
				    TEXT("Wintab instance '{0}' encountered {1} lost packets."), {FWintabInstance::GetName().ToString(), NumLostPackets}));
		}
	}

	bool FWintabInstance::AddEventHandler(IStylusInputEventHandler* EventHandler, const EEventHandlerThread Thread)
	{
        if (!EventHandler)
        {
            LogWarning(LOG_PREAMBLE, "Tried to add nullptr as event handler.");
            return false;
        }

		if (Thread == EEventHandlerThread::OnGameThread)
		{
			return MessageHandler.AddEventHandler(EventHandler);
		}

		if (Thread == EEventHandlerThread::Asynchronous)
		{
			// TODO Handle asynchronous thread
			LogError(LOG_PREAMBLE, "Asynchronous event handler is not supported yet.");
			return false;
		}

        return false;
	}

	bool FWintabInstance::RemoveEventHandler(IStylusInputEventHandler* EventHandler)
	{
        if (!EventHandler)
        {
            LogWarning(LOG_PREAMBLE, "Tried to remove nullptr event handler.");
            return false;
        }

		if (MessageHandler.RemoveEventHandler(EventHandler))
		{
			return true;
		}

		// TODO Handle asynchronous thread

		return false;
	}

	const TSharedPtr<IStylusInputTabletContext> FWintabInstance::GetTabletContext(const uint32 TabletContextID)
	{
        return TabletContexts.Get(TabletContextID);
    }

	const TSharedPtr<IStylusInputStylusInfo> FWintabInstance::GetStylusInfo(const uint32 StylusID)
	{
		return StylusInfos.Get(StylusID);
	}

	float FWintabInstance::GetPacketsPerSecond(const EEventHandlerThread EventHandlerThread) const
	{
        return MessageHandler.GetPacketStats().GetPacketsPerSecond();
    }

	FName FWintabInstance::GetInterfaceName()
	{
		return FWintabAPI::GetName();
	}

	FText FWintabInstance::GetName()
	{
		return FText::Format(LOCTEXT("Wintab", "Wintab #{0}"), ID);
	}

	bool FWintabInstance::WasInitializedSuccessfully() const
	{
		return true;
	}

	const FTabletContext* FWintabInstance::GetTabletContextInternal(const uint32 TabletContextID) const
	{
		return TabletContexts.Get(TabletContextID).Get();
	}

	uint32 FWintabInstance::GetStylusID(const uint32 TabletContextID, uint32 CursorIndex)
	{
		DWORD CursorPhysicalID;
		UINT CursorType;
		if (WintabAPI.WtInfo(WTI_CURSORS + CursorIndex, CSR_PHYSID, &CursorPhysicalID) == sizeof(CursorPhysicalID) &&
			WintabAPI.WtInfo(WTI_CURSORS + CursorIndex, CSR_TYPE, &CursorType) == sizeof(CursorType))
		{
			const uint16 MaskedCursorId = MaskCursorId(CursorType);
			const uint16 MaskedCursorType = MaskCursorType(CursorType);

			const uint64 CursorID = static_cast<uint64>(MaskedCursorId) << 32 | CursorPhysicalID;
			const TTuple<uint64, uint32>* Mapping = CursorIDToStylusIDMappings.FindByPredicate(
				[CursorID](const TTuple<uint64, uint32>& Tuple)
				{
					return Tuple.Get<0>() == CursorID;
				});

			if (!Mapping)
			{
				uint32 StylusID = CursorPhysicalID;

				// Resolve any collisions of physical IDs, which are only guaranteed to be unique within a (masked) cursor type.
				while (StylusInfos.Contains(StylusID))
				{
					++StylusID;
				}

				Mapping = &CursorIDToStylusIDMappings.Emplace_GetRef(CursorID, StylusID);

				const TSharedRef<FStylusInfo> StylusInfo = StylusInfos.Add(StylusID);

				StylusInfo->WintabPhysicalID = CursorPhysicalID;
				StylusInfo->WintabCursorType = MaskedCursorType;

				const ECursorIndexType CursorIndexType = [&TabletContexts = TabletContexts, TabletContextID, CursorIndex]
				{
					if (const TSharedPtr<FTabletContext> TabletContextPtr = TabletContexts.Get(TabletContextID))
					{
						const FTabletContext& TabletContext = *TabletContextPtr;
						if (TabletContext.WintabFirstCursor <= CursorIndex && CursorIndex < static_cast<uint8>(TabletContext.WintabFirstCursor + TabletContext.WintabNumCursors))
						{
							const int8 CursorIndexTypeInt = CursorIndex - TabletContext.WintabFirstCursor;
							if (0 <= CursorIndexTypeInt && CursorIndexTypeInt < static_cast<int8>(ECursorIndexType::Num_Enumerators))
							{
								return static_cast<ECursorIndexType>(CursorIndexTypeInt);
							}
						}
					}
					return ECursorIndexType::Invalid_Enumerator;
				}();

				// If this is the inverted side of a pen, populate the stylus data with the non-inverted side instead.
				if (CursorIndexType != ECursorIndexType::Invalid_Enumerator && CursorIsInverted(CursorIndexType))
				{
					--CursorIndex;
				}

				FWintabInfoOutputBuffer OutputBuffer;
				TCHAR* OutputBufferPtr = OutputBuffer.Allocate(WTI_CURSORS + CursorIndex, CSR_NAME);
				if (WintabAPI.WtInfo(WTI_CURSORS + CursorIndex, CSR_NAME, OutputBufferPtr) <= OutputBuffer.SizeInBytes())
				{
					StylusInfo->Name = FString::Format(TEXT("{0} ({1})"), {OutputBufferPtr, MaskedCursorTypeToString(MaskedCursorType)});
				}
				else
				{
					LogWarning(LOG_PREAMBLE, FString::Format(TEXT("Failed to query name for Wintab cursor with index {0}."), {CursorIndex}));
				}

				BYTE NumButtons;
				if (WintabAPI.WtInfo(WTI_CURSORS + CursorIndex, CSR_BUTTONS, &NumButtons) == sizeof(NumButtons))
				{
					StylusInfo->Buttons.SetNum(NumButtons);

					OutputBufferPtr = OutputBuffer.Allocate(WTI_CURSORS + CursorIndex, CSR_BTNNAMES);
					if (WintabAPI.WtInfo(WTI_CURSORS + CursorIndex, CSR_BTNNAMES, OutputBufferPtr) <= OutputBuffer.SizeInBytes())
					{
						const TCHAR* Name = OutputBufferPtr;
						int32 ButtonIndex = 0;
						while (*Name)
						{
							StylusInfo->Buttons[ButtonIndex].Name = Name;
							Name += StylusInfo->Buttons[ButtonIndex].Name.Len() + 1;
							++ButtonIndex;
						}
					}
					else
					{
						LogWarning(LOG_PREAMBLE, FString::Format(TEXT("Failed to query button names for Wintab cursor with index {0}."), {CursorIndex}));
					}

					BYTE ButtonMap[32] = {};
					if (WintabAPI.WtInfo(WTI_CURSORS + CursorIndex, CSR_BUTTONMAP, &ButtonMap) == sizeof(ButtonMap))
					{
						for (int32 ButtonIndex = 0; ButtonIndex < NumButtons; ++ButtonIndex)
						{
							// Todo
						}
					}
					else
					{
						LogWarning(LOG_PREAMBLE, FString::Format(TEXT("Failed to query button map for Wintab cursor with index {0}."), {CursorIndex}));
					}
				}
				else
				{
					LogWarning(LOG_PREAMBLE, FString::Format(TEXT("Failed to query number of buttons for Wintab cursor with index {0}."), {CursorIndex}));
				}
			}

			// Make sure a StylusInfo with this ID exists, and the cursor type and physical ID match.
			checkSlow(Mapping && StylusInfos.Contains(Mapping->Get<1>()) && StylusInfos.Get(Mapping->Get<1>()).IsValid()
				&& StylusInfos.Get(Mapping->Get<1>())->WintabCursorType == MaskedCursorType
				&& StylusInfos.Get(Mapping->Get<1>())->WintabPhysicalID == CursorPhysicalID);

			return Mapping->Get<1>();
		}
		else
		{
			LogWarning(LOG_PREAMBLE, FString::Format(TEXT("Failed to query cursor metadata for cursor index {0}."), {CursorIndex}));
		}

		return 0;
	}

	void FWintabInstance::ClearTabletContexts()
	{
		for (int32 Index = 0, NumIndices = TabletContexts.Num(); Index < NumIndices; ++Index)
		{
			const HCTX WintabContextHandle = TabletContexts[Index]->WintabContextHandle;

			if (WintabAPI.WtClose(WintabContextHandle))
			{
				LogVerbose(LOG_PREAMBLE, FString::Format(TEXT("Closed Wintab context with handle {0}."),
														 {HctxToUint32(WintabContextHandle)}));
			}
			else
			{
				LogError(LOG_PREAMBLE, FString::Format(TEXT("Could not close Wintab context with handle {0}."),
													   {HctxToUint32(WintabContextHandle)}));
			}
		}

		TabletContexts.Clear();
	}

	void FWintabInstance::UpdateTabletContexts()
	{
		ClearTabletContexts();

		UINT WintabContextIndex = 0;
		LOGCONTEXTW WintabContext;

		while (WintabAPI.WtInfo(WTI_DDCTXS + WintabContextIndex, 0, &WintabContext) == sizeof(WintabContext))
		{
			if (!SetupWintabContext(WintabAPI, WintabContextIndex, WintabContext))
			{
				LogError(LOG_PREAMBLE, FString::Format(TEXT("Failed to setup Wintab context with index {0}}."), {WintabContextIndex}));
				++WintabContextIndex;
				continue;
			}

			const HCTX WintabContextHandle = WintabAPI.WtOpen(OSWindowHandle, &WintabContext, Windows::TRUE);
			if (!WintabContextHandle)
			{
				LogError(LOG_PREAMBLE, FString::Format(TEXT("Failed to open Wintab context with index {0}."), {WintabContextIndex}));
				++WintabContextIndex;
				continue;
			}

			const uint32 TabletContextID = HctxToUint32(WintabContextHandle);

			TSharedRef<FTabletContext> TabletContextPtr = [&TabletContexts = TabletContexts, TabletContextID]
			{
				if (TabletContexts.Contains(TabletContextID))
				{
					return TabletContexts.Get(TabletContextID).ToSharedRef();
				}
				return TabletContexts.Add(TabletContextID);
			}();

			FTabletContext& TabletContext = *TabletContextPtr;

			if (!SetupTabletContext(WintabAPI, WintabContextIndex, WintabContextHandle, WintabContext, WindowRect, TabletContext, MessageHandler))
			{
				LogError(LOG_PREAMBLE, FString::Format(TEXT("Failed setting up tablet context with ID {0}."), {TabletContextID}));

				TabletContexts.Remove(TabletContextID);

				if (!WintabAPI.WtClose(WintabContextHandle))
				{
					LogError(LOG_PREAMBLE, FString::Format(TEXT("Could not close Wintab context with handle {0}."), {HctxToUint32(WintabContextHandle)}));
				}

				++WintabContextIndex;
				continue;
			}

			LogVerbose(LOG_PREAMBLE, FString::Format(TEXT("Added tablet context for ID {0} [{1}, {2}]."), {
				                                         TabletContext.ID, TabletContext.Name , TabletContext.PlugAndPlayID
			                                         }));

			++WintabContextIndex;
		}
	}

	void FWintabInstance::UpdateWindowRect()
	{
		if (!ensure(WintabAPI.GetWindowRect(OSWindowHandle, &WindowRect)))
		{
			LogError(LOG_PREAMBLE, "Could not retrieve window rectangle; coordinates mapping will be incorrect!");
		}
	}
}

#undef LOG_PREAMBLE
#undef LOCTEXT_NAMESPACE
