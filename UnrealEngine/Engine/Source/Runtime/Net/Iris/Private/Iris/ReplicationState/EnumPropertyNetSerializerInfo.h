// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// EnumPropertyNetSerializerInfo.h —— Enum 类属性的 NetSerializer Info 跨文件声明
// ---------------------------------------------------------------------------------------------------------------------
// 这里只做"声明"，实现见 EnumPropertyNetSerializerInfo.cpp。
// 之所以要把 Info 单例的访问器跨文件 declare（而不是像普通 Info 那样只在 cpp 中 file-static），
// 是因为：
//   - PropertyNetSerializerInfoRegistry.cpp 中 FPropertyNetSerializerInfoRegistry::Reset 等场景会按 FieldClass
//     间接引用，但更主要的原因是 **DefaultPropertyNetSerializerInfos.cpp 在 RegisterDefaultPropertyNetSerializerInfos**
//     里需要 UE_NET_REGISTER_NETSERIALIZER_INFO(FEnumAsBytePropertyNetSerializerInfo) 等宏，
//     该宏展开后引用 GetPropertyNetSerializerInfo_FEnumAsBytePropertyNetSerializerInfo()，
//     而该函数定义在 EnumPropertyNetSerializerInfo.cpp 中，因此 declare 必须先于 cpp 引用处。
//
// 三个 Info：
//   - FEnumAsBytePropertyNetSerializerInfo —— FByteProperty + Enum 后端（TEnumAsByte<EFoo> 的运行时形式），
//                                             走 FEnumUint8NetSerializer + 由 FByteProperty->Enum 填充 Config；
//   - FEnumPropertyNetSerializerInfo       —— FEnumProperty（Native enum，编译期已定型 underlying type），
//                                             根据 underlying（FByteProperty/FUInt16Property/.../FInt64Property）
//                                             从 8 个 FEnum{U,}{Int8/16/32/64}NetSerializer 中选一个；
//   - FNetRoleNetSerializerInfo            —— 专门处理 ENetRole（Role/RemoteRole）：使用 FNetRoleNetSerializer
//                                             支持服务端/客户端 Role 角色互换的特殊编解码逻辑。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"

namespace UE::Net::Private
{

// 跨文件 declare（cpp 中由 UE_NET_IMPLEMENT_NETSERIALIZER_INFO 实现）
UE_NET_DECLARE_NETSERIALIZER_INFO(FEnumAsBytePropertyNetSerializerInfo)
UE_NET_DECLARE_NETSERIALIZER_INFO(FEnumPropertyNetSerializerInfo);
UE_NET_DECLARE_NETSERIALIZER_INFO(FNetRoleNetSerializerInfo);

}
