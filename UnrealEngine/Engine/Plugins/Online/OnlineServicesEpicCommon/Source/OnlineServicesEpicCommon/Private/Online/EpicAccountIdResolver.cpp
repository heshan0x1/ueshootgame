// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/EpicAccountIdResolver.h"

#include "Online/OnlineAsyncOp.h"

namespace UE::Online
{

TFunction<TFuture<FAccountId>(FOnlineAsyncOp& InAsyncOp, const EOS_EpicAccountId EpicAccountId)> IEpicAccountIdResolver::ResolveEpicAccountIdFn()
{
	return [this](FOnlineAsyncOp& InAsyncOp, const EOS_EpicAccountId& EpicAccountId)
	{
		const FAccountId* LocalAccountIdPtr = InAsyncOp.Data.Get<FAccountId>(TEXT("LocalAccountId"));
		if (!ensure(LocalAccountIdPtr))
		{
			return MakeFulfilledPromise<FAccountId>().GetFuture();
		}
		return ResolveAccountId(*LocalAccountIdPtr, EpicAccountId);
	};
}

TFunction<TFuture<TArray<FAccountId>>(FOnlineAsyncOp& InAsyncOp, const TArray<EOS_EpicAccountId>& EpicAccountIds)> IEpicAccountIdResolver::ResolveEpicAccountIdsFn()
{
	return [this](FOnlineAsyncOp& InAsyncOp, const TArray<EOS_EpicAccountId>& EpicAccountIds)
	{
		const FAccountId* LocalAccountIdPtr = InAsyncOp.Data.Get<FAccountId>(TEXT("LocalAccountId"));
		if (!ensure(LocalAccountIdPtr))
		{
			return MakeFulfilledPromise<TArray<FAccountId>>().GetFuture();
		}
		return ResolveAccountIds(*LocalAccountIdPtr, EpicAccountIds);
	};
}

} // namespace UE::Online