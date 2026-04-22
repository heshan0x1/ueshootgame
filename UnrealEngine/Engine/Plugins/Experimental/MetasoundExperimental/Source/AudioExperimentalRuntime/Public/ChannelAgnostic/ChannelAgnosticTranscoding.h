// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypeFamily/ChannelTypeFamily.h"

namespace Audio
{
	// All transcoder (from -> to) combinations.
	AUDIOEXPERIMENTALRUNTIME_API FChannelTypeFamily::FTranscoder GetTranscoder(const FDiscreteChannelTypeFamily& InFrom, const FDiscreteChannelTypeFamily& InTo, const FChannelTypeFamily::FGetTranscoderParams&);
	AUDIOEXPERIMENTALRUNTIME_API FChannelTypeFamily::FTranscoder GetTranscoder(const FDiscreteChannelTypeFamily& InFrom, const FAmbisonicsChannelTypeFamily& InTo, const FChannelTypeFamily::FGetTranscoderParams&);
	AUDIOEXPERIMENTALRUNTIME_API FChannelTypeFamily::FTranscoder GetTranscoder(const FAmbisonicsChannelTypeFamily& InFrom, const FDiscreteChannelTypeFamily& InTo, const FChannelTypeFamily::FGetTranscoderParams&);
	AUDIOEXPERIMENTALRUNTIME_API FChannelTypeFamily::FTranscoder GetTranscoder(const FAmbisonicsChannelTypeFamily& InFrom, const FAmbisonicsChannelTypeFamily& InTo, const FChannelTypeFamily::FGetTranscoderParams&);
		
	template<typename TFromType>
	struct TFromVisitor final : IChannelTypeVisitor
	{
		const TFromType& FromType;
		const FChannelTypeFamily::FGetTranscoderParams& Params;
		FChannelTypeFamily::FTranscoder& Transcoder;
			
		TFromVisitor(const TFromType& InFromType, const FChannelTypeFamily::FGetTranscoderParams& InParams, FChannelTypeFamily::FTranscoder& InTranscoder)
			: FromType(InFromType), Params(InParams), Transcoder(InTranscoder)
		{}
			
		virtual void Visit(const FDiscreteChannelTypeFamily& InTo) override
		{
			Transcoder = GetTranscoder(FromType, InTo, Params);	
		}
		virtual void Visit(const FAmbisonicsChannelTypeFamily& InTo) override
		{
			Transcoder = GetTranscoder(FromType, InTo, Params);
		}
	};
		
	struct FTranscoderResolver final : public IChannelTypeVisitor
	{
		explicit FTranscoderResolver(const FChannelTypeFamily::FGetTranscoderParams& InParams) : Params(InParams) {}
		virtual void Visit(const FDiscreteChannelTypeFamily& InFrom) override
		{
			TFromVisitor Visitor(InFrom, Params, Result);
			Params.ToType.Accept(Visitor);
		}
		virtual void Visit(const FAmbisonicsChannelTypeFamily& InFrom) override
		{
			TFromVisitor Visitor(InFrom, Params, Result);
			Params.ToType.Accept(Visitor);
		}
		FChannelTypeFamily::FTranscoder&& MoveResult()
		{
			return MoveTemp(Result);
		}
		FChannelTypeFamily::FGetTranscoderParams Params;
		FChannelTypeFamily::FTranscoder Result;
	};
}