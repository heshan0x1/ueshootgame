// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "NetSerializerConfig.generated.h"

/**
 * ENetSerializerConfigTraits —— 每个具体 Serializer Config 的编译期/运行期标志位。
 *
 *  - None：默认，POD 风格 Config，不需要析构调用。
 *  - NeedDestruction：Config 含非平凡析构（如含 TArray / TMap / 智能指针）——Iris 在释放时会调用虚析构函数。
 *    如果设置了这个 Trait 但基类 ~FNetSerializerConfig() 未被正确虚拟化调用，会造成内存泄漏。
 *
 * 扩展说明：未来如需更多元信息（例如"跨帧可共享"、"只读"等），可在此枚举追加位。
 */
enum class ENetSerializerConfigTraits : uint32
{
	None = 0,
	NeedDestruction = 1 << 0,
};
ENUM_CLASS_FLAGS(ENetSerializerConfigTraits);

/**
 * FNetSerializerConfig —— 所有 NetSerializer 配置结构体的基类。
 *
 * 为什么是 USTRUCT？
 *  - 可以在 UClass / UScriptStruct 属性元数据（metadata）里直接引用子类 Config（通过 UPROPERTY 挂接）；
 *  - 让 UHT 能够生成反射信息，便于编辑器端 GUI 或蓝图配置；
 *  - 允许序列化默认实例（比如 FIntRangeNetSerializerConfig 的 LowerBound/UpperBound）。
 *
 * 为什么有虚析构？
 *  - 少数 Config 含非平凡成员（见 ENetSerializerConfigTraits::NeedDestruction），Iris 通过基类指针管理时需要多态析构；
 *  - 基类虚析构会让子类自动获得 vtable——注意这会让 sizeof(Config) 增加指针大小，但 Iris 框架从设计上能够容忍。
 *
 * 使用约定：
 *  1. 每个 Serializer 都应声明 "FXxxNetSerializerConfig : public FNetSerializerConfig"；
 *  2. 可以添加 UPROPERTY 字段；
 *  3. 若含非 POD 成员，构造函数中设置 ConfigTraits |= NeedDestruction；
 *  4. 推荐在 Serializer 内部声明 `inline static const ConfigType DefaultConfig;` 以便无需外部显式传 Config 即可使用。
 */
USTRUCT()
struct FNetSerializerConfig
{
	GENERATED_BODY()

	/** 默认构造：ConfigTraits 清零，Config 默认按 POD 处理。 */
	FNetSerializerConfig()
	: ConfigTraits(ENetSerializerConfigTraits::None)
	{}

	/** 虚析构——保证通过基类指针 delete 子类时能正确销毁。定义在 NetSerializer.cpp。 */
	IRISCORE_API virtual ~FNetSerializerConfig();

	/** Config 的元信息标志位（见 ENetSerializerConfigTraits）。子类按需在构造函数里追加。 */
	ENetSerializerConfigTraits ConfigTraits;
};
