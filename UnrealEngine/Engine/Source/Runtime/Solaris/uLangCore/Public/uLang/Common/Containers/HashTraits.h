// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace uLang
{
// Default hash function for pointers.
inline uint32_t GetTypeHash(const void* Key)
{
	const uintptr_t PtrInt = reinterpret_cast<uintptr_t>(Key) >> 4;
    uint32_t Hash = static_cast<uint32_t>(PtrInt);    
    Hash ^= Hash >> 16;
    Hash *= 0x85ebca6b;
    Hash ^= Hash >> 13;
    Hash *= 0xc2b2ae35;
    Hash ^= Hash >> 16;
    return Hash;
}

inline uint32_t MurmurFinalize32(uint32_t Hash)
{
    Hash ^= Hash >> 16;
    Hash *= 0x85ebca6b;
    Hash ^= Hash >> 13;
    Hash *= 0xc2b2ae35;
    Hash ^= Hash >> 16;
    return Hash;
}

/**
 * Combines two hash values to get a third.
 * Note - this function is not commutative.
 *
 * WARNING!  This function is subject to change and should only be used for creating
 *           combined hash values which don't leave the running process,
 *           e.g. GetTypeHash() overloads.
 */
[[nodiscard]] inline constexpr uint32_t HashCombineFast(uint32_t A, uint32_t B)
{
    return A ^ (B + 0x9e3779b9 + (A << 6) + (A >> 2));
}

inline uint32_t GetTypeHash(uint32_t I)
{
    return MurmurFinalize32(I);
}

inline uint32_t GetTypeHash(int32_t I)
{
    return MurmurFinalize32(static_cast<uint32_t>(I));
}

inline uint32_t GetTypeHash(uint64_t I)
{
    return HashCombineFast(MurmurFinalize32(static_cast<uint32_t>(I)), MurmurFinalize32(static_cast<uint32_t>(I >> 32)));
}

inline uint32_t GetTypeHash(int64_t I)
{
    return HashCombineFast(MurmurFinalize32(static_cast<uint32_t>(I)), MurmurFinalize32(static_cast<uint32_t>(static_cast<uint64_t>(I) >> 32)));
}

template<class KeyType>
struct TDefaultHashTraits
{
    static uint32_t GetKeyHash(const KeyType& Key) { return GetTypeHash(Key); }
};
}
