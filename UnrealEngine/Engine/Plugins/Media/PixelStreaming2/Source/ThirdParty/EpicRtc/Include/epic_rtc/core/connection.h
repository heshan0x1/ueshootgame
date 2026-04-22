// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/core/audio/audio_source.h"
#include "epic_rtc/core/connection_config.h"
#include "epic_rtc/core/data_source.h"
#include "epic_rtc/core/video/video_source.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

/**
 * Represents media connection with the Media-Server or another Participant. In terms of WebRTC, this would be PeerConnection.
 * Holds all the media-related state and methods.
 */
class EpicRtcConnectionInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Add audio source.
     * @param InAudioSource Audio source to be added.
     */
    virtual EPICRTC_API EpicRtcErrorCode AddAudioSource(const EpicRtcAudioSource& inAudioSource) = 0;

    /**
     * Add video source.
     * @param InVideoSource Video source to be added.
     */
    virtual EPICRTC_API EpicRtcErrorCode AddVideoSource(const EpicRtcVideoSource& inVideoSource) = 0;

    /**
     * Add data source.
     * @param inParticipantId Participant ID this source will be added to. In MediaServer mode use local participant ID.
     * @param InDataSource Data source to be added.
     */
    virtual EPICRTC_API EpicRtcErrorCode AddDataSource(const EpicRtcStringView& inParticipantId, const EpicRtcDataSource& inDataSource) = 0;

    /**
     * Gets maximum frame size for data Track in bytes.
     * @return Maximum frame size.
     */
    virtual EPICRTC_API uint64_t GetMaxDataMessageSizeBytes() = 0;

    /**
     * Restarts underlying transport after applying the new configuration.
     * In WebRTC terms, this would be the same as restarting ice.
     * @param InConnectionConfig New connection configuration.
     */
    virtual EPICRTC_API EpicRtcErrorCode RestartConnection(const EpicRtcStringView& inParticipantId, const EpicRtcConnectionConfig& inConnectionConfig) = 0;

    /**
     * Start the negotiation with the remote peer. Call this method after adding local sources to an active Room or after P2P participant has joined to kick off the negotiation.
     */
    virtual EPICRTC_API void StartNegotiation() = 0;

    /**
     * Start the negotiation with the remote peer. Call this method after adding local sources to an active Room or after P2P participant has joined to kick off the negotiation.
     * @param inParticipantId Participant ID the negotiation should be started with. In MediaServer mode use local participant ID.
     */
    virtual EPICRTC_API EpicRtcErrorCode StartNegotiation(const EpicRtcStringView& inParticipantId) = 0;

    /**
     * Sets the bitrates used for this connection. Default values are set in the EpicRtcRoomConfig during CreateRoom, but this method
     * can be used to update the rate on a per connection basis.
     * @param inBitrate New bitrate configuration.
     */
    virtual EPICRTC_API EpicRtcErrorCode SetConnectionRates(const EpicRtcStringView& inParticipantId, const EpicRtcBitrate& inBitrate) = 0;

    /**
     * Stats toggle at Connection level, set to false to disable stats for this specific connection only.
     * @param enabled Enable/disable flag
     */
    virtual EPICRTC_API EpicRtcErrorCode SetStatsEnabled(const EpicRtcStringView& inParticipantId, EpicRtcBool enabled) = 0;

    // Prevent copying
    EpicRtcConnectionInterface(const EpicRtcConnectionInterface&) = delete;
    EpicRtcConnectionInterface& operator=(const EpicRtcConnectionInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EPICRTC_API EpicRtcConnectionInterface() = default;
    virtual EPICRTC_API ~EpicRtcConnectionInterface() = default;
};

#pragma pack(pop)
