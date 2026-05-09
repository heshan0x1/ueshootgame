// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================
// 文件作用 (中文说明)
//   FObjectPollFrequencyLimiter: Iris 的“按帧周期筛选轮询对象”模块, 与
//   UObjectReplicationBridge::CallPreSendUpdate 协作, 决定本帧需要把哪些对象交给 Bridge 走属性 Poll。
//
//   设计要点:
//     - 每个对象一个 PollFramePeriod (uint8): 表示 “每 N 帧轮询一次”。
//       上限 256 帧 (uint8 容量), 30Hz 服务器下约 8.5 秒。
//     - FrameCounters[ObjectIndex] (uint8): 当前对象到下次 Poll 还差几帧, 减到 -1 (即 0xFF) 时
//       触发 Poll 并重置为 PollFramePeriod。 (与 Period 0 相当于每帧 Poll 等价。)
//     - PollOffset 错峰: 同一 PollFramePeriod 的对象在添加时通过 FrameIndexOffsets[Period] 自减
//       计数, 让一批同时加入的对象在不同帧被 Poll, 摊平 CPU 峰值。
//     - 主接口 Update(RelevantObjects, DirtyObjects, OutObjectsToPoll):
//         OutObjectsToPoll = (Counter==-1 命中 ∪ DirtyObjects) ∩ RelevantObjects
//       SSE2 / NEON / 标量 三种实现路径, 一次处理 32 / 32 / 32 个对象。
//
//   与 ReplicationSystem.md §7 一致: 帧级轮询节流, 是 Iris 在大量对象时降低单帧 Poll 成本的关键。
// =====================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Net/Core/NetBitArray.h"

namespace UE::Net::Private
{
	typedef uint32 FInternalNetRefIndex;
}

namespace UE::Net::Private
{

class FObjectPollFrequencyLimiter
{
public:
	FObjectPollFrequencyLimiter();

	/** 申请 Counters / FramesBetweenUpdates 数组, 长度向上对齐到 32 (一次处理一个 word)。*/
	void Init(uint32 MaxActiveObjectCount);
	/** 释放内部数组。*/
	void Deinit();

	/** ReplicationSystem 扩容时同步扩展两个数组。 */
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

	/** 设置对象的轮询周期, 同时根据 FrameIndexOffsets 错峰对象在哪一帧被 Poll。 */
	void SetPollFramePeriod(FInternalNetRefIndex InternalIndex, uint8 PollFramePeriod);

	/** 让 InternalIndex 与 ObjectToPollWithInternalIndex 同帧轮询(子对象跟随父对象). */
	void SetPollWithObject(FInternalNetRefIndex ObjectToPollWithInternalIndex, FInternalNetRefIndex InternalIndex);

	/** 
	* Produces the list of objects that should be polled this frame.
	* This list is composed of relevant objects that are dirty or that hit their poll period this frame.
	*
	* (中文) 主入口:
	*   OutObjectsToPoll = (达到 Poll 周期 ∪ DirtyObjects) ∩ RelevantObjects
	* 内部按 32-bit word 批处理, 同时把 FrameCounters 减一并在归零(-1) 时重置为 FramesBetweenUpdates。
	*/
	void Update(const FNetBitArrayView& RelevantObjects, const FNetBitArrayView& DirtyObjects, FNetBitArrayView& OutObjectsToPoll);

	/** We use a uint8 to track frames, so the limit is 255 frames.*/
	/** (中文) PollFramePeriod 上限即 uint8 容量 255。30Hz 下约 8.5 秒。*/
	static constexpr uint32 GetMaxPollingFrames()
	{		
		return static_cast<uint32>(std::numeric_limits<uint8>::max());
	}

private:

	// 当前注册过 SetPollFramePeriod 的最大 InternalIndex (Update 的扫描上限)
	uint32 MaxInternalHandle = 0;
	// 服务器自启动以来的全局帧序号 (仅在 Update 内自增, 不参与正确性, 仅做调试参考)
	uint32 FrameIndex = 0;
	// We store the number of frames between updates as a byte to be able to process 16 objects per instruction.
	// This limits polling to at least every 256th frame. At 30Hz this means every 8.5 seconds.
	// (中文) PollFramePeriod 共享桶: 每个 period 维护一个自减计数器, 用于错峰新对象的初始 FrameCounter,
	//        让同一周期、同时加入的对象不会全在同一帧被 Poll。
	uint8 FrameIndexOffsets[256] = {};
	// 每对象 “两次 Poll 之间的帧数” (= PollFramePeriod). 用 uint8 是为 SSE/NEON 处理 16 字节对齐
	TArray<uint8> FramesBetweenUpdates;
	// 每对象 “距下次 Poll 还差多少帧” 的倒数计数器
	TArray<uint8> FrameCounters;
};


inline void FObjectPollFrequencyLimiter::SetPollFramePeriod(FInternalNetRefIndex InternalIndex, uint8 PollFramePeriod)
{
	MaxInternalHandle = FPlatformMath::Max(MaxInternalHandle, InternalIndex);

	FramesBetweenUpdates[InternalIndex] = PollFramePeriod;
	// Spread the polling of objects with the same frequency so that if you add lots of objects the same frame they won't be polled at the same time. The update loop decrements counters so we need to be careful with how we offset things.
	// (中文) 关键: FrameIndexOffsets[PollFramePeriod] 在每次注册时自减一次, 取得一个错峰偏移量;
	//   ~(FrameIndex + FrameOffset) % (PollFramePeriod + 1) 把初始计数分散到 0..PollFramePeriod 中,
	//   保证同周期对象按帧均匀分散; 因 Update 内是 “先减后判 -1” 的模型, 这里取按位反 (~) 来修正方向。
	const uint8 FrameOffset = --FrameIndexOffsets[PollFramePeriod];
	FrameCounters[InternalIndex] = static_cast<uint8>(uint32(~(FrameIndex + FrameOffset)) % uint32(PollFramePeriod + 1U));
}

inline void FObjectPollFrequencyLimiter::SetPollWithObject(FInternalNetRefIndex ObjectToPollWithInternalIndex, FInternalNetRefIndex InternalIndex)
{
	MaxInternalHandle = FPlatformMath::Max(MaxInternalHandle, InternalIndex);

	// Copy state from object to poll with
	// (中文) 复制周期 + 计数器, 确保子对象与父对象在同一帧被 Poll, 避免相邻 attach 对象间 1 帧抖动
	FramesBetweenUpdates[InternalIndex] = FramesBetweenUpdates[ObjectToPollWithInternalIndex];
	FrameCounters[InternalIndex] = FrameCounters[ObjectToPollWithInternalIndex];
}

}
