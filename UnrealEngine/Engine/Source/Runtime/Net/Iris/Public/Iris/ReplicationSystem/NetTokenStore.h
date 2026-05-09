// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NetTokenStore.h —— FNetTokenStore + FNetTokenDataStore 基类
// -------------------------------------------------------------------------------------------------------------
// 模块定位：所有 Token 化数据存储（FName/FString/Struct/GameplayTag…）的中央宿主，是 Iris "稳定数据压缩" 的核心。
//
// 工作模型（高层概览）：
//   - 量化阶段："具体值数据" → GetOrCreateToken → FNetToken（仅一个 32-bit 索引 + 1 bit 权威标志 + 5 bit TypeId）。
//   - 序列化阶段：状态比特流里只写 Token；具体值通过独立的 NetTokenDataStream 单独导出（一次性，按 ACK 跟踪）。
//   - 接收阶段：FNetTokenDataStream 比 ReplicationDataStream 更早处理，保证读到的 Token 都能被 ResolveToken。
//
// Local vs Remote Token：
//   - Authority（一般为 Server）：分配的 Token 标记 IsAssignedByAuthority=true，是"权威 Token"。
//   - 客户端在没收到权威 Token 之前可以先用本地分配的 Token；当收到来自权威的 Token 后，会把同一份数据的本地 Token
//     替换为权威 Token，下次再发送时就不必再 Export（已被对端确认）。
//   - 因此每个 FNetTokenStore 持有 1 份 LocalNetTokenStoreState + N 份 RemoteNetTokenStoreState（每连接一份）。
//
// 关键索引层次：
//     FNetToken (TypeId, Index, Authority)               <- 网络比特流上传输的紧凑 ID
//        ↓ FNetTokenStoreState[TypeId][Index]
//     FNetTokenStoreKey (KeyIndex)                       <- 在 DataStore 内部的本地数组索引
//        ↓ DataStore::StoredTokens[KeyIndex] / DataStore::StoredFNames[KeyIndex] 等
//     具体值（FName / FString / Struct）
//
// 类型层次：
//   FNetTokenDataStore (基类) 派生：
//     - FNameTokenStore        ：FName ↔ Token
//     - FStringTokenStore      ：FString ↔ Token
//     - TStructNetTokenDataStore<T> ：USTRUCT ↔ Token
//     - 还可派生 GameplayTagTokenStore 等
//
// 与 NetCore FNetToken 的协作：
//   - FNetToken 类型（uint64）由 NetCore 模块定义，本文件负责把它和"具体业务数据"做映射。
// =============================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Net/Core/NetToken/NetToken.h"
#include "UObject/NameTypes.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Templates/Tuple.h"

#include "NetTokenStore.generated.h"

/** 
* The idea with NetTokens is to allow export of "stable"-pieces of data such as string and names by replacing them with a NetToken during quantization/serialization.
* The data associated with a NetToken is then exported separately from the data. As soon as the exported data has been acknowledged the data will only be serialized using the NetToken.
* 
* As both Client and Server can communicate NetTokens, each side can end up assigning different tokens that will differs from each other.
* Here is a high-level overview of the algorithm which works slightly differently based on if using Iris replication or the old replication system.
*
* The sending side looks-up or creates a NetToken for the Data being serialized. Servers will mark assigned NetTokens as authoritative, while clients generate a temporary NetToken.
* When a network bunch/batch that contains NetToken is being sent, there is a per-connection look-up to see if we need to append and serialize exports or not, if the data associated with the token
* has been acknowledged the token will not be exported again.
*
* On the receiving side, imported exports are always guaranteed to have been processed before we attempt to read received data containing NetTokens which allows
* the receiving side to resolve the NetToken to get the actual data.
*
* The implementation details differs a bit depending on if we are using iris replication or old style replication.
*
* A current example that us used by both systems is GameplayTags, For Iris: See GameplayTagNetSerializer.cpp, for the old replication system: See: GameplayTagContainer.cpp.
*
* 中文摘要：
*   - "具体值"（如 FString/FName/UStruct）→ FNetToken 化后只在比特流里写 Token；
*   - 真正的数据通过独立通道（NetTokenDataStream）一次性导出并按 ACK 跟踪；
*   - 一旦 ACK，后续都只发 Token，从而获得显著的带宽节省（典型应用：GameplayTag）。
*/

/* NetBitArray validation support. */
#ifndef UE_NET_VALIDATE_NETTOKENTYPE
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#	define UE_NET_VALIDATE_NETTOKENTYPE 0
#else
#	define UE_NET_VALIDATE_NETTOKENTYPE 1
#endif
#endif


class UNetTokenDataStream;
namespace UE::Net
{
	class FNetTokenDataStore;
	class FNetTokenStore;
	class FNetTokenStoreState;
	class FNetSerializationContext;

	namespace Private
	{
		class FNetExportContext;
	}
}

USTRUCT()
struct FNetTokenStoreTypeIdPair
{
	GENERATED_BODY()
	UPROPERTY()
	FString StoreTypeName;
	UPROPERTY()
	uint32 TypeID = 0;
	bool operator<(const FNetTokenStoreTypeIdPair& Other) const
	{
		return TypeID < Other.TypeID;
	}
};
	
// -----------------------------------------------------------------------------
// UNetTokenTypeIdConfig
//   - 通过 [/Script/IrisCore.NetTokenTypeIdConfig] Engine.ini 配置 "TokenStore 名 ↔ TypeID" 的稳定映射；
//   - 之所以需要稳定映射：FNetToken 在比特流中只写 5-bit TypeId（FNetToken::TokenTypeIdBits），
//     如果在不同进程/版本之间 TypeId 漂移会导致解码错乱，所以必须由 ini 强制锁定；
//   - 例：+ReservedTypeIds=(StoreTypeName="NameTokenStore",  TypeID=0)
//         +ReservedTypeIds=(StoreTypeName="StringTokenStore", TypeID=1)
//         +ReservedTypeIds=(StoreTypeName="GameplayTagTokenStore", TypeID=2)
// -----------------------------------------------------------------------------
UCLASS(Transient, MinimalAPI, config=Engine)
class UNetTokenTypeIdConfig : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	TArray<FNetTokenStoreTypeIdPair> ReservedTypeIds;

	uint32 GetTypeID(const FString& TypeName) const;
	uint32 GetTypeID(const FName& TypeName) const
	{
		return GetTypeID(TypeName.ToString());
	}
	
protected:
	bool ReservedTypeIdsAppearValid() const;
};

namespace UE::Net
{
/**
 * NetTokenDataStore
 * Implemented per type to store and serialize data associated with a NetToken.
 *
 * 中文摘要：
 *   - 抽象基类，每种"被 Token 化的数据类型"派生一个具体子类（FNameTokenStore / FStringTokenStore / TStructNetTokenDataStore<T>）。
 *   - 内部维护一个 StoredTokens[KeyIndex] → FNetToken 的映射；KeyIndex 是本地数组下标，FNetToken 是网络层 ID。
 *   - 子类只需要实现 4 个虚函数：WriteTokenData / ReadTokenData （Iris BitStream 版 + FArchive 兼容版）。
 *   - WriteTokenData/ReadTokenData 不写 TypeId（TypeId 在 NetTokenStore 层统一处理）。
*/
class FNetTokenDataStore
{
public:
	// -------------------------------------------------------------------------
	// FNetTokenStoreKey
	//   - 在某个具体 DataStore 内部的本地索引（uint32），KeyIndex=0 始终保留为 Invalid。
	//   - 与 FNetToken 的 Index 不同：FNetToken 的 Index 是按 NetTokenStoreState 看到的"网络可见编号"，
	//     而 KeyIndex 是 DataStore 自己看到的"内存中的实际数组下标"。
	//   - 这层间接映射使得 Local Token 和 Authority Token 可以指向同一份具体值（同一个 KeyIndex）。
	// -------------------------------------------------------------------------
	class FNetTokenStoreKey
	{
	public:
		enum { InvalidKeyIndex = 0U };

		FNetTokenStoreKey()
		: KeyIndex(InvalidKeyIndex)
		{
		}

		explicit FNetTokenStoreKey(uint32 InKeyIndex)
		: KeyIndex(InKeyIndex)
		{
		}

		uint32 GetKeyIndex() const
		{ 
			return KeyIndex;
		}

		bool IsValid() const
		{ 
			return KeyIndex != InvalidKeyIndex;
		}

		bool operator==(const FNetTokenStoreKey& Other) const = default;

	private:
		uint32 KeyIndex = InvalidKeyIndex;
	};

	IRISCORE_API virtual ~FNetTokenDataStore();

	// Serialization methods for NetTokens that does not include the TypeId as this is known by the NetTokenDataStore.
	// 注意：以下 Read/Write NetToken 不带 TypeId（DataStore 已知自己的 TypeId）。

	// Write NetToken
	IRISCORE_API void WriteNetToken(UE::Net::FNetSerializationContext& Context, FNetToken Token);

	// Read NetToken
	IRISCORE_API FNetToken ReadNetToken(UE::Net::FNetSerializationContext& Context);

	// Write NetToken
	IRISCORE_API void WriteNetToken(FArchive& Ar, FNetToken Token);

	// Read NetToken
	IRISCORE_API FNetToken ReadNetToken(FArchive& Ar);

protected:

	explicit IRISCORE_API FNetTokenDataStore(FNetTokenStore& InTokenStore);

	// 子类实现：把 KeyIndex 对应的"具体值"写入比特流（Iris 路径）。
	virtual void WriteTokenData(FNetSerializationContext& Context, FNetTokenStoreKey Key) const = 0;
	// 子类实现：从比特流读出"具体值"，存入本地存储并返回 KeyIndex。
	virtual FNetTokenDataStore::FNetTokenStoreKey ReadTokenData(FNetSerializationContext& Context, const FNetToken& NetToken) = 0;

	//Do not export things through the UPackageMap. This will break things.
	// 兼容旧 NetSerialize 的 FArchive 路径；务必不要通过 UPackageMap 再次导出引用，否则会破坏 NetToken 体系。
	virtual void WriteTokenData(FArchive& Ar, FNetTokenStoreKey Key, UPackageMap* Map = nullptr) const = 0;
	virtual FNetTokenDataStore::FNetTokenStoreKey ReadTokenData(FArchive& Ar, const FNetToken& NetToken, UPackageMap* Map = nullptr) = 0;
	
	// 通过 (FNetToken, NetTokenStoreState) 反查到 DataStore 内部的 KeyIndex。
	IRISCORE_API FNetTokenDataStore::FNetTokenStoreKey GetTokenKey(FNetToken Token, const FNetTokenStoreState& TokenStoreState) const;

	inline FNetToken::FTypeId GetTypeId() const
	{
		return TypeId;
	}

	// Create new NetToken
	// 为 KeyIndex 分配并存储一个新的"本地 NetToken"（递增 LocalNetTokenStoreState 中的索引）。
	IRISCORE_API FNetToken CreateAndStoreTokenForKey(FNetTokenStoreKey Key);
	// 强制把 NetToken 与 KeyIndex 绑定（用于"收到权威 Token，覆盖本地 Token"的场景）。
	IRISCORE_API void StoreTokenForKey(FNetTokenStoreKey Key, FNetToken NetToken);
	IRISCORE_API FNetToken GetNetTokenFromKey(FNetTokenStoreKey) const;

	// Allocate next TokenStoreKey
	// 申请下一个 KeyIndex（即 StoredTokens 数组追加一个槽位）。
	FNetTokenDataStore::FNetTokenStoreKey GetNextNetTokenStoreKey();

protected:
	// Maps from FNetTokenStoreKey (index) to NetToken, this typically is the locally assigned NetToken, but can be overridden if we receive a token from the authority.
	// 索引 ↔ NetToken 的本地映射；正常情况存"本地分配的 Token"，在收到权威 Token 后会被覆盖为权威 Token，
	// 这样下一次 GetOrCreateToken 命中同一份数据时直接返回权威 Token，避免重复 Export。
	TArray<FNetToken> StoredTokens;
	FNetTokenStore& TokenStore;

private:
	friend FNetTokenStore;
	FNetToken::FTypeId TypeId; // 在 RegisterDataStore 时由 NetTokenStore 通过 ini 配置写入。
};

/**
 * FNetTokenStore
 * Main system for using NetTokensm currently owns type specific NetTokenDataStores and per connection state
 * Currently we have a unique instance per NetDriver/ReplicationSystem but it is possible we will share this across game instance.
 *
 * 中文摘要：
 *   - "中央门面"，一个 ReplicationSystem 持有一份 FNetTokenStore。
 *   - 持有：① N 个具体类型的 FNetTokenDataStore（按 TypeId 索引）；② 1 份 LocalNetTokenStoreState；
 *           ③ 每连接的 RemoteNetTokenStoreState（容量 = MaxConnections）。
 *   - 提供 InternalRead/WriteNetToken（含 TypeId）+ Read/WriteTokenData + ConditionalRead/WriteNetTokenData
 *     （后者按 ExportContext 已 ACK 状态决定要不要再次导出）。
 */
class FNetTokenStore
{
	UE_NONCOPYABLE(FNetTokenStore);
public:
	IRISCORE_API FNetTokenStore();
	IRISCORE_API ~FNetTokenStore();

	/** External configuration variables used to initialize the NetTokenStore */
	struct FInitParams
	{
		FNetToken::ENetTokenAuthority Authority; // 当前进程是否是 Authority（一般为 Server）。
		uint32 MaxConnections = 256;             // RemoteNetTokenStoreStates 的容量。
	};
	IRISCORE_API void Init(FInitParams& InitParams);

	/** Returns true if this is the authority, typically the server. */
	bool IsAuthority() const
	{
		return Params.Authority == FNetToken::ENetTokenAuthority::Authority;
	}

	/** A token is local if the authority of the NetTokenStore and the token matches, Invalid tokens are always local. */
	// "本地 Token"判定：Authority 端来看的 Authority Token 是本地，Client 端来看的 None Token 是本地。
	// 用于决定 ResolveToken 时使用 Local 状态表还是 Remote 状态表。
	bool IsLocalToken(const FNetToken NetToken) const
	{
		return !NetToken.IsValid() || IsAuthority() == NetToken.IsAssignedByAuthority();
	}

	/** Register DataStore and return true if it was registered. */
	// 注册一个具体类型的 DataStore（按 TokenStoreName 反查 ini 中的 TypeId 并赋给 DataStore）。
	IRISCORE_API bool RegisterDataStore(TUniquePtr<FNetTokenDataStore> DataStore, FName TokenStoreName);

	/** UnRegister  a DataStore and return true if it was registered. */
	IRISCORE_API bool UnRegisterDataStore(FName TokenStoreName);

	/** Get const access to data store by name. */
	IRISCORE_API const FNetTokenDataStore* GetDataStore(FName Name) const;

	/** Get data store by name. */
	IRISCORE_API FNetTokenDataStore* GetDataStore(FName Name);

	/** Create and return data store of specified type, it will be owned by NetTokenStore. */
	template<typename T>
	bool CreateAndRegisterDataStore()
	{
		TUniquePtr<FNetTokenDataStore> NetTokenDataStore(MakeUnique<T>(*this));
		
		return RegisterDataStore(MakeUnique<T>(*this), T::GetTokenStoreName());
	}
	
	/** Unregister a data store of specified type. */
	template<typename T>
	bool DeleteAndUnRegisterDataStore()
	{
		return UnRegisterDataStore(T::GetTokenStoreName());
	}

	/** Return data store of specified type. */
	template<typename T>
	T* GetDataStore()
	{
		return static_cast<T*>(GetDataStore(T::GetTokenStoreName()));
	}

	/** Return data store of specified type. */
	template<typename T>
	const T* GetDataStore() const
	{
		return static_cast<const T*>(GetDataStore(T::GetTokenStoreName()));
	}
	
	// FNetTokenStoreState maps from NetTokenIndex -> NetTokenStoreKey (Index)
	// Remote and local NetTokens use separate NetTokensStore states.
	// -------------------------------------------------------------------------
	// FNetTokenStoreState：每 (Local 或 Remote+ConnId) 一份。
	// 内部维护 TokenInfoArray[TypeId][NetTokenIndex] -> FNetTokenStoreKey 的双层数组映射。
	// 接收侧的 RemoteNetTokenStoreState 根据 NetTokenDataStream 收到的 Export 数据增长。
	// -------------------------------------------------------------------------

	/** Get const access to the local NetTokenStoreState */
	const FNetTokenStoreState* GetLocalNetTokenStoreState() const
	{
		return LocalNetTokenStoreState.Get();
	}

	/** Get access to the local NetTokenStoreState */
	FNetTokenStoreState* GetLocalNetTokenStoreState()
	{
		return LocalNetTokenStoreState.Get();
	}

	/** Init RemoteNetTokenStoreState for given ConnectionId, if it already exists it will be reset. */
	// 新连接接入时调用：分配/清空该连接的 Remote 状态表（接收 Token 表）。
	IRISCORE_API void InitRemoteNetTokenStoreState(uint32 ConnectionId);

	/** Get const access to RemoteNetTokenStoreState for given ConnectionId. */
	IRISCORE_API const FNetTokenStoreState* GetRemoteNetTokenStoreState(uint32 ConnectionId) const;

	/** Get RemoteNetTokenStoreState for given ConnectionId. */
	IRISCORE_API FNetTokenStoreState* GetRemoteNetTokenStoreState(uint32 ConnectionId);

	/** Write data associated with the NetToken. */
	// 把 Token 对应的"具体值"写入比特流（Iris BitStream 路径）。
	IRISCORE_API void WriteTokenData(FNetSerializationContext& Context, const FNetToken NetToken) const;

	/** Read data associated with the NetToken. */
	// 从比特流读出"具体值"，并把结果存入 RemoteNetTokenStoreState（包含 Token→KeyIndex 映射）。
	IRISCORE_API void ReadTokenData(FNetSerializationContext& Context, const FNetToken NetToken, FNetTokenStoreState& RemoteNetTokenStoreState);

	/** Write data associated with the NetToken. Do not export anything via the UPackageMap. */
	// FArchive 兼容路径：用于旧 NetSerialize（FArchive&）-> NetToken 的桥接。
	IRISCORE_API void WriteTokenData(FArchive& Ar, const FNetToken NetToken, UPackageMap* Map = nullptr) const;
	
	/** Read data associated with the NetToken. Do not export anything via the UPackageMap. */
	IRISCORE_API void ReadTokenData(FArchive& Ar, const FNetToken NetToken, FNetTokenStoreState& RemoteNetTokenStoreState, UPackageMap* Map = nullptr);

	/** Conditionally write NetTokenData unless already exported. */
	// 写入 1 bit 标志位 + 可选的 token data；通过 ExportContext 查询该 Token 是否已 ACK：
	//   - 已 ACK 或来自远端的 Token：仅写 0 bit；
	//   - 未 ACK：写 1 bit + token data，并把 Token 加入"已导出"集合。
	IRISCORE_API void ConditionalWriteNetTokenData(FNetSerializationContext& Context, Private::FNetExportContext* ExportContext, const FNetToken NetToken) const;

	/** Conditionally read NetTokenData if exported. */
	IRISCORE_API void ConditionalReadNetTokenData(FNetSerializationContext& Context, const FNetToken NetToken);

	/** Utility methods, consolidate with other changes to NetTokenStore as next step. */
	// 把 Token 加入到 Context 的 "Pending Export" 列表（之后由 NetTokenDataStream::WriteData 真正 export）。
	IRISCORE_API static void AppendExport(FNetSerializationContext&, FNetToken NetToken);

	/** 
	 * Convenience method to Write a NetToken without writing type bits
	 * In development builds it will verify that the token type matches the StoreType, otherwise it will skip the lookup of DataStore.
	 *
	 * 中文：在已知 DataStore 类型的场景，跳过 5-bit TypeId 的写入以节省带宽。
	 */
	template <typename T>
	void WriteNetTokenWithKnownType(FNetSerializationContext& Context, FNetToken NetToken)
	{
#if UE_NET_VALIDATE_NETTOKENTYPE
		// We only verify this in dev builds as it should trap most programmer errors.
		if (NetToken.IsValid())
		{
			const T* DataStore = GetDataStore<T>();
			if (!DataStore || (DataStore->GetTypeId() != NetToken.GetTypeId()))
			{
				UE_LOG(LogNetToken, Error, TEXT("Tried to write NetToken %s using invalid NetTokenStore %s"), *NetToken.ToString(), *(T::GetTokenStoreName().ToString()));
				// Just to get some attention to the log.
				ensure(DataStore && (DataStore->GetTypeId() == NetToken.GetTypeId()));
			}
		}
#endif
		const bool bWriteTypeId = false;
		FNetTokenStore::InternalWriteNetToken(Context, NetToken, bWriteTypeId);
	}

	/** 
	 * Convenience method to read a NetToken without reading type bits.
	*/
	template <typename T>
	FNetToken ReadNetTokenWithKnownType(FNetSerializationContext& Context)
	{
		FNetToken NetToken;
		if (T* DataStore = GetDataStore<T>())
		{
			NetToken = DataStore->ReadNetToken(Context);
		}
		else
		{
			UE_LOG(LogNetToken, Error, TEXT("ReadNetTokenWithKnownType Tried to read NetToken using invalid NetTokenStore %s"), *(T::GetTokenStoreName().ToString()));
			// Just to get some attention to the log.
			ensure(DataStore);
		}

		return NetToken;
	}

	/** Write NetToken including TypeId */
	void WriteNetToken(FNetSerializationContext& Context, FNetToken NetToken) const
	{
		const bool bWriteTypeId = true;
		FNetTokenStore::InternalWriteNetToken(Context, NetToken, bWriteTypeId);
	}

	/** Read NetToken including TypeId */
	FNetToken ReadNetToken(FNetSerializationContext& Context) const
	{
		return InternalReadNetToken(Context, FNetToken::InvalidTokenTypeId);
	}

	/** Write NetToken including TypeId */
	void WriteNetToken(FArchive& Ar, FNetToken NetToken) const
	{
		const bool bWriteTypeId = true;
		FNetTokenStore::InternalWriteNetToken(Ar, NetToken, bWriteTypeId);
	}

	/** Read NetToken including TypeId */
	FNetToken ReadNetToken(FArchive& Ar) const
	{
		return InternalReadNetToken(Ar, FNetToken::InvalidTokenTypeId);
	}
	
private:

	friend UNetTokenDataStream;
	friend FNetTokenDataStore;

	using FNetTokenStoreKey = FNetTokenDataStore::FNetTokenStoreKey;

	// Internal method to write a NetToken, if bWriteTokenType is true it will write the TypeId as well.
	// 比特流编码：PackedUint32(Index) + 1 bit IsAssignedByAuthority + 可选 5-bit TypeId。
	IRISCORE_API static void InternalWriteNetToken(UE::Net::FNetSerializationContext& Context, FNetToken Token, bool bWriteTokenType);
	IRISCORE_API static void InternalWriteNetToken(FArchive& Ar, FNetToken Token, bool bWriteTokenType);

	// Internal method to read a NetToken, if the TokenTypeId is valid it will be used instead of reading it from the bitstream
	IRISCORE_API static FNetToken InternalReadNetToken(UE::Net::FNetSerializationContext& Context, FNetToken::FTypeId TokenTypeId);
	IRISCORE_API static FNetToken InternalReadNetToken(FArchive& Ar, FNetToken::FTypeId TokenTypeId);

	// 验证收到的 (NetToken, KeyIndex) 是否一致；如果是 Authority Token 且本端不是 Authority，则替换 Local Token。
	bool ValidateAndStoreNetTokenData(FNetTokenDataStore& DataStore, FNetTokenStoreState& RemoteNetTokenStoreState, const FNetToken NetToken, const FNetTokenStoreKey StoreKey);
	
	// 调试/预导出辅助：拿到当前所有已分配的本地 Token。
	TArray<FNetToken> GetAllNetTokens() const;

	// 构造一个合法的 FNetToken（带边界检查）。
	static FNetToken MakeNetToken(uint32 TypeId, uint32 Index, FNetToken::ENetTokenAuthority Authority)
	{ 
		if ((TypeId < FNetToken::MaxTypeIdCount) && (Index < FNetToken::MaxNetTokenCount))
		{
			return FNetToken(TypeId, Index, Authority);
		}
		
		return FNetToken();
	}

	TUniquePtr<FNetTokenStoreState> LocalNetTokenStoreState;                  // 本端"发"用的状态表（Local Token 表）。
	TArray<TUniquePtr<FNetTokenStoreState>> RemoteNetTokenStoreStates;        // 每连接一份"收"用的状态表（Remote Token 表）。
	TArray<TTuple<FName, TUniquePtr<FNetTokenDataStore>>> TokenDataStores;    // 按 TypeId 索引的 DataStore 表（Name 仅用于查重 / 日志）。
	FInitParams Params;
};

inline FNetTokenDataStore::FNetTokenStoreKey FNetTokenDataStore::GetNextNetTokenStoreKey()
{
	uint32 NextTokenStoreKeyIndex = (uint32)StoredTokens.Num();
	if (ensure(NextTokenStoreKeyIndex < FNetToken::MaxNetTokenCount))
	{
		FNetTokenStoreKey TokenKey(NextTokenStoreKeyIndex);

		// We fill in the actual key later
		StoredTokens.Add(FNetToken());

		return TokenKey;
	}

	return FNetTokenStoreKey();
}
}
