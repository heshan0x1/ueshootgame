// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcDataTrackObserver.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EpicRtcDataTrackObserver)

namespace UE::PixelStreaming2
{
	FEpicRtcDataTrackObserver::FEpicRtcDataTrackObserver(TObserverVariant<IPixelStreaming2DataTrackObserver> UserObserver)
		: UserObserver(UserObserver)
	{
	}

	void FEpicRtcDataTrackObserver::OnDataTrackState(EpicRtcDataTrackInterface* DataTrack, const EpicRtcTrackState State)
	{
		if (UserObserver)
		{
			UserObserver->OnDataTrackState(DataTrack, State);
		}
	}

	void FEpicRtcDataTrackObserver::OnDataTrackMessage(EpicRtcDataTrackInterface* DataTrack)
	{
		if (UserObserver)
		{
			UserObserver->OnDataTrackMessage(DataTrack);
		}
	}

	void FEpicRtcDataTrackObserver::OnDataTrackError(EpicRtcDataTrackInterface* DataTrack, const EpicRtcErrorCode Error)
	{
		if (UserObserver)
		{
			UserObserver->OnDataTrackError(DataTrack, Error);
		}
	}
} // namespace UE::PixelStreaming2
