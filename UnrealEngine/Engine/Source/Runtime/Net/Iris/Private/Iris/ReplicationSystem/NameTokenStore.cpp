// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NameTokenStore.cpp —— FName 的 NetToken 化存储实现
// -------------------------------------------------------------------------------------------------------------
// 序列化路径：
//   Iris  ：WriteString / ReadString（NetBitStreamUtil）
//   FArchive：UPackageMap::StaticSerializeName（保留旧 NetSerialize 兼容性）
// 后续优化（注释中已记录 JIRA：UE-221753）：可以利用 FName 的 String/Number 拆分来进一步压缩。
// =============================================================================================================

#include "Iris/ReplicationSystem/NameTokenStore.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Hash/CityHash.h"
#include "Net/Core/Trace/NetTrace.h"
#include "UObject/CoreNet.h"

#define UE_NET_ENABLE_FNAME_TOKEN_LOG 1

#if UE_NET_ENABLE_FNAME_TOKEN_LOG
#	define UE_LOG_FNAMETOKEN(Format, ...)  UE_LOG(LogNetToken, Verbose, Format, ##__VA_ARGS__)
#else
#	define UE_LOG_FNAMETOKEN(...)
#endif

#define UE_LOG_FNAMETOKEN_WARNING(Format, ...)  UE_LOG(LogNetToken, Warning, Format, ##__VA_ARGS__)

namespace UE::Net
{

// 主入口：FName -> NetToken
//   1) 通过 GetOrCreateTokenStoreKey 在本地数组中"复用 / 新建" KeyIndex；
//   2) 若该 KeyIndex 还没有绑定 NetToken（即首次出现），则通过 CreateAndStoreTokenForKey 分配本地 Token；
//   3) 否则直接返回已绑定的 Token（可能是本地 Token，也可能是已被替换为权威 Token，详见 NetTokenStore::ValidateAndStoreNetTokenData）。
FNetToken FNameTokenStore::GetOrCreateToken(FName Name)
{
	FNetTokenStoreKey Key = GetOrCreateTokenStoreKey(Name);
	if (Key.IsValid())
	{
		const FNetToken ExistingToken = GetNetTokenFromKey(Key);
		if (ExistingToken.IsValid())
		{
			return ExistingToken;
		}
		else
		{
			const FNetToken NewToken = CreateAndStoreTokenForKey(Key);

			UE_LOG_FNAMETOKEN(TEXT("FNameTokenStore::GetOrCreateToken - Created %s for %s"), *NewToken.ToString(), *Name.ToString());

			return NewToken;
		}
	}

	return FNetToken();
}

// FName -> KeyIndex 反查；找不到时分配新 KeyIndex 并把 FName 写入双向数组。
FNetTokenDataStore::FNetTokenStoreKey FNameTokenStore::GetOrCreateTokenStoreKey(FName Name)
{
	if (const FNetTokenStoreKey* ExistingKey = FNameToKey.Find(Name))
	{
		return *ExistingKey;
	}

	const FNetTokenStoreKey NewKey = GetNextNetTokenStoreKey();
	if (NewKey.IsValid())
	{
		StoredFNames.Add(Name);
		FNameToKey.Add(Name, NewKey);

		return NewKey;
	}

	return FNetTokenStoreKey();
}

FNameTokenStore::FNameTokenStore(FNetTokenStore& InTokenStore)
: FNetTokenDataStore(InTokenStore)
{
	// As we use an array for our storage we must match the size of the StoredTokens array.
	// We assume that Index 0 is invalid.
	// 必须与基类 StoredTokens 数组对齐（基类已预占 Index 0 为 Invalid）。
	StoredFNames.SetNum(StoredTokens.Num());
}

// 解析 Token：
//   - "本地 Token"（与本端权威性相同）：用 LocalNetTokenStoreState 反查；
//   - "远端 Token"：必须传入对端的 RemoteTokenStoreState（来自 FNetTokenStore::GetRemoteNetTokenStoreState）。
FName FNameTokenStore::ResolveToken(FNetToken Token, const FNetTokenStoreState* NetTokenStoreState) const
{
	const FNetTokenStoreState* TokenStoreState = TokenStore.IsLocalToken(Token) ? TokenStore.GetLocalNetTokenStoreState() : NetTokenStoreState;
	if (Token.IsValid() && ensureMsgf(TokenStoreState, TEXT("FNameTokenStore::ResolveToken Needs valid remote NetTokenStoreState to resolve remote %s"), *Token.ToString()))
	{
		const FNetTokenStoreKey StoreKey = GetTokenKey(Token, *TokenStoreState);
		if (StoreKey.IsValid() && StoreKey.GetKeyIndex() < (uint32)StoredFNames.Num())
		{
			return StoredFNames[StoreKey.GetKeyIndex()];
		}
		else
		{
			UE_LOG(LogNetToken, Error, TEXT("FNameTokenStore::ResolveToken failed to resolve %s in NetTokenDataStore: %s"), *Token.ToString(), *GetTokenStoreName().ToString());
		}
	}

	return FName();
}

// 把 KeyIndex 对应的 FName 写入比特流（Iris 路径）：当前实现为完整字符串导出。
void FNameTokenStore::WriteTokenData(FNetSerializationContext& Context, FNetTokenStoreKey TokenStoreKey) const
{
	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(StoredFNames[TokenStoreKey.GetKeyIndex()], *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
	UE_LOG_FNAMETOKEN(TEXT("FNameTokenStore::WriteTokenData %s %s"), *(StoredTokens[TokenStoreKey.GetKeyIndex()].ToString()), *(StoredFNames[TokenStoreKey.GetKeyIndex()].ToString()));
	// $TODO: $IRIS: We can be a bit smarter here and utilize the string-number split of FNames to export less data.. JIRA: UE-221753
	WriteString(Context.GetBitStreamWriter(), StoredFNames[TokenStoreKey.GetKeyIndex()].ToString());
}

// FArchive 兼容路径：通过 UPackageMap::StaticSerializeName 走 NetGUID 化的 FName 序列化（Hardcoded / Lookup-table 等）。
void FNameTokenStore::WriteTokenData(FArchive& Ar, FNetTokenStoreKey TokenStoreKey, UPackageMap* Map /*= nullptr*/) const
{
	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(StoredFNames[TokenStoreKey.GetKeyIndex()], static_cast<FNetBitWriter&>(Ar), GetTraceCollector(static_cast<FNetBitWriter&>(Ar)), ENetTraceVerbosity::VeryVerbose);
	UE_LOG_FNAMETOKEN(TEXT("FNameTokenStore::WriteTokenData %s %s"), *(StoredTokens[TokenStoreKey.GetKeyIndex()].ToString()), *(StoredFNames[TokenStoreKey.GetKeyIndex()].ToString()));
	FName Name = StoredFNames[TokenStoreKey.GetKeyIndex()];
	UPackageMap::StaticSerializeName(Ar, Name);
}

// 接收侧：读到字符串 → FName → 在本端的 FNameToKey 表中查找/插入，返回 KeyIndex。
// 注意此处并不直接绑定 LocalToken，绑定由 FNetTokenStore::ValidateAndStoreNetTokenData 完成。
FNetTokenDataStore::FNetTokenStoreKey FNameTokenStore::ReadTokenData(FNetSerializationContext& Context, const FNetToken& NetToken)
{
	UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(TokenScope, FName(), *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	// Read the token data and add it to the store without assigning LocalToken
	FString Temp;
	ReadString(Reader, Temp);

	if (!Reader->IsOverflown())
	{
		FName Name(Temp);
		UE_NET_TRACE_SET_SCOPE_NAME(TokenScope, Name);

		return GetOrCreateTokenStoreKey(Name);
	}
	else
	{
		return FNetTokenStoreKey();
	}
}

FNetTokenDataStore::FNetTokenStoreKey FNameTokenStore::ReadTokenData(FArchive& Ar, const FNetToken& NetToken, UPackageMap* Map /*= nullptr*/)
{
	FName Name;
	UPackageMap::StaticSerializeName(Ar, Name);

	if (!Ar.IsError())
	{
		return GetOrCreateTokenStoreKey(Name);
	}
	else
	{
		return FNetTokenStoreKey();
	}
}

}
