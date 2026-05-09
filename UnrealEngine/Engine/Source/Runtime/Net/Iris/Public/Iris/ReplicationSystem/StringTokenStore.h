// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// StringTokenStore.h —— FString 的 NetToken 化存储
// -------------------------------------------------------------------------------------------------------------
// 与 NameTokenStore 类似，但内部用 CityHash64(FString) 作为去重 key（避免持有大量 FString 副本进行哈希）。
// 持久化存储用 FMemStackBase 进行批量分配的 TCHAR* 字符串副本，存储期内永不释放（生命周期 = ReplicationSystem）。
// =============================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Iris/ReplicationSystem/NetTokenStore.h"
#include "Misc/MemStack.h"

namespace UE::Net
{

// Simple token store used to store string tokens
// When the PackageMapRefactor is complete we will most likely rely on NetTagManager for persistent storage
// 简化版字符串 token 存储；待 PackageMap 重构完成后大概率会切换到 NetTagManager 提供的持久存储。
class FStringTokenStore : public FNetTokenDataStore
{
	UE_NONCOPYABLE(FStringTokenStore);
public:
	IRISCORE_API explicit FStringTokenStore(FNetTokenStore& TokenStore);

	// Create a string token for the provided string
	// 主入口：FString -> NetToken（同一字符串多次 GetOrCreateToken 都返回同一个 Token）。
	IRISCORE_API FNetToken GetOrCreateToken(const FString& String);
	IRISCORE_API FNetToken GetOrCreateToken(const TCHAR* Name, uint32 Length);

	// Resolve NetToken, to resolve remote tokens RemoteTokenStoreState must be valid
	// 解析 Token：
	//   - 如果是本端发出的"本地 Token"，使用 LocalNetTokenStoreState；
	//   - 如果是收到的"远端 Token"，必须传入接收侧的 RemoteTokenStoreState。
	// 返回值：内部持久化的 const TCHAR*（生命周期 = 该 Store 的 Allocator）。
	IRISCORE_API const TCHAR* ResolveToken(FNetToken Token, const FNetTokenStoreState* RemoteTokenStoreState = nullptr) const;	

	// Resolve a token received from remote
	// 显式语义版本：明确表示 Token 来自远端。
	const TCHAR* ResolveRemoteToken(FNetToken Token, const FNetTokenStoreState& NetTokenStoreState) const
	{ 
		return ResolveToken(Token, &NetTokenStoreState);
	}

	static FName GetTokenStoreName()
	{
		return StringTokenStoreName;
	}

protected:
	// Serialize data for a token, note there is not validation in this function
	// 写入字符串内容（Iris BitStream 走 WriteString，FArchive 走 operator<<）。
	virtual void WriteTokenData(FNetSerializationContext& Context, FNetTokenStoreKey TokenStoreKey) const override;
	virtual void WriteTokenData(FArchive& Archive, FNetTokenStoreKey TokenStoreKey, UPackageMap* Map = nullptr) const override;

	// Read data for a token, returns a valid StoreKey if successful read
	virtual FNetTokenStoreKey ReadTokenData(FNetSerializationContext& Context, const FNetToken& NetToken) override;
	virtual FNetTokenStoreKey ReadTokenData(FArchive& Archive, const FNetToken& NetToken, UPackageMap* Map = nullptr) override;

	// Create a persistent string
	// 把字符串 Hash 化（CityHash64）查找/插入 HashToKey；新插入时会通过 FMemStackBase 复制一份持久副本。
	FNetTokenStoreKey GetOrCreatePersistentString(const TCHAR* Name, uint32 Length);

private:
	inline static FName StringTokenStoreName = TEXT("StringTokenStore");

	TMap<uint64, FNetTokenStoreKey> HashToKey;  // CityHash64(string) -> KeyIndex（去重 + 反查）。
	TArray<const TCHAR*> StoredStrings;          // KeyIndex -> 持久化字符串指针（指向 Allocator 分配的内存）。
	FMemStackBase Allocator;                     // 字符串副本的批量分配器（不会单独释放，全部随 Store 析构释放）。
};

}
