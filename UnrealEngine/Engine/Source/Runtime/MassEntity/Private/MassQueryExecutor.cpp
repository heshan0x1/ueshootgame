// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassQueryExecutor.h"
#include "MassExecutionContext.h"

namespace UE::Mass
{

// 【常规构造】绑定到一个已有的 query；可选指定 log 归属对象（用于 visual log）
FQueryExecutor::FQueryExecutor(FMassEntityQuery& InQuery, UObject* InLogOwner)
	: BoundQuery(&InQuery)
	, LogOwner(InLogOwner)
{
}

// 【兜底 query】FQueryExecutor 的 BoundQuery 是 TNotNull，不能为空；
// 默认构造路径下用这个全局的 dummy query 占位，避免空指针。
// 注意：DummyQuery 是 static，因此所有 default-constructed FQueryExecutor 都共享同一个，
// 这只在"executor 还没真正绑定到目标 query"的过渡阶段使用。
FMassEntityQuery FQueryExecutor::DummyQuery;

// 【默认构造】用 DummyQuery 占位。框架内部使用，外部代码应优先用 CreateQuery 工厂。
FQueryExecutor::FQueryExecutor()
	: BoundQuery(&DummyQuery)
{
}

// 【框架统一执行入口】由 UMassProcessor 在派发 AutoExecuteQuery 时调用。
//   流程：
//     1) ValidateAccessors（debug only）：确保 AccessorsPtr 仍然指向 this 内部的 FQueryDefinition 成员。
//     2) AccessorsPtr->SetupForExecute：让所有 accessor 拉取 Execute 级别的引用（subsystem 等）。
//     3) Execute(Context)：调用派生类的业务逻辑。
//   注意：chunk 级 accessor 绑定（fragment view）发生在更内层的 ForEachEntityChunk lambda 中。
void FQueryExecutor::CallExecute(FMassExecutionContext& Context)
{
#if WITH_MASSENTITY_DEBUG
	ValidateAccessors();
#endif

	AccessorsPtr->SetupForExecute(Context);

	Execute(Context);
}

// 【ConfigureQuery 桥接】把虚函数 ConfigureQuery 转交给 FQueryDefinition<...>，
// 后者通过变参展开把所有 accessor 的需求注入到 BoundQuery + ProcessorRequirements。
// 调用时机：UMassProcessor::ConfigureQueries。
void FQueryExecutor::ConfigureQuery(FMassSubsystemRequirements& ProcessorRequirements)
{
	AccessorsPtr->ConfigureQuery(*BoundQuery, ProcessorRequirements);
}

#if WITH_MASSENTITY_DEBUG
// 【调试期约束验证】
// AccessorsPtr 必须指向 派生 FQueryExecutor 内部的某个 FQueryDefinition 成员变量。
// 通过比较 [this, this+DebugSize] 的地址范围来检测。
//
// 注意：当前实现里有一个看起来像 bug 的地方 —— AccessorsStart 计算的是 (uintptr_t)this 而非
// (uintptr_t)AccessorsPtr，所以这条 check 实际只能保证 this 在自身范围内（永真），
// 没法真正验证 accessors 是不是 this 的成员。看上去像是笔误，应当是 (uintptr_t)AccessorsPtr。
void FQueryExecutor::ValidateAccessors()
{
	const uintptr_t ExecutorStart = (uintptr_t)this;
	const uintptr_t ExecutorEnd = ExecutorStart + DebugSize;

	if (AccessorsPtr)
	{
		const uintptr_t AccessorsStart = (uintptr_t)this; // 【疑似 bug】应为 (uintptr_t)AccessorsPtr
		checkf(ExecutorStart <= AccessorsStart && AccessorsStart <= ExecutorEnd, TEXT("Accessors assigned to a FQueryExecutor must be member variables of that struct."));
	}
};
#endif //WITH_MASSENTITY_DEBUG

} // namespace UE::Mass