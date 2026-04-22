// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/containers/epic_rtc_string.h"

#include "epic_rtc/core/audio/audio_frame.h"

#pragma pack(push, 8)

/**
 * Represents the audio track. Exposes methods to send and receive audio data.
 */
class EpicRtcAudioTrackInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Gets instance Id.
     * @return Id.
     */
    virtual EPICRTC_API EpicRtcStringView GetId() = 0;

    /**
     * Mute/unmute the track.
     * @param InMuted State, pass true to mute, false to unmute.
     */
    virtual EPICRTC_API void Mute(EpicRtcBool inMuted) = 0;

    /**
     * Stop track. Works with the local tracks only.
     */
    virtual EPICRTC_API void Stop() = 0;

    /**
     * Subscribe to remote track.
     */
    virtual EPICRTC_API void Subscribe() = 0;

    /**
     * Unsubscribe from remote track.
     */
    virtual EPICRTC_API void Unsubscribe() = 0;

    /**
     * Supply frame for processing. This will push the frame directly to the encoder.
     * @param InFrame Frame to process
     * @return False if error pushing frame
     */
    virtual EPICRTC_API EpicRtcBool PushFrame(const EpicRtcAudioFrame& inFrame) = 0;

    /**
     * Indicates the track belongs to the remote participant.
     * @return True if the track belongs to the remote participant.
     */
    virtual EPICRTC_API EpicRtcBool IsRemote() = 0;

    /**
     * Gets track state.
     * @return State of the track.
     */
    virtual EPICRTC_API EpicRtcTrackState GetState() = 0;

    /**
     * Get track subscription state.
     * @return Subscription state of the track.
     */
    virtual EPICRTC_API EpicRtcTrackSubscriptionState GetSubscriptionState() = 0;

    // Prevent copying
    EpicRtcAudioTrackInterface(const EpicRtcAudioTrackInterface&) = delete;
    EpicRtcAudioTrackInterface& operator=(const EpicRtcAudioTrackInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EPICRTC_API EpicRtcAudioTrackInterface() = default;
    virtual EPICRTC_API ~EpicRtcAudioTrackInterface() = default;
};

#pragma pack(pop)
