// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/NetSerializer.h"

/**
 * FNetSerializerConfig 的虚析构定义。
 *
 * 为什么在 .cpp 中给一个空实现？
 *  - 虚函数需要有"一个"翻译单元提供实现（否则链接不过），即使体为空；
 *  - 明确 vtable 的发射点（key function），避免重复 vtable 出现在多个调用方单元里，减少代码膨胀；
 *  - 方便未来统一添加 destruction hook（例如调试断言、泄漏跟踪）。
 */
FNetSerializerConfig::~FNetSerializerConfig()
{
}
