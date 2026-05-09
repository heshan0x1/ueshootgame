// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// NameTokenStore.h —— FName 的 NetToken 化存储
// -------------------------------------------------------------------------------------------------------------
// 把任意 FName 通过 NetToken 化压缩传输：相同的 FName 多次发送只 export 一次（其余只发 4-6 字节的 Token）。
// 与 FNameAsNetTokenNetSerializer 协作：NetSerializer 在 Quantize 时调用 GetOrCreateToken，反量化时 ResolveToken。
// =============================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Iris/ReplicationSystem/NetTokenStore.h"

namespace UE::Net
{

class FNameTokenStore : public FNetTokenDataStore
{
	UE_NONCOPYABLE(FNameTokenStore);
public:
	IRISCORE_API explicit FNameTokenStore(FNetTokenStore& TokenStore);

	// Create a NetToken for the provided name
	// 主入口：FName ↔ NetToken。命中已有映射则直接返回；否则分配新 KeyIndex + 本地 Token（首次发送会被 export）。
	IRISCORE_API FNetToken GetOrCreateToken(FName Name);

	// Resolve NetToken, to resolve remote tokens RemoteTokenStoreState must be valid
	// 解析：本地 Token 用 LocalNetTokenStoreState；远端 Token 必须传入接收侧的 RemoteTokenStoreState。
	IRISCORE_API FName ResolveToken(FNetToken Token, const FNetTokenStoreState* RemoteTokenStoreState = nullptr) const;	

	static FName GetTokenStoreName()
	{ 
		return NameTokenStoreName;
	}

protected:
	// Serialize data for a token, note there is not validation in this function
	// 写入"具体值"——FName 字符串内容。
	IRISCORE_API virtual void WriteTokenData(FNetSerializationContext& Context, FNetTokenStoreKey TokenStoreKey) const override;
	IRISCORE_API virtual void WriteTokenData(FArchive& Archive, FNetTokenStoreKey TokenStoreKey, UPackageMap* Map = nullptr) const override;

	// Read data for a token, returns a valid StoreKey if successful read
	// 读取并落库；如果该 FName 在本端已存在则复用同一 KeyIndex（保证收发双方对同一份数据有相同的 Key）。
	IRISCORE_API virtual FNetTokenStoreKey ReadTokenData(FNetSerializationContext& Context, const FNetToken& NetToken) override;
	IRISCORE_API virtual FNetTokenStoreKey ReadTokenData(FArchive& Archive, const FNetToken& NetToken, UPackageMap* Map = nullptr) override;

	// Create and store data for Token
	// 内部：FName→KeyIndex 反查，找不到则分配新 KeyIndex 并把 Name 入库。
	FNetTokenStoreKey GetOrCreateTokenStoreKey(FName Name);

private:

	inline static FName NameTokenStoreName = TEXT("NameTokenStore");

	// 双向映射：① FName 哈希查找 KeyIndex；② 数组按 KeyIndex 查找 FName。
	TMap<FName, FNetTokenStoreKey> FNameToKey;
	TArray<FName> StoredFNames;
};

}
