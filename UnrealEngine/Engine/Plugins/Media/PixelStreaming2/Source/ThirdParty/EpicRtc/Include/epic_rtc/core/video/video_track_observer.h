// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/common.h"

#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/video/video_frame.h"

#pragma pack(push, 8)

class EpicRtcVideoTrackInterface;  // forward declaration

class EpicRtcVideoTrackObserverInterface : public EpicRtcRefCountInterface
{
public:
    virtual void OnVideoTrackMuted(EpicRtcVideoTrackInterface*, EpicRtcBool mute) = 0;
    virtual void OnVideoTrackState(EpicRtcVideoTrackInterface*, const EpicRtcTrackState) = 0;
    virtual void OnVideoTrackFrame(EpicRtcVideoTrackInterface*, const EpicRtcVideoFrame&) = 0;

    /**
     * Fires when encoded frame becomes available.
     * For local track this event will fire after the frame was encoded, for remote track after the frame has been received and assembled.
     * @param participantId Id of the participant this track belongs to. In P2P mode the local track will fire this event multiple times for on PushFrame invocation since every
     *  P2P participant will have their own video encoder.
     * @param track Video track the frame originated from.
     * @param frame Encoded video frame.
     */
    virtual void OnVideoTrackEncodedFrame(EpicRtcStringView participantId, EpicRtcVideoTrackInterface* track, const EpicRtcEncodedVideoFrame& frame) = 0;

    /**
     * Indicates whether the observer is ready to receive messages.
     * If false, any method calls will be ignored.
     * @return EpicRtcBool Observer enabled state
     */
    virtual EpicRtcBool Enabled() const = 0;
};

class EpicRtcVideoTrackObserverFactoryInterface : public EpicRtcRefCountInterface
{
public:
    virtual EpicRtcErrorCode CreateVideoTrackObserver(const EpicRtcStringView participantId, const EpicRtcStringView videoTrackId, EpicRtcVideoTrackObserverInterface** outVideoTrackObserver) = 0;
};

#pragma pack(pop)
