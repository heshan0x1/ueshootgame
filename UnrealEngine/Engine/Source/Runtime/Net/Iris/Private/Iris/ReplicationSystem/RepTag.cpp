// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// RepTag.cpp —— RepTag 哈希生成与查找实现
// -------------------------------------------------------------------------------------------------------------
// 关键算法：
//   - MakeRepTag："RepTag_" + TagName 一起进 CityHash64WithSeed，避免与裸字符串哈希冲突。
//   - FindRepTag(Protocol)：遍历 ReplicationStateDescriptors[]，按 StateDescriptor 累加 InternalSize 计算绝对偏移。
//   - FindRepTag(Descriptor)：遍历 MemberTagDescriptors[]，命中后通过 InnerTagIndex 在嵌套 Struct 中递归。
// =============================================================================================================

#include "Iris/ReplicationSystem/RepTag.h"
#include "Iris/ReplicationState/InternalReplicationStateDescriptorUtils.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/Serialization/NetSerializers.h" // FStructNetSerializerConfig
#include "Containers/ArrayView.h"
#include "HAL/PlatformString.h"
#include "Hash/CityHash.h"

namespace UE::Net
{

FRepTag MakeRepTag(const char* TagName)
{
	// 注意：Seed 是 ASCII "RepTag_\0"（0x52='R', 0x65='e', ...），用来给所有 RepTag 增加一个统一前缀语义，
	// 避免与"裸字符串 hash"（无前缀）发生冲突。修改 Seed 会破坏所有已生成 Tag 的兼容性。
	constexpr uint64 Seed = 0x5265705461675F00ULL; // Think of this as "RepTag_"
	return CityHash64WithSeed(TagName, FPlatformString::Strlen(TagName), Seed);
}

// 仅检测 Tag 是否存在；遍历 Protocol 内全部 StateDescriptor 的 TagDescriptors。
bool HasRepTag(const FReplicationProtocol* Protocol, FRepTag RepTag)
{
	check(Protocol);
	for (const FReplicationStateDescriptor* StateDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, Protocol->ReplicationStateCount))
	{
		for (const FReplicationStateMemberTagDescriptor& TagDescriptor : MakeArrayView(StateDescriptor->MemberTagDescriptors, StateDescriptor->TagCount))
		{
			if (TagDescriptor.Tag == RepTag)
			{
				return true;
			}
		}
	}

	return false;
}

// 在单个 StateDescriptor 内查找 Tag，并解析成员的偏移信息：
//   - 普通成员：直接读取 MemberDescriptor 的 ExternalMemberOffset / InternalMemberOffset；
//   - 嵌套 Struct（InnerTagIndex 有效）：递归进入子 Descriptor，累加偏移直到找到叶子成员。
bool FindRepTag(const FReplicationStateDescriptor* Descriptor, FRepTag RepTag, FRepTagFindInfo& OutRepTagFindInfo)
{
	check(Descriptor);
	for (const FReplicationStateMemberTagDescriptor& TagDescriptor : MakeArrayView(Descriptor->MemberTagDescriptors, Descriptor->TagCount))
	{
		if (TagDescriptor.Tag != RepTag)
		{
			continue;
		}

		// Lookup offsets and serializers from the descriptor. We may have to dig deep to find the relevant information.
		// 初始化输出。OutRepTagFindInfo.StateIndex 由 Protocol 版调用者后续覆盖。
		OutRepTagFindInfo.StateIndex = 0;
		OutRepTagFindInfo.ExternalStateOffset = 0;
		OutRepTagFindInfo.InternalStateAbsoluteOffset = 0;
		const FReplicationStateDescriptor* CurrentDescriptor = Descriptor;
		const FReplicationStateMemberTagDescriptor* CurrentTagDescriptor = &TagDescriptor;
		for (;;)
		{
			// 累加当前层的成员偏移。
			const uint16 MemberIndex = CurrentTagDescriptor->MemberIndex;
			const FReplicationStateMemberDescriptor& MemberDescriptor = CurrentDescriptor->MemberDescriptors[MemberIndex];
			OutRepTagFindInfo.ExternalStateOffset += MemberDescriptor.ExternalMemberOffset;
			OutRepTagFindInfo.InternalStateAbsoluteOffset += MemberDescriptor.InternalMemberOffset;

			const FReplicationStateMemberSerializerDescriptor* SerializerInfo = &CurrentDescriptor->MemberSerializerDescriptors[MemberIndex];
			const uint16 InnerTagIndex = CurrentTagDescriptor->InnerTagIndex;
			if (InnerTagIndex == MAX_uint16)
			{
				// 已到叶子成员：填好 Serializer + Config 直接返回。
				OutRepTagFindInfo.Serializer = SerializerInfo->Serializer;
				OutRepTagFindInfo.SerializerConfig = SerializerInfo->SerializerConfig;
				return true;
			}

			// Since the inner tag index is valid we assume the member is a struct.
			// InnerTagIndex 有效说明当前成员是 Struct，递归进入子 Descriptor。
			if (!ensure(IsUsingStructNetSerializer(*SerializerInfo)))
			{
				return false;
			}

			const FStructNetSerializerConfig* StructConfig = static_cast<const FStructNetSerializerConfig*>(SerializerInfo->SerializerConfig);
			CurrentDescriptor = StructConfig->StateDescriptor;
			CurrentTagDescriptor = &StructConfig->StateDescriptor->MemberTagDescriptors[InnerTagIndex];
		}
	}

	return false;
}

// Protocol 视角：遍历 StateDescriptors[]，调用单 Descriptor 版本，最后把"内部偏移"调整为全协议绝对偏移。
bool FindRepTag(const FReplicationProtocol* Protocol, FRepTag RepTag, FRepTagFindInfo& OutRepTagFindInfo)
{
	check(Protocol);
	uint32 InternalOffset = 0;
	for (const FReplicationStateDescriptor*& StateDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, Protocol->ReplicationStateCount))
	{
		// Iris 内部 buffer 是单个线性分配（按 InternalAlignment 对齐 + InternalSize 累加），
		// 因此把当前 StateDescriptor 的 InternalSize 累加到 InternalOffset 即得到绝对偏移基址。
		InternalOffset = Align(InternalOffset, StateDescriptor->InternalAlignment);
		
		if (FindRepTag(StateDescriptor, RepTag, OutRepTagFindInfo))
		{
			OutRepTagFindInfo.StateIndex = static_cast<uint32>(&StateDescriptor - Protocol->ReplicationStateDescriptors);
			OutRepTagFindInfo.InternalStateAbsoluteOffset += InternalOffset;
			return true;
		}
		
		InternalOffset += StateDescriptor->InternalSize;
	}

	return false;
}

}
