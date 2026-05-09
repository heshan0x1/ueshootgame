// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetTokenStore.cpp —— FNetTokenStore + FNetTokenDataStore 实现
// -------------------------------------------------------------------------------------------------------------
// 本文件包含三个层次：
//   1) FNetTokenStoreState（内部实现类）：TokenInfoArray[TypeId][Index] -> KeyIndex 的二级数组；
//   2) FNetTokenDataStore：基类的 GetTokenKey / CreateAndStoreTokenForKey / Read|WriteNetToken 实现；
//   3) FNetTokenStore：注册表 + 比特流读写 + 条件性 export + 远端 Token 校验 + 替换为权威 Token。
// =============================================================================================================

#include "Iris/ReplicationSystem/NetTokenStore.h"
#include "Iris/ReplicationSystem/ObjectReferenceCacheFwd.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Net/Core/Trace/NetTrace.h"
#include "UObject/CoreNet.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetTokenStore)

namespace UE::Net
{
class FNetSerializationContext;
	

class FNetTokenStoreState
{
public:
	using FNetTokenStoreKey = FNetTokenDataStore::FNetTokenStoreKey;

	// The size of the array is managed by FNetTokenDataStream
	// 注：TokenInfoArray 的实际增长由 NetTokenDataStream 接收 export 数据时驱动 ReserveTokenCount。
	FNetTokenStoreState()
	{
		Reset();
	}

	// Reserve space for tokens
	// 接收侧：根据收到的 NetToken.GetIndex() 扩张数组到 (Index+1) 大小，并把空槽位填 0（=Invalid Key）。
	bool ReserveTokenCount(uint32 TypeIndex, uint32 NewCount)
	{
		if ((TypeIndex >= FNetToken::MaxTypeIdCount) || (NewCount >= FNetToken::MaxNetTokenCount))
		{
			return false;
		}

		const uint32 NewSize = FMath::Max<uint32>(TokenInfoArray[TypeIndex].Num(), NewCount);
		TokenInfoArray[TypeIndex].SetNumZeroed(NewSize);

		return true;
	}

	void Reset()
	{
		// We reserve the first token for each type as an invalid token
		// Index=0 始终为非法槽，方便 ReserveTokenCount + SetNumZeroed 用 0 表示"Invalid"。
		for (TArray<FNetTokenStoreKey>& TokenInfos : TokenInfoArray)
		{
			TokenInfos.SetNum(1);
		}
	}

	// Map from NetTokenIndex -> NetTokenDataStoreKey (Index)
	// 二维数组：第一维 = TypeId（5 bit，最多 32 个 DataStore），第二维 = NetToken 内部索引（22 bit）。
	TArray<FNetTokenDataStore::FNetTokenStoreKey> TokenInfoArray[FNetToken::MaxTypeIdCount];
};

FNetTokenDataStore::FNetTokenDataStore(FNetTokenStore& InTokenStore)
: TokenStore(InTokenStore)
, TypeId(FNetToken::InvalidTokenTypeId)
{
	// Reserve first index as invalid.
	// 与 FNetTokenStoreState 一样，KeyIndex=0 也保留为非法值。
	StoredTokens.Add(FNetToken());
}

FNetTokenDataStore::~FNetTokenDataStore()
{
}

// 通过 NetToken（含 Index 和 TypeId）+ TokenStoreState 反查到 DataStore 内部数组的 KeyIndex。
// - 调用方需要保证 Token.GetTypeId() == 当前 DataStore 的 TypeId（不一致是程序员错误）。
FNetTokenDataStore::FNetTokenStoreKey FNetTokenDataStore::GetTokenKey(FNetToken Token, const FNetTokenStoreState& TokenStoreState) const
{
	if (Token.GetTypeId() == GetTypeId())
	{
		const TArray<FNetTokenStoreKey>& TokenStoreKeysForType = TokenStoreState.TokenInfoArray[GetTypeId()];
		const int32 TokenIndex(Token.GetIndex());

		return TokenIndex < TokenStoreKeysForType.Num() ? TokenStoreKeysForType[TokenIndex] : FNetTokenStoreKey();
	}
	else
	{
		UE_LOG(LogNetToken, Error, TEXT("FNetTokenDataStore::GetTokenKey Invalid tokentype %s StoreTypeId: %u"), *Token.ToString(), GetTypeId());
		return FNetTokenStoreKey();
	}
}

// 为给定 KeyIndex 分配一个新的"本地 NetToken"：
// - LocalNetTokenStoreState.TokenInfoArray[TypeId] 末尾追加 KeyIndex（=> 该 NetToken 的 Index = Num-1）；
// - 把同一份 (KeyIndex, NetToken) 也写到 StoredTokens[KeyIndex]，便于下次 GetOrCreateToken 时直接复用 Token。
FNetToken FNetTokenDataStore::CreateAndStoreTokenForKey(FNetTokenStoreKey Key)
{
	FNetTokenStoreState& LocalNetTokenStoreState = *TokenStore.GetLocalNetTokenStoreState();

	// The LocalNetTokenStoreState always maps locally assigned tokens -> Key (Index)
	const uint32 NextTokenIndex = LocalNetTokenStoreState.TokenInfoArray[GetTypeId()].Num();

	if (!ensure(NextTokenIndex < FNetToken::MaxNetTokenCount))
	{
		return FNetToken();
	}

	FNetToken NewToken = FNetTokenStore::MakeNetToken(TypeId, NextTokenIndex, TokenStore.IsAuthority() ? FNetToken::ENetTokenAuthority::Authority : FNetToken::ENetTokenAuthority::None);

	// Store token info
	LocalNetTokenStoreState.TokenInfoArray[GetTypeId()].Add(Key);

	// The stored tokens array contains the current NetToken associated with the Key (Index), but it can be updated to use an authoritative token instead.
	StoredTokens[Key.GetKeyIndex()] = NewToken;

	return NewToken;
}

void FNetTokenDataStore::StoreTokenForKey(FNetTokenStoreKey Key, FNetToken NetToken)
{
	// The stored tokens array contains the current NetToken associated with the Key (Index), but it can be updated to use an authoritative token instead.
	StoredTokens[Key.GetKeyIndex()] = NetToken;
}

FNetToken FNetTokenDataStore::GetNetTokenFromKey(FNetTokenStoreKey Key) const
{
	return StoredTokens[Key.GetKeyIndex()];
}

// -----------------------------------------------------------------------------
// 比特流编码格式（FNetSerializationContext 路径）：
//   PackedUint32(Index)
//   if (Index != Invalid)
//     1 bit IsAssignedByAuthority
//     [optional] 5 bit TypeId（仅当 bWriteTokenType=true）
// -----------------------------------------------------------------------------
FNetToken FNetTokenStore::InternalReadNetToken(UE::Net::FNetSerializationContext& Context, FNetToken::FTypeId TokenTypeId)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	FNetToken ReadToken;

	UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(TokenScope, FName(), *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
	const uint32 TokenIndex = ReadPackedUint32(Reader);
	if (const bool bIsValid = (TokenIndex != FNetToken::InvalidTokenIndex))
	{
		const bool bIsAssignedByAuthority = Reader->ReadBool();
		if (TokenTypeId == FNetToken::InvalidTokenTypeId)
		{
			TokenTypeId = Reader->ReadBits(FNetToken::TokenTypeIdBits);
		}

		if (!Context.HasErrorOrOverflow())
		{
			ReadToken = FNetTokenStore::MakeNetToken(TokenTypeId, TokenIndex, bIsAssignedByAuthority ? FNetToken::ENetTokenAuthority::Authority : FNetToken::ENetTokenAuthority::None);
			UE_NET_TRACE_SET_SCOPE_NAME(TokenScope, *ReadToken.ToString());

			// We expected a valid token
			if (!ReadToken.IsValid())
			{
				Context.SetError(GNetError_InvalidValue);
				UE_LOG(LogNetToken, Error, TEXT("Expected a valid token but got an invalid one. TokenTypeId: %u TokenIndex: %u."), TokenTypeId, TokenIndex);
				ensure(false);
			}
		}
	}

	return ReadToken;
}

void FNetTokenStore::InternalWriteNetToken(UE::Net::FNetSerializationContext& Context, FNetToken Token, bool bWriteTokenType)
{
	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(*Token.ToString(), *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	const uint32 TokenIndex = Token.GetIndex();
	WritePackedUint32(Writer, TokenIndex);
	if (TokenIndex != FNetToken::InvalidTokenIndex)
	{
		Writer->WriteBool(Token.IsAssignedByAuthority());
		if (bWriteTokenType)
		{
			Writer->WriteBits(Token.GetTypeId(), FNetToken::TokenTypeIdBits);
		}
	}
}

FNetToken FNetTokenStore::InternalReadNetToken(FArchive& Ar, FNetToken::FTypeId TokenTypeId)
{
	FNetToken ReadToken;

	uint32 TokenIndex = 0;
	Ar.SerializeIntPacked(TokenIndex);
	if (const bool bIsValid = (TokenIndex != FNetToken::InvalidTokenIndex))
	{
		bool bIsAssignedByAuthority = false;
		Ar.SerializeBits(&bIsAssignedByAuthority, 1);

		if (TokenTypeId == FNetToken::InvalidTokenTypeId)
		{
			uint32 TempTokenTypeId = 0;
			Ar.SerializeBits(&TempTokenTypeId, FNetToken::TokenTypeIdBits);
			TokenTypeId = TempTokenTypeId;
		}

		if (!Ar.IsError())
		{
			ReadToken = FNetTokenStore::MakeNetToken(TokenTypeId, TokenIndex, bIsAssignedByAuthority ? FNetToken::ENetTokenAuthority::Authority : FNetToken::ENetTokenAuthority::None);
		}
	}

	return ReadToken;
}

// Note: Be careful when modifying this methods to not affect replay compatibility.
// 注意：修改本方法的比特流格式将破坏 replay 兼容性。
void FNetTokenStore::InternalWriteNetToken(FArchive& Ar, FNetToken Token, bool bWriteTokenType)
{
	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(*Token.ToString(), static_cast<FNetBitWriter&>(Ar), GetTraceCollector(static_cast<FNetBitWriter&>(Ar)), ENetTraceVerbosity::VeryVerbose);
	uint32 TokenIndex = Token.GetIndex();
	Ar.SerializeIntPacked(TokenIndex);
	if (TokenIndex != FNetToken::InvalidTokenIndex)
	{
		bool bIsAssignedByAuthority = Token.IsAssignedByAuthority();
		Ar.SerializeBits(&bIsAssignedByAuthority, 1);

		if (bWriteTokenType)
		{
			uint32 TokenTypeId = Token.GetTypeId();
			Ar.SerializeBits(&TokenTypeId, FNetToken::TokenTypeIdBits);
		}
	}
}

FNetToken FNetTokenDataStore::ReadNetToken(UE::Net::FNetSerializationContext& Context)
{
	return FNetTokenStore::InternalReadNetToken(Context, GetTypeId());
}

void FNetTokenDataStore::WriteNetToken(UE::Net::FNetSerializationContext& Context, FNetToken Token)
{
	const bool bWriteTokenTypeId = false;
	return FNetTokenStore::InternalWriteNetToken(Context, Token, bWriteTokenTypeId);
}

FNetToken FNetTokenDataStore::ReadNetToken(FArchive& Ar)
{
	return FNetTokenStore::InternalReadNetToken(Ar, GetTypeId());
}

void FNetTokenDataStore::WriteNetToken(FArchive& Ar, FNetToken Token)
{
	const bool bWriteTokenTypeId = false;
	FNetTokenStore::InternalWriteNetToken(Ar, Token, bWriteTokenTypeId);
}

FNetTokenStore::FNetTokenStore()
: LocalNetTokenStoreState(new FNetTokenStoreState)
{
}

FNetTokenStore::~FNetTokenStore()
{
}

void FNetTokenStore::Init(FNetTokenStore::FInitParams& InParams)
{
	Params = InParams;
	// 为每条连接预留一格 Remote 状态表（按 ConnectionId 索引）。
	RemoteNetTokenStoreStates.SetNum(InParams.MaxConnections);
}

void FNetTokenStore::InitRemoteNetTokenStoreState(uint32 ConnectionId)
{
	// 新连接接入：清空已有状态表 / 创建新表，准备接收远端的 Token Export。
	if (ensureMsgf((ConnectionId != InvalidConnectionId) && (ConnectionId < (uint32)RemoteNetTokenStoreStates.Num()), TEXT("Trying to init RemoteNetTokenStoreState for invalid connection %u"), ConnectionId))
	{
		if (FNetTokenStoreState* ExistingState = RemoteNetTokenStoreStates[ConnectionId].Get())
		{
			ExistingState->Reset();
		}
		else
		{
			RemoteNetTokenStoreStates[ConnectionId] = MakeUnique<FNetTokenStoreState>();
		}
	}
}

const FNetTokenStoreState* FNetTokenStore::GetRemoteNetTokenStoreState(uint32 ConnectionId) const
{
	if (!ensureMsgf((ConnectionId != InvalidConnectionId) && (ConnectionId < (uint32)RemoteNetTokenStoreStates.Num()), TEXT("Trying to access RemoteNetTokenStoreState for ConnectionID: %u"), ConnectionId))
	{
		return nullptr;
	}
	return RemoteNetTokenStoreStates[ConnectionId].Get();
}

FNetTokenStoreState* FNetTokenStore::GetRemoteNetTokenStoreState(uint32 ConnectionId)
{
	if (!ensureMsgf((ConnectionId != InvalidConnectionId) && (ConnectionId < (uint32)RemoteNetTokenStoreStates.Num()), TEXT("Trying to access non existing RemoteNetTokenStoreState for ConnectionId: %u"), ConnectionId))
	{
		return nullptr;
	}
	return RemoteNetTokenStoreStates[ConnectionId].Get();
}

const FNetTokenDataStore* FNetTokenStore::GetDataStore(FName Name) const
{
	const TTuple<FName, TUniquePtr<FNetTokenDataStore>>* Entry = TokenDataStores.FindByPredicate([Name](const TTuple<FName, TUniquePtr<FNetTokenDataStore>>& Entry){ return Name == Entry.Get<0>(); });
	return Entry ? Entry->Get<1>().Get() : nullptr;
}

 FNetTokenDataStore* FNetTokenStore::GetDataStore(FName Name)
{
	TTuple<FName, TUniquePtr<FNetTokenDataStore>>* Entry = TokenDataStores.FindByPredicate([Name](TTuple<FName, TUniquePtr<FNetTokenDataStore>>& Entry){ return Name == Entry.Get<0>(); });
	return Entry ? Entry->Get<1>().Get() : nullptr;
}

bool FNetTokenStore::RegisterDataStore(TUniquePtr<FNetTokenDataStore> DataStore, FName TokenStoreName)
{
	// 上限校验：FNetToken 的 TypeId 字段宽度决定最多能注册的 DataStore 数（5-bit => 32 种）。
	if (TokenDataStores.Num() >= FNetToken::MaxTypeIdCount)
	{
		return false;
	}

	if (!DataStore)
	{
		return false;
	}

	if (!ensure(GetDataStore(TokenStoreName) == nullptr))
	{
		// Already registered
		return false;
	}

	// 必需配置：[/Script/IrisCore.NetTokenTypeIdConfig]+ReservedTypeIds=(StoreTypeName="...", TypeID=N)
	// 否则 Token 的 TypeId 在不同进程间不稳定，会破坏跨端解码。
	const UNetTokenTypeIdConfig* TypeIDConfig = GetDefault<UNetTokenTypeIdConfig>();
	const uint32 TypeId = TypeIDConfig->GetTypeID(TokenStoreName);
	DataStore->TypeId = TypeId;
	if (!ensureAlwaysMsgf(DataStore->TypeId != FNetToken::InvalidTokenTypeId, TEXT("No TypeID information found in [/Script/IrisCore.NetTokenTypeIdConfig] Engine.ini for %s"), *TokenStoreName.ToString()))
	{
		UE_LOG(LogNetToken, Error, TEXT("No TypeID information found in [/Script/IrisCore.NetTokenTypeIdConfig] Engine.ini for %s"), *TokenStoreName.ToString());
		return false;
	}

	if (!ensure(TypeId != FNetToken::InvalidTokenTypeId && TypeId < FNetToken::MaxTypeIdCount))
	{
		UE_LOG(LogNetToken, Error, TEXT("Invalid TypeID information found for %s"), *TokenStoreName.ToString());
		return false;
	}

	// 防止 ini 中两个 StoreTypeName 配成同一个 TypeID（会让运行时数据错乱）。
	bool bLargeEnough = static_cast<uint32>(TokenDataStores.Num()) > DataStore->TypeId;
	bool bSomethingAlreadyExists = bLargeEnough ? TokenDataStores[DataStore->TypeId].Value.IsValid() : false;
	FName ExistingName = bLargeEnough ? TokenDataStores[DataStore->TypeId].Key : NAME_None;
	if (!ensureAlwaysMsgf(!bSomethingAlreadyExists, TEXT("An existing TokenDataStore (Name %s) has been created with that TypeId when Trying to add New TokenDataStore with name: %s"),*ExistingName.ToString(),*TokenStoreName.ToString()))
	{
		UE_LOG(LogNetToken, Error, TEXT("An existing TokenDataStore (Name %s) has been created with that TypeId when Trying to add New TokenDataStore with name: %s"),*ExistingName.ToString(),*TokenStoreName.ToString());
		return false;
	}

	// Need to resize to
	// 直接用 TypeId 作为下标存放，便于 O(1) 查询。
	if (!bLargeEnough)
	{
		TokenDataStores.SetNum(DataStore->TypeId + 1);
	}

	TokenDataStores.EmplaceAt(DataStore->TypeId, TokenStoreName, MoveTemp(DataStore));

	return true;
}

bool FNetTokenStore::UnRegisterDataStore(FName TokenStoreName)
{
	if (TokenDataStores.Num() >= FNetToken::MaxTypeIdCount)
	{
		return false;
	}
	
	if (FNetTokenDataStore* DataStore = GetDataStore(TokenStoreName))
	{
		// Delete but don't resize the array
		TokenDataStores[DataStore->TypeId].Value.Reset();
		TokenDataStores[DataStore->TypeId].Key = NAME_None;
		return true;
	}

	// Failed
	return false;
}

// 写入路径：根据 NetToken 的 TypeId 找到 DataStore，再通过 LocalNetTokenStoreState 反查到 KeyIndex，最后委托给 DataStore 写"具体值"。
void FNetTokenStore::WriteTokenData(FNetSerializationContext& Context, const FNetToken NetToken) const
{
	if (NetToken.IsValid())
	{
		FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

		const FNetTokenStoreKey& TokenKey = LocalNetTokenStoreState->TokenInfoArray[NetToken.GetTypeId()][NetToken.GetIndex()];

		// Write token data
		TokenDataStores[NetToken.GetTypeId()].Get<1>().Get()->WriteTokenData(Context, TokenKey);
	}
}

void FNetTokenStore::WriteTokenData(FArchive& Ar, const FNetToken NetToken, UPackageMap* Map /*= nullptr*/) const
{
	if (NetToken.IsValid())
	{
		const FNetToken::FTypeId TokenTypeId = NetToken.GetTypeId();

		// We cannot write token data for bad tokens
		if ((TokenTypeId >= FNetToken::MaxTypeIdCount) || (NetToken.GetIndex() >= (uint32)LocalNetTokenStoreState->TokenInfoArray[TokenTypeId].Num()))
		{
			UE_LOG(LogNetToken, Error, TEXT("Trying to write data for unknown NetToken %s"), *NetToken.ToString());
			return;
		}

		const FNetTokenStoreKey TokenKey = LocalNetTokenStoreState->TokenInfoArray[NetToken.GetTypeId()][NetToken.GetIndex()];

		// Write token data
		TokenDataStores[NetToken.GetTypeId()].Get<1>()->WriteTokenData(Ar, TokenKey, Map);
	}
}

// 读取侧的核心校验：
//   - 验证 RemoteNetTokenStoreState 中尚无该 NetTokenIndex 或已存的 KeyIndex 与新读到的一致；
//   - 当对端发来"权威 Token"且本端不是 Authority 时，把本地 StoredTokens[KeyIndex] 替换为权威 Token，
//     这样下次本地再 GetOrCreateToken 同一份数据，会直接拿到权威 Token，从而避免再次 export。
bool FNetTokenStore::ValidateAndStoreNetTokenData(FNetTokenDataStore& DataStore, FNetTokenStoreState& RemoteNetTokenStoreState, const FNetToken NetToken, const FNetTokenStoreKey StoreKey)
{
	if (!StoreKey.IsValid() || !RemoteNetTokenStoreState.ReserveTokenCount(NetToken.GetTypeId(), NetToken.GetIndex() + 1))
	{
		return false;
	}

	TArray<FNetTokenStoreKey>& TokenStoreKeysForType = RemoteNetTokenStoreState.TokenInfoArray[NetToken.GetTypeId()];

	// Since the same tokendata might be exported multiple times we better validate that it is the same data
	const FNetTokenStoreKey& ExistingStoreKey = TokenStoreKeysForType[NetToken.GetIndex()];
	if (!ensureAlways(!ExistingStoreKey.IsValid() || StoreKey == ExistingStoreKey))
	{
		return false;
	}

	// If this is an authtoken and we are not the authority, update the stored key so that we can use the authoriative key instead of the local key
	if (NetToken.IsAssignedByAuthority() && !IsAuthority())
	{
		UE_LOG(LogNetToken, Verbose, TEXT("FNetTokenStore::ReadTokenData - Replaced local key %s with %s"), *DataStore.StoredTokens[StoreKey.GetKeyIndex()].ToString(), *NetToken.ToString());	

		// This way the next time we lookup this key during assignment use the imported authoritative key which we do not have to export
		DataStore.StoredTokens[StoreKey.GetKeyIndex()] = NetToken;
	}
	
	// Store
	TokenStoreKeysForType[NetToken.GetIndex()] = StoreKey;

	return true;
}

void FNetTokenStore::ReadTokenData(FNetSerializationContext& Context, const FNetToken NetToken, FNetTokenStoreState& RemoteNetTokenStoreState)
{
	if (NetToken.IsValid())
	{
		FNetBitStreamReader* Reader = Context.GetBitStreamReader();

		FNetToken::FTypeId TokenTypeId = NetToken.GetTypeId();

		if (TokenTypeId >= (uint32)TokenDataStores.Num())
		{
			Context.SetError(GNetError_InvalidValue);
			UE_LOG(LogNetToken, Error, TEXT("Failed to read ReadTokenData for %s."), *NetToken.ToString());
			ensure(false);
			return;
		}

		FNetTokenDataStore* DataStore = TokenDataStores[TokenTypeId].Get<1>().Get();
		const FNetTokenStoreKey StoreKey = DataStore->ReadTokenData(Context, NetToken);

		if (!ValidateAndStoreNetTokenData(*DataStore, RemoteNetTokenStoreState, NetToken, StoreKey))
		{
			Context.SetError(GNetError_InvalidValue);
			UE_LOG(LogNetToken, Error, TEXT("Failed to read ReadTokenData for %s."), *NetToken.ToString());
			ensure(false);
			return;
		}
	}
}

void FNetTokenStore::ReadTokenData(FArchive& Ar, const FNetToken NetToken, FNetTokenStoreState& RemoteNetTokenStoreState, UPackageMap* Map /*= nullptr*/)
{
	if (NetToken.IsValid())
	{
		// Read type
		FNetToken::FTypeId TokenTypeId = NetToken.GetTypeId();

		// Validate that we managed to read and verify the type
		if (Ar.IsError() || TokenTypeId >= (uint32)TokenDataStores.Num() )
		{
			Ar.SetError();
			UE_LOG(LogNetToken, Error, TEXT("Failed to read ReadTokenData for %s."), *NetToken.ToString());
			return;
		}

		FNetTokenDataStore* DataStore = TokenDataStores[TokenTypeId].Get<1>().Get();
		const FNetTokenStoreKey StoreKey = DataStore->ReadTokenData(Ar, NetToken, Map);

		if (!ValidateAndStoreNetTokenData(*DataStore, RemoteNetTokenStoreState, NetToken, StoreKey))
		{
			Ar.SetError();
			UE_LOG(LogNetToken, Error, TEXT("Failed to read ReadTokenData for %s."), *NetToken.ToString());
			ensure(false);
			return;
		}
	}
}

// 条件性写入：核心是"避免重复 export"。
//   - 远端发来的 Token 不应再被本端 export → 直接写 false 跳过；
//   - ExportContext 中已记录该 Token 已被 ACK → 写 false 跳过；
//   - 否则写 true + Token Data，并把 Token 加入 "本批次已 export" 集合。
void FNetTokenStore::ConditionalWriteNetTokenData(FNetSerializationContext& Context, Private::FNetExportContext* ExportContext, const FNetToken NetToken) const
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// We should not try to export tokens received from remote
	if (IsAuthority() != NetToken.IsAssignedByAuthority())
	{
		Writer->WriteBool(false);
		return;
	}

	if (ExportContext)
	{
		if (Writer->WriteBool(!ExportContext->IsExported(NetToken)))
		{
			WriteTokenData(Context, NetToken);
			ExportContext->AddExported(NetToken);			
		}
	}
	else
	{
		Writer->WriteBool(true);
		WriteTokenData(Context, NetToken);
	}
}

// 条件性读取：先读 1 bit；为 1 时调用 ReadTokenData 并写入 ResolveContext.RemoteNetTokenStoreState；
// 为 0 时假设对端已 ACK 过同一 Token（或来自远端，已存在状态表中）。
void FNetTokenStore::ConditionalReadNetTokenData(FNetSerializationContext& Context, const FNetToken NetToken)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	const bool bIsExportToken = Reader->ReadBool();
	if (bIsExportToken)
	{
		if (Reader->IsOverflown())
		{
			return;
		}

		FNetObjectResolveContext& ResolveContext = Context.GetInternalContext()->ResolveContext;
	
		ReadTokenData(Context, NetToken, *ResolveContext.RemoteNetTokenStoreState);
	}
}

// 把 Token 注入"本批次待 export"集合；真正 export 由 NetTokenDataStream::WriteData 负责。
// 该方法被 TStructAsNetTokenNetSerializerImpl::Serialize 等大量调用。
void FNetTokenStore::AppendExport(FNetSerializationContext& Context, FNetToken NetToken)
{
	using namespace UE::Net::Private;

	FNetExportContext* ExportContext = Context.GetExportContext();
	if (ensure(ExportContext))
	{
		ExportContext->AddPendingExport(NetToken);
	}
}

// 调试/预导出：枚举本端所有已分配的 Token（用于 net.Iris.IrisPreExportExistingNetTokensOnConnect）。
TArray<FNetToken> FNetTokenStore::GetAllNetTokens() const
{
	TArray<FNetToken> Result;

	const FNetTokenStoreState* TokenStoreState = GetLocalNetTokenStoreState();
	for (const TTuple<FName, TUniquePtr<FNetTokenDataStore>>& TokenDataStore : TokenDataStores)
	{
		const uint32 TypeId = TokenDataStore.Get<1>()->GetTypeId();
		const uint32 NumTokensForType = TokenStoreState->TokenInfoArray[TypeId].Num();
		if (NumTokensForType > 1)
		{
			Result.Append(MakeArrayView(&TokenDataStore.Get<1>()->StoredTokens[1], NumTokensForType - 1));
		}
	}

	return MoveTemp(Result);
}

}

// -----------------------------------------------------------------------------
// UNetTokenTypeIdConfig：Engine.ini → 运行时 TypeID 反查
// -----------------------------------------------------------------------------
uint32 UNetTokenTypeIdConfig::GetTypeID(const FString& TypeName) const
{
	// Need to reevaluate every retrieval in the event of hotfixes, or ini changes due to GFP loading/unloading.
	bool bTypeIdsAppearValid = ReservedTypeIdsAppearValid();
	if (!ensureAlwaysMsgf(bTypeIdsAppearValid, TEXT("Duplicate or Invalid TypeIds detected!")))
	{
		return UE::Net::FNetToken::InvalidTokenTypeId;
	}
	for (const FNetTokenStoreTypeIdPair& TypePair : ReservedTypeIds)
	{
		if (TypePair.StoreTypeName == TypeName)
		{
			return TypePair.TypeID;
		}
	}
	checkf(false, TEXT("Unknown Token Store Type %s. StoreType MUST be declared in Engine.ini."), *TypeName);
	return UE::Net::FNetToken::InvalidTokenTypeId;
}

bool UNetTokenTypeIdConfig::ReservedTypeIdsAppearValid() const
{
	if (ReservedTypeIds.Num() == 0)
	{
		UE_LOG(LogNetToken, Error, TEXT("No TypeID information found in [/Script/IrisCore.NetTokenTypeIdConfig] Engine.ini"));
		return false;
	}
	TMap<uint32,FString> Found;
	for (const FNetTokenStoreTypeIdPair& TypeIdPair : ReservedTypeIds)
	{
		if (!ensureAlwaysMsgf(TypeIdPair.TypeID < UE::Net::FNetToken::MaxTypeIdCount, TEXT("TypeID %d found for %s that is larger than FNetToken::MaxIdCount %d"), TypeIdPair.TypeID, *TypeIdPair.StoreTypeName, UE::Net::FNetToken::MaxTypeIdCount))
		{
			UE_LOG(LogNetToken, Error, TEXT("TypeID %d found for %s that is larger than FNetToken::MaxIdCount %d"), TypeIdPair.TypeID, *TypeIdPair.StoreTypeName, UE::Net::FNetToken::MaxTypeIdCount);
			return false;
		}
		if (!ensureAlwaysMsgf(!Found.Contains(TypeIdPair.TypeID), TEXT("Duplicate TypeIDs found: %s and %s with TypeId: %u"), *Found[TypeIdPair.TypeID], *TypeIdPair.StoreTypeName, TypeIdPair.TypeID))
		{
			UE_LOG(LogNetToken, Error, TEXT("Duplicate TypeID information found in [/Script/IrisCore.NetTokenTypeIdConfig] Engine.ini for%s and %s with TypeId: %u"), *Found[TypeIdPair.TypeID], *TypeIdPair.StoreTypeName, TypeIdPair.TypeID);
			return false;
		}
		Found.Add(TypeIdPair.TypeID, TypeIdPair.StoreTypeName);
	}
	return true;
}
