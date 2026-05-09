// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ==========================================================================================
// NetMetrics.h —— Iris 网络度量（Metrics）模块的唯一公共头文件
// ------------------------------------------------------------------------------------------
// 模块定位：L0 基础数据容器层（单头文件，仅依赖 Core）。
// 设计目的：为 Iris 各子系统（NetRefHandleManager / Filtering / Prioritization /
//           DeltaCompression / NetBlob / Stats 等）提供一个"按 FName 键
//           收集整型 / 浮点度量值"的极简容器，便于把运行时指标统一汇总给
//           上层（NetDriver、分析 / 遥测系统）。
// 典型调用方：UReplicationSystem::CollectNetMetrics(FNetMetrics&) ——
//            由外部 Hook 在每帧或按需调用，把本帧收集到的指标交给分析管线。
// 注：本文件只包含"数据类型 + 容器"两层定义，没有对应的 .cpp（全部 inline）。
// ==========================================================================================

#include "HAL/Platform.h"            // 提供 uint32 / int32 等基本整型跨平台别名
#include "Containers/Map.h"          // 提供 TMap，用作 FName → FNetMetric 的底层容器
#include "UObject/NameTypes.h"       // 提供 FName，作为指标键（去重 + 哈希友好）
#include "Misc/AssertionMacros.h"    // 提供 check()，用于类型安全的运行时断言

#include <type_traits>               // std::is_integral_v / is_signed_v / is_floating_point_v —— 模板分发与 static_assert


namespace UE::Net
{

/**
 * Class used to store a single analytics value.
 * Only supports integers or floating points for now.
 *
 * 中文说明：
 *   FNetMetric 是一个"类型安全 union"，用于承载单一指标值。底层通过匿名
 *   union 复用同一块存储来放 uint32 / int32 / double 三种类型之一；另有
 *   一个 EDataType 字段作为"当前到底存的是哪一种"的判别式（tag）。
 *
 * 为什么这样设计：
 *   - 指标值可能来自不同原始类型（计数用 uint32、差值用 int32、比率 / 时长用
 *     double），希望用同一容器收纳而不膨胀体积；
 *   - 避免 TVariant / std::variant 引入的构造析构开销；内部类型都是 POD
 *     且尺寸 ≤ 8B，用裸 union + tag 就够；
 *   - 构造时用 if constexpr 按传入类型分发到对应的成员，调用方零样板代码。
 *
 * 使用约束：
 *   - 只支持整型 / 浮点（模板 static_assert 强制）；
 *   - 整型统一折叠为 32 位有符号 / 无符号两类；传入 int64 / uint64 会被
 *     隐式截断到 32 位 —— 调用方如需宽整型请自行换成 double；
 *   - GetSigned / GetUnsigned / GetDouble 会用 check() 断言当前 tag 正确，
 *     类型误读将在开发期立刻触发断言，不会产生静默损坏。
 */
struct FNetMetric
{
public:
	
	/**
	 * 指标值的数据类型判别式（tag）。
	 *
	 * 语义：
	 *   - None     —— 默认构造后的"空"状态，表示 union 尚未写入任何有效值，
	 *                 调用任何 GetXxx() 都会触发 check()；
	 *   - Unsigned —— 当前 union 中存放 uint32 Unsigned；
	 *   - Signed   —— 当前 union 中存放 int32  Signed；
	 *   - Double   —— 当前 union 中存放 double Double。
	 *
	 * 注意：模块子文档 Docs/Modules/Metrics.md 中列出了 3 个值（Unsigned /
	 * Signed / Double），实际代码比文档多一个 None —— 代表"默认构造后未
	 * 赋值"的空状态。
	 */
	enum class EDataType
	{
		None,     // 空状态：默认构造后的初始 tag，禁止直接读取
		Unsigned, // union.Unsigned 有效（uint32）
		Signed,   // union.Signed   有效（int32）
		Double,   // union.Double   有效（double）
	};

public:

	/**
	 * 默认构造：把 union 清零（写 Double=0.0 会把最宽的 8 字节槽位清 0），
	 * 并把 tag 置为 None，表示"尚未赋值"。
	 * 调用方不应直接对默认构造后的对象调用 GetXxx() —— 会 check 失败。
	 */
	FNetMetric()
		: Double(0.0)                 // 初始化 union 的最宽成员，确保整个存储区为 0
		, DataType(EDataType::None)   // tag 显式置空，GetXxx() 依赖它做合法性检查
	{}

	/**
	 * 模板构造：按传入类型自动分发到合适的 union 槽位与 tag。
	 *
	 * 分发策略（编译期决策，零运行时开销）：
	 *   - 若 T 为有符号整型（int8/int16/int32/int64 等） —— 走 Signed 分支，
	 *     值被隐式转换为 int32 存入 union.Signed；
	 *   - 若 T 为无符号整型（uint8/uint16/uint32/uint64 / bool 等） ——
	 *     走 Unsigned 分支，值被隐式转换为 uint32 存入 union.Unsigned；
	 *   - 若 T 为浮点（float/double/long double） —— 走 Double 分支，
	 *     值被隐式提升到 double 存入 union.Double。
	 *
	 * 关键点：
	 *   - static_assert 在编译期拒绝一切非整型 / 非浮点的 T，避免指针、
	 *     bool* 数组、UObject* 之类被误传；
	 *   - if constexpr 让未命中的分支完全不参与实例化 —— 既避免了错误的
	 *     类型转换警告，也让每个实例化版本只生成一条赋值指令；
	 *   - 注意 int64 / uint64 会被截断为 32 位；64 位计数器请自行归一为
	 *     double 再传入，或在调用方分高低位传两次。
	 *
	 * @param InValue 任意整型 / 浮点的输入值。
	 */
	template<typename T>
	FNetMetric(T InValue)
	{
		// 编译期防线：任何非整型、非浮点类型（如指针、枚举、UObject*）都将在此处被拒绝。
		static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>, "Only integers and floats are supported");

		if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
		{
			// 有符号整型分支：int8/16/32/64 都落到 int32 槽位（可能截断）
			DataType = EDataType::Signed;
			Signed = InValue;
		}
		else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>)
		{
			// 无符号整型分支：uint8/16/32/64、bool 都落到 uint32 槽位（可能截断）
			DataType = EDataType::Unsigned;
			Unsigned = InValue;
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			// 浮点分支：float/double/long double 都提升到 double 槽位
			DataType = EDataType::Double;
			Double = InValue;
		}
	}

	/**
	 * 重置当前 FNetMetric 为新的值（任意支持类型）。
	 *
	 * 实现等价于"再造一个临时 FNetMetric 后整体拷贝回来"——这样可以直接
	 * 复用构造函数里的 if constexpr 分发逻辑，避免重复代码。
	 *
	 * 注意：
	 *   - 这是"覆盖写"语义：无论 T 与原有 DataType 是否一致，都会被替换；
	 *   - 调用后 DataType 一定非 None（除非 T 非法，会被 static_assert 拦下）。
	 */
	template<typename T>
	void Set(T InValue)
	{
		static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>, "Only integers and floats are supported");
		*this = FNetMetric(InValue); // 先构造临时再整体赋值，复用构造函数的分发逻辑
	}

	/** 返回当前存储的类型判别式；调用方据此决定调用 GetSigned/Unsigned/Double 中的哪一个。 */
	EDataType GetDataType() const 
	{ 
		return DataType; 
	}

	/**
	 * 取出有符号整型值。
	 * 前置条件：DataType == Signed，否则 check() 断言失败（开发期崩溃，不会返回脏数据）。
	 */
	int32 GetSigned() const
	{
		check(DataType == EDataType::Signed);
		return Signed;
	}

	/**
	 * 取出无符号整型值。
	 * 前置条件：DataType == Unsigned，否则 check() 断言失败。
	 */
	uint32 GetUnsigned() const
	{
		check(DataType == EDataType::Unsigned);
		return Unsigned;
	}

	/**
	 * 取出浮点值。
	 * 前置条件：DataType == Double，否则 check() 断言失败。
	 */
	double GetDouble() const
	{
		check(DataType == EDataType::Double);
		return Double;
	}

private:

	/**
	 * 匿名 union：三种类型共享同一块存储（按最宽成员 double 对齐为 8 字节）。
	 * 任意时刻只有 DataType 指示的那个成员是"有效值"，其余成员读出都是未定义行为。
	 */
	union
	{
		uint32 Unsigned; // 无符号整型槽位（DataType == Unsigned 时有效）
		int32 Signed;    // 有符号整型槽位（DataType == Signed   时有效）
		double Double;   // 浮点槽位     （DataType == Double   时有效，兼作默认构造清零入口）
	};

	EDataType DataType;  // 判别式：指示当前 union 实际存放的是哪个成员
};

/**
* Collects network metrics and keeps track of their name.
*
* 中文说明：
*   FNetMetrics 是一个"按 FName 键收集 FNetMetric 值"的容器，是 Iris
*   对外暴露的"指标快照袋"。上层（典型是 UReplicationSystem::CollectNetMetrics）
*   把 FNetMetrics& 传给底层子系统，各子系统把自己关心的指标写入其中，
*   最终由调用方统一读取 GetMetrics() 得到 const TMap<FName, FNetMetric>& 快照。
*
* 线程模型：无内部锁，假定所有调用都在同一线程（通常是 Net 刷新线程）内完成。
*
* 生命周期：一般是"一帧一用"——每次收集前由上层 Reset 或新建一个实例，
*          收集结束后交给分析 Hook，立即销毁或被覆盖写。
*
* 关于 EmplaceMetric / AddMetric 的差异（与 Docs/Modules/Metrics.md 注释）：
*   - 模块子文档描述 EmplaceMetric 为"覆盖写"、AddMetric 为"同类型累加"；
*   - 实际代码中两者都只是 TMap::Emplace / TMap::Add 的薄包装 ——
*     都是"同键覆盖写"，并未实现累加逻辑。
*   这是文档与实现的已知不一致点，调用方目前应把两者都当成"覆盖写"使用。
*/
struct FNetMetrics
{
public:

	/**
	 * 以移动语义把一个已构造好的 FNetMetric 放入容器，同键将覆盖原值。
	 * 适合传入临时右值（例如 EmplaceMetric(TEXT("RTT"), FNetMetric(rtt_ms))），
	 * 省掉一次拷贝。
	 */
	void EmplaceMetric(FName InName, FNetMetric&& InMetric)		{ Metrics.Emplace(InName, MoveTemp(InMetric)); }

	/**
	 * 以常引用把 FNetMetric 拷入容器，同键将覆盖原值。
	 * 与 EmplaceMetric 的差别仅在于参数传递方式；语义同样是"覆盖写"。
	 */
	void AddMetric(FName InName, const FNetMetric& InMetric)	{ Metrics.Add(InName, InMetric); }

	/**
	 * 返回内部 TMap 的常引用，供上层遍历并导出到分析 / 遥测通道。
	 * 生存期受 this 限制，调用方不应长期持有。
	 */
	const TMap<FName, FNetMetric>& GetMetrics() const { return Metrics; }

private:

	TMap<FName, FNetMetric> Metrics; // FName → FNetMetric 映射；同键覆盖写；无内部锁
};

} // end namespace UE::Net