// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// MassProcessingContext.h —— Mass 处理上下文（Executor 入口的"票据"）
// -----------------------------------------------------------------------------
// 该文件定义 UE::Mass::FProcessingContext —— Executor::Run* 系列函数的统一输入：
//   * 携带要操作的 FMassEntityManager（共享引用）；
//   * 携带 DeltaSeconds（驱动 processor 的时间步长）；
//   * 携带 AuxData（FInstancedStruct，用于附带任意类型的辅助数据）；
//   * 持有/创建 FMassCommandBuffer（延迟命令缓冲区）；
//   * 在内部 lazy 地构造 FMassExecutionContext —— 真正给 processor 用的"执行上下文"。
//
// 与 FMassExecutionContext 的关系：
//   * FProcessingContext 是【调用方→Executor】之间的"参数包"，由调用方填好后传入。
//   * FMassExecutionContext 才是【Executor→Processor】之间真正的执行上下文。
//   * FProcessingContext 在第一次 GetExecutionContext() 时，在自己的内嵌缓冲区
//     ExecutionContextBuffer 里 placement-new 出 FMassExecutionContext。
//
// 析构期间会自动把 CommandBuffer 中累积的延迟命令 flush 或 append 到 EntityManager。
// 别名 FMassProcessingContext = UE::Mass::FProcessingContext（见 MassProcessingTypes.h）。
// =============================================================================

#pragma once

#include "MassExecutionContext.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "StructUtils/InstancedStruct.h"


namespace UE::Mass
{
	/**
	 * FProcessingContext —— 处理上下文（Executor 的轻量"票据"）
	 *
	 * 视角：这是【调用方】把"我要让 Mass 跑一波 processor"这件事打包给 Executor 时所用的容器。
	 * 设计要点：
	 *   1) 必须显式提供 FMassEntityManager（操作目标）；默认/拷贝构造在 5.6 版本被废弃。
	 *   2) DeltaSeconds 通过构造函数传入，运行期不可改（getter 唯一读取入口）。
	 *   3) CommandBuffer 在第一次需要时自动创建；调用方也可在构造后、首次 GetExecutionContext 之前
	 *      通过 SetCommandBuffer 注入自定义缓冲。
	 *   4) ExecutionContextBuffer 是预留的对齐字节数组，用 placement-new 构造 FMassExecutionContext —— 
	 *      避免一次堆分配，且生命周期严格绑定 FProcessingContext。
	 *   5) 析构函数会把延迟命令 flush 或 append 回 EntityManager（取决于 bFlushCommandBuffer）。
	 */
	struct FProcessingContext
	{
		// 5.6 版本起：禁用默认/拷贝构造。FProcessingContext 必须显式绑定 EntityManager。
		UE_DEPRECATED_FORGAME(5.6, "This constructor is deprecated. Use one of the other ones.")
		FProcessingContext() = default;
		UE_DEPRECATED_FORGAME(5.6, "This constructor is deprecated. Use one of the other ones.")
		FProcessingContext(const FProcessingContext&) = default;

		// 移动构造允许 —— TriggerParallelTasks 走的是 rvalue 路径（见 MassExecutor.h）
		FProcessingContext(FProcessingContext&&) = default;

		// 主要构造函数族：所有 4 个重载最终都会落到 TSharedRef<FMassEntityManager> 这一份。
		// @param InEntityManager      要操作的 EntityManager（必须有效）
		// @param InDeltaSeconds       本帧/本次执行的时间步长（必须 >= 0；Executor 会 ensure 检查）
		// @param bInFlushCommandBuffer  析构时是否自动 flush 延迟命令（true=立即应用；false=仅 append 到 EntityManager 待后续 flush）
		explicit FProcessingContext(FMassEntityManager& InEntityManager, const float InDeltaSeconds = 0.f, const bool bInFlushCommandBuffer = true);
		explicit FProcessingContext(const TSharedRef<FMassEntityManager>& InEntityManager, const float InDeltaSeconds = 0.f, const bool bInFlushCommandBuffer = true);
		explicit FProcessingContext(TSharedRef<FMassEntityManager>&& InEntityManager, const float InDeltaSeconds = 0.f, const bool bInFlushCommandBuffer = true);
		explicit FProcessingContext(const TSharedPtr<FMassEntityManager>& InEntityManager, const float InDeltaSeconds = 0.f, const bool bInFlushCommandBuffer = true);
		// 析构：若 ExecutionContext 已被构造，则按 bFlushCommandBuffer 决定 flush 或 append CommandBuffer
		MASSENTITY_API ~FProcessingContext();
		
		// 获取（必要时 lazy 创建）内部 FMassExecutionContext。
		// 第一次调用时会：
		//   * 在 ExecutionContextBuffer 上 placement-new 一个 FMassExecutionContext
		//   * 若 CommandBuffer 还未指定，则现场创建一个
		//   * 把 CommandBuffer 装到 ExecutionContext 中并关闭"自动 flush"（由 FProcessingContext 析构时统一处理）
		//   * 把 AuxData 注入 ExecutionContext，并把执行类型标为 Processor
		// 注意：[[nodiscard]] 防止误调用而忽略返回值。
		[[nodiscard]] FMassExecutionContext& GetExecutionContext() &;
		// rvalue 版本：把内部 ExecutionContext"搬走"（用于 TriggerParallelTasks 把上下文转交给 task）。
		// 调用后 ExecutionContextPtr 被置 nullptr，CommandBuffer 也被 reset，析构函数将不再 flush。
		[[nodiscard]] FMassExecutionContext&& GetExecutionContext() &&;
		// 析构时是否会 flush 命令（getter 形式访问 bFlushCommandBuffer）
		bool GetWillFlushCommands() const;
		// 本次处理的时间步长
		float GetDeltaSeconds() const;

		// 注入自定义 CommandBuffer（必须在首次 GetExecutionContext 之前调用，否则 check 失败）
		void SetCommandBuffer(TSharedPtr<FMassCommandBuffer>&& InCommandBuffer);
		void SetCommandBuffer(const TSharedPtr<FMassCommandBuffer>& InCommandBuffer);

		// 获取关联的 EntityManager 共享引用
		const TSharedRef<FMassEntityManager>& GetEntityManager() const;

		// ---- 以下成员变量在 5.6 起均被废弃 "直接访问"，应改用对应的 getter/setter ----
		// 仍然保留是为了向后兼容；在 5.8 计划下沉到 protected 并去除 deprecated 标记（见文件末尾 @todo）

		// 持有要操作的 EntityManager。请用 GetEntityManager() 替代直接访问。
		UE_DEPRECATED_FORGAME(5.6, "Direct access to FProcessingContext.EntityManager has been deprecated. Use GetEntityManager instead.")
		TSharedRef<FMassEntityManager> EntityManager;

		// 时间步长。请通过构造函数设置、通过 GetDeltaSeconds() 读取。
		UE_DEPRECATED_FORGAME(5.6, "Direct access to FProcessingContex.DeltaSeconds has been deprecated. Set it via the constructor instead, read via the getter.")
		float DeltaSeconds = 0.f;

		// 辅助数据：以 FInstancedStruct 形式承载任意 USTRUCT，由调用方/processor 约定语义。
		// 典型用途：observer 触发时把"事件参数"塞进来，让 processor 通过 ExecutionContext.GetAuxData() 读到。
		FInstancedStruct AuxData;

		/** 
		 * If set to "true" the MassExecutor will flush commands at the end of given execution function. 
		 * If "false" the caller is responsible for manually flushing the commands.
		 *
		 * 中文：是否在本 ProcessingContext 析构时自动把 CommandBuffer 应用到 EntityManager。
		 *   true  —— 析构时调用 EntityManager->FlushCommands(CommandBuffer)，立刻应用结构变更
		 *   false —— 仅调用 EntityManager->AppendCommands(CommandBuffer)，把命令并入主缓冲，由调用方择机 flush
		 *           （多 processor 串行 + 统一时机 flush 的场景，例如 Phase 结束时一次性 flush）
		 */
		UE_DEPRECATED_FORGAME(5.6, "Direct access to FProcessingContex.bFlushCommandBuffer has been deprecated. Set it via the constructor instead, read via the GetWillFlushCommands getter.")
		bool bFlushCommandBuffer = true; 

		// 延迟命令缓冲区。第一次 GetExecutionContext 时若为空会自动创建。请用 SetCommandBuffer() 注入。
		UE_DEPRECATED_FORGAME(5.6, "Direct access to FProcessingContext's CommandBuffer has been deprecated. Use SetCommandBuffer()")
		TSharedPtr<FMassCommandBuffer> CommandBuffer;

	protected:
		// 内嵌字节缓冲区：placement-new 用，承载 FMassExecutionContext。
		// 这样把 ExecutionContext 的生命周期 "嵌入" 到 FProcessingContext，省去一次堆分配，
		// 析构时显式调用 ~FMassExecutionContext()（见 .cpp）。
		uint8 ExecutionContextBuffer[sizeof(FMassExecutionContext)];
		// 指向 ExecutionContextBuffer 中已构造对象的指针。nullptr 表示尚未 lazy 构造（或已被 move 走）。
		FMassExecutionContext* ExecutionContextPtr = nullptr;
	};

	//----------------------------------------------------------------------//
	// INLINES —— 内联实现
	//----------------------------------------------------------------------//
	// @todo remove the depracation disabling once CommandBuffer and EntityManager are moved to `protected` and un-deprecated (around UE5.8)
	// 中文：以下区块内部读写 EntityManager / CommandBuffer / bFlushCommandBuffer 这些被废弃直访的成员，
	// 因此包了一层 PRAGMA 关闭 deprecated 警告。等 5.8 把它们改 protected 后即可移除。
	PRAGMA_DISABLE_DEPRECATION_WARNINGS

	// 入口 #1：EntityManager 引用 → 转发到 TSharedRef 版（通过 AsShared() 获取共享引用）
	inline FProcessingContext::FProcessingContext(FMassEntityManager& InEntityManager, const float InDeltaSeconds, const bool bInFlushCommandBuffer)
		: FProcessingContext(/*MoveTemp*/(InEntityManager.AsShared()), InDeltaSeconds, bInFlushCommandBuffer)
	{

	}

	// 入口 #2：TSharedRef const& —— 主路径之一，直接初始化成员
	inline FProcessingContext::FProcessingContext(const TSharedRef<FMassEntityManager>& InEntityManager, const float InDeltaSeconds, const bool bInFlushCommandBuffer)
		: EntityManager(InEntityManager)
		, DeltaSeconds(InDeltaSeconds)
		, bFlushCommandBuffer(bInFlushCommandBuffer)
	{
		
	}

	// 入口 #3：TSharedRef&& —— 移动版本，避免一次引用计数原子操作
	inline FProcessingContext::FProcessingContext(TSharedRef<FMassEntityManager>&& InEntityManager, const float InDeltaSeconds, const bool bInFlushCommandBuffer)
		: EntityManager(MoveTemp(InEntityManager))
		, DeltaSeconds(InDeltaSeconds)
		, bFlushCommandBuffer(bInFlushCommandBuffer)
	{
		
	}

	// 入口 #4：TSharedPtr const& —— 转 SharedRef 后走主路径（要求传入指针非空，由 ToSharedRef 检查）
	inline FProcessingContext::FProcessingContext(const TSharedPtr<FMassEntityManager>& InEntityManager, const float InDeltaSeconds, const bool bInFlushCommandBuffer)
		: FProcessingContext(InEntityManager.ToSharedRef(), InDeltaSeconds, bInFlushCommandBuffer)
	{
		
	}

	// lvalue 版 GetExecutionContext：lazy 构造内部 FMassExecutionContext。
	// 关键步骤：
	//   1) 在 ExecutionContextBuffer 上 placement-new；
	//   2) 若调用方未注入 CommandBuffer，现场创建一个；
	//   3) 把 CommandBuffer 装入 ExecutionContext；
	//   4) 关闭 ExecutionContext 的"自动 flush"（由 FProcessingContext 析构统一处理）；
	//   5) 灌入 AuxData 并把执行类型标记为 Processor —— 这会让 EntityQuery 等知道当前是 processor 阶段，
	//      可以安全使用 ECS 的非线程安全 API（具体由 FMassExecutionContext 决定）。
	inline FMassExecutionContext& FProcessingContext::GetExecutionContext() &
	{
		if (ExecutionContextPtr == nullptr)
		{
			ExecutionContextPtr = new(&ExecutionContextBuffer) FMassExecutionContext(*EntityManager, DeltaSeconds);

			if (CommandBuffer.IsValid() == false)
			{
				CommandBuffer = MakeShareable(new FMassCommandBuffer());
			}

			ExecutionContextPtr->SetDeferredCommandBuffer(CommandBuffer);
			
			ExecutionContextPtr->SetFlushDeferredCommands(false);
			ExecutionContextPtr->SetAuxData(AuxData);
			ExecutionContextPtr->SetExecutionType(EMassExecutionContextType::Processor);
		}
		return *ExecutionContextPtr;
	}

	// rvalue 版 GetExecutionContext：把执行上下文"搬走"。
	// 调用场景：TriggerParallelTasks 把 ExecutionContext 移交给 task graph，FProcessingContext 自身已经"卸载"，
	// 析构时不再 flush 任何东西（因 ExecutionContextPtr 已被置空、CommandBuffer 已被 reset）。
	inline FMassExecutionContext&& FProcessingContext::GetExecutionContext() &&
	{
		// Note: it's fine to store a reference to created execution context
		// while nulling-out the pointer, since the FMassExecutionContext data
		// lives in the ExecutionContextBuffer buffer. Nulling out ExecutionContextPtr
		// only signals that the execution context has been moved out.
		// 中文：把指针置空只是"标记已 move 走"——FMassExecutionContext 的数据本身依然驻留在
		// ExecutionContextBuffer 缓冲区里，调用者拿到的是对该数据的引用 + MoveTemp，安全有效。

		FMassExecutionContext& LocalExecutionContext = GetExecutionContext();
		ExecutionContextPtr = nullptr;
		CommandBuffer.Reset();

		return MoveTemp(LocalExecutionContext);
	}

	// 注入 CommandBuffer（移动版）。要求 ExecutionContext 尚未创建（否则 CommandBuffer 已被绑入其中，
	// 替换会破坏一致性，触发 checkf）。
	inline void FProcessingContext::SetCommandBuffer(TSharedPtr<FMassCommandBuffer>&& InCommandBuffer)
	{
		checkf(ExecutionContextPtr == nullptr, TEXT("Setting command buffer after ExecutionContext creation is not supported"));
		if (ExecutionContextPtr == nullptr)
		{
			CommandBuffer = MoveTemp(InCommandBuffer);
		}
	}

	// 注入 CommandBuffer（拷贝版）。同上约束。
	inline void FProcessingContext::SetCommandBuffer(const TSharedPtr<FMassCommandBuffer>& InCommandBuffer)
	{
		checkf(ExecutionContextPtr == nullptr, TEXT("Setting command buffer after ExecutionContext creation is not supported"));
		if (ExecutionContextPtr == nullptr)
		{
			CommandBuffer = InCommandBuffer;
		}
	}

	inline const TSharedRef<FMassEntityManager>& FProcessingContext::GetEntityManager() const
	{
		return EntityManager;
	}

	inline bool FProcessingContext::GetWillFlushCommands() const
	{
		return bFlushCommandBuffer;
	}

	inline float FProcessingContext::GetDeltaSeconds() const
	{
		return DeltaSeconds;
	}

	PRAGMA_ENABLE_DEPRECATION_WARNINGS
} // namespace UE::Mass
