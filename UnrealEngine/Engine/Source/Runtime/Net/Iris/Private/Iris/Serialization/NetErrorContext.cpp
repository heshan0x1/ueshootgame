// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// NetErrorContext.cpp
// ---------------------------------------------------------------------------------------------
// Iris 序列化层的"全局错误标识"集合实现。
//
// 设计要点：
//   - Iris 在 (反)序列化过程中遇到错误时，并不抛异常、也不立刻断开连接，而是把错误"打标签"
//     存放到 FNetErrorContext::Error 字段（FName）。一旦设置过错误，本批次后续的所有
//     反序列化都会被短路（IsBitStreamOverflown / HasError 系列检查），最终连接级别的代码
//     再决定是否踢人/重传。
//   - 这里使用 FName 作为错误标识，便于：
//       * 跨翻译单元比较只比较 ComparisonIndex，零成本；
//       * 直接通过名字打日志、上报到 NetTrace；
//       * 上层只需要包含本头文件即可使用，避免引入额外的枚举依赖。
//   - 所有 GNetError_* 全局常量的命名习惯为 `GNetError_<原因>`；新增错误时建议沿用此前缀，
//     方便 grep/统一收集。
//
// 与文档对照：详见 Docs/Modules/Serialization.md §1.3「上下文/错误/日志」章节。
// =============================================================================================

#include "Iris/Serialization/NetErrorContext.h"

namespace UE::Net
{

// ------------------------------------------------------------------------------------------
// GNetError_* —— Iris 标准错误标识表
// ------------------------------------------------------------------------------------------
// 这些 FName 常量在引擎启动时被静态初始化（FName 内部有线程安全的全局名称表），
// 在任意线程通过 == 比较都是 O(1)。新增错误请保持与现有命名风格一致，并同步在
// Docs/Modules/Serialization.md 的错误清单中登记。
// ------------------------------------------------------------------------------------------

/** BitStream 写入/读取越过缓冲区边界（最常见错误，写超容、读越界都归此类）。 */
const FName GNetError_BitStreamOverflow("BitStream overflow");
/** 通用的 BitStream 错误，用于无法用更精确分类描述的位流异常。 */
const FName GNetError_BitStreamError("BitStream error");
/** 反序列化得到的数组长度不合理（超过协议或 MaxExports 等阈值），多用于防御恶意/损坏的包。 */
const FName GNetError_ArraySizeTooLarge("Array size is too large");
/** 收到的 NetHandle 在本端 NetRefHandleManager 中无法解析（可能尚未导出/已销毁）。 */
const FName GNetError_InvalidNetHandle("Invalid NetHandle");
/** NetHandle 的内部状态损坏（如 RefCounting/版本号不一致），属于内部一致性问题。 */
const FName GNetError_BrokenNetHandle("Broken NetHandle");
/** 从位流中解出来的值不在 NetSerializer 允许范围内（例如非法枚举、异常浮点）。 */
const FName GNetError_InvalidValue("Invalid value");
/** 其它内部错误：通常意味着代码 bug 而非网络数据问题，需要打 log 上报。 */
const FName GNetError_InternalError("Internal error");
/** 收到了非法的 DataStream（比如类型 ID 未注册、ChunkType 不在白名单内）。 */
const FName GNetError_InvalidDataStream("Invalid DataStream");

// ------------------------------------------------------------------------------------------
// FNetErrorContext::SetError
// ------------------------------------------------------------------------------------------
// 把错误"粘"到上下文上。三个语义点：
//   1. 一旦设置过错误，后续重复 SetError 会被静默忽略（保留首个错误），便于定位最初的根因；
//   2. 不允许把错误再"清空"（传入 None 会触发 ensure），避免半路抹掉错误导致后续误用脏数据；
//   3. 调用方应在每个序列化函数里通过 HasError() 主动短路，不要依赖异常机制。
// ------------------------------------------------------------------------------------------
void FNetErrorContext::SetError(const FName InError)
{
	// 不允许使用 None 清空错误：意图清错的代码几乎都是 bug。
	ensureMsgf(!InError.IsNone(), TEXT("Clearing an error is not allowed. Error '%s' will remain."), ToCStr(Error.ToString()));

	// 已经处于错误态时直接返回，保留最早出现的错误信息（错误链中的"根因"最有诊断价值）。
	if (HasError())
	{
		return;
	}

	Error = InError;
}

}
