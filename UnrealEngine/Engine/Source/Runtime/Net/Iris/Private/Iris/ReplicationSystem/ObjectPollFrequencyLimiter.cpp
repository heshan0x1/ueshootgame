// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================
// 文件作用 (中文说明)
//   FObjectPollFrequencyLimiter::Update 的三种实现 (SSE2 / NEON / 标量), 配合 .h 中的成员注释。
//
//   核心算法 (与平台无关):
//     For each 32 个对象 (一个 NetBitArrayView word):
//       1) 若 RelevantObjectsWord == 0   -> 整个 word 中没有相关对象, 直接跳过 (常见: filter 完全关闭)
//       2) Counter -= 1                 -> 8bit 减法, 若发生 0 -> 0xFF 的 borrow 即为 “该对象到时”
//       3) 命中位置 mask -> ObjectsToPoll
//       4) Counter[命中] = FramesBetweenUpdates  -> 重置该对象的下一周期计数
//       5) OutWord = (ObjectsToPoll | DirtyObjects) & RelevantObjects
//
//   SSE2 路径用 _mm_cmpeq_epi8 + _mm_movemask_epi8 一次得到 16-bit mask;
//   NEON 路径通过 vceqzq_u8 + 横向加法 (vaddvq) 拼出 16-bit mask;
//   标量路径手工展开为 4 字节一组, 8 组共 32 字节 = 32 对象。
//
//   说明: counter 的物理含义是 “减到 0xFF 即触发”, 等价 “每 (Period+1) 帧 Poll 一次”。
//         配合 .h 中的 SetPollFramePeriod 错峰逻辑可以让大批量对象的 Poll 在时间维度均匀打散。
// =====================================================================================================

#include "ObjectPollFrequencyLimiter.h"
#include "Iris/Core/IrisProfiler.h"
#include "HAL/IConsoleManager.h"
#include "Math/VectorRegister.h"
#include "Templates/AlignmentTemplates.h"

namespace UE::Net::Private
{

FObjectPollFrequencyLimiter::FObjectPollFrequencyLimiter()
{
}

void FObjectPollFrequencyLimiter::Init(uint32 MaxActiveObjectCount)
{
	// We want to be able to easily update 32 objects at a time.
	// 向上对齐到 32, 让 Update 中的 word 循环不需要边界判断
	const uint32 StorageObjectCount = Align(MaxActiveObjectCount, 32U);

	// Allocate max amount of memory required.
	// 两个数组同长, 索引方式与 ObjectIndex 完全一致 (PollFramePeriod / 当前倒计数)
	FramesBetweenUpdates.SetNumZeroed(StorageObjectCount);
	FrameCounters.SetNumZeroed(StorageObjectCount);
}

void FObjectPollFrequencyLimiter::Deinit()
{
	FramesBetweenUpdates.Empty();
	FrameCounters.Empty();
}

void FObjectPollFrequencyLimiter::OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex)
{
	const uint32 StorageObjectCount = Align(NewMaxInternalIndex, 32U);

	FramesBetweenUpdates.SetNumZeroed(StorageObjectCount);
	FrameCounters.SetNumZeroed(StorageObjectCount);
}

void FObjectPollFrequencyLimiter::Update(const FNetBitArrayView& RelevantObjects, const FNetBitArrayView& DirtyObjects, FNetBitArrayView& OutObjectsToPoll)
{
	IRIS_PROFILER_SCOPE(ObjectPollFrequencyLimiter_Update);

	typedef FNetBitArrayView::StorageWordType WordType;
	constexpr uint32 WordBitCount = FNetBitArrayView::WordBitCount;

	if (!MaxInternalHandle)
	{
		// 还没有任何对象注册过 PollFramePeriod, 不需要任何处理
		return;
	}

	++FrameIndex;

	uint8* CountersData = FrameCounters.GetData();
	const uint8* FramesBetweenUpdatesData = FramesBetweenUpdates.GetData();

	const WordType* RelevantObjectsData = RelevantObjects.GetData();
	const WordType* DirtyObjectsData = DirtyObjects.GetData();
	WordType* ObjectsToPollData = OutObjectsToPoll.GetData();

	// Decrease counters by one and if they wrap around it's time to poll.
	// Algorithm can easily be throttled to only allow at most N objects to be polled per frame.
	// (中文) 三个等价实现, 顺序: x86 SSE2 / ARM NEON / 标量
#if PLATFORM_ENABLE_VECTORINTRINSICS == 1 && !PLATFORM_ENABLE_VECTORINTRINSICS_NEON
	{
		// 一次处理 32 个对象 (两条 128-bit 向量, 各 16 字节即 16 对象)
		static_assert(WordBitCount == 32U, "Code assumes a NetBitArray word size of 32 bits.");

		const VectorRegisterInt AllBitsSet = _mm_cmpeq_epi32(_mm_setzero_si128(), _mm_setzero_si128());
		const VectorRegisterInt Zero = _mm_setzero_si128();
		const VectorRegisterInt One = _mm_set1_epi8(char(1));

		for (uint32 ObjectIndex = 0, ObjectEndIndex = MaxInternalHandle;
			ObjectIndex <= ObjectEndIndex; 
			ObjectIndex += WordBitCount, CountersData += WordBitCount, FramesBetweenUpdatesData += WordBitCount, ++RelevantObjectsData, ++DirtyObjectsData, ++ObjectsToPollData)
		{
			// Skip ranges with no scopable objects.
			// 整个 word 内无相关对象 -> 跳过 (Counter 也不递减, 等到该对象再次相关时再开始计时)
			const WordType ObjectsInScopeWord = *RelevantObjectsData;
			if (!ObjectsInScopeWord)
			{
				continue;
			}

			// 加载 32 个 counter 与 32 个 PollFramePeriod
			VectorRegisterInt Values0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(CountersData + 0U));
			VectorRegisterInt Values1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(CountersData + 16U));

			const VectorRegisterInt FramesBetweenUpdates0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(FramesBetweenUpdatesData + 0U));
			const VectorRegisterInt FramesBetweenUpdates1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(FramesBetweenUpdatesData + 16U));

			// counter -= 1; 减完恰好 == 0xFF (即 AllBitsSet) 的位置代表 “到时” -> 命中
			Values0 = _mm_sub_epi8(Values0, One);
			const VectorRegisterInt MaskToUpdate0 = _mm_cmpeq_epi8(Values0, AllBitsSet);
			// 命中位置: 用对应 PollFramePeriod 重置 counter; 未命中: 保持递减后的值
			Values0 = VectorIntSelect(MaskToUpdate0, FramesBetweenUpdates0, Values0);

			Values1 = _mm_sub_epi8(Values1, One);
			const VectorRegisterInt MaskToUpdate1 = _mm_cmpeq_epi8(Values1, AllBitsSet);
			Values1 = VectorIntSelect(MaskToUpdate1, FramesBetweenUpdates1, Values1);

			// 把 16 字节比较结果压成 16-bit mask
			const uint32 ObjectsToPoll0 = _mm_movemask_epi8(MaskToUpdate0);
			const uint32 ObjectsToPoll1 = _mm_movemask_epi8(MaskToUpdate1);

			// 写回更新后的 counter
			_mm_storeu_si128(reinterpret_cast<__m128i*>(CountersData + 0U), Values0);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(CountersData + 16U), Values1);

			// Poll objects that have been set to dirty or are due.
			// 最终 Poll 集合: (周期到时 ∪ 脏) ∩ 相关
			const WordType ObjectsToPoll = (ObjectsToPoll1 << 16U) | ObjectsToPoll0;
			const WordType DirtyObjectWord = *DirtyObjectsData;
			*ObjectsToPollData = (ObjectsToPoll | DirtyObjectWord) & ObjectsInScopeWord;
		}
	}
#elif PLATFORM_ENABLE_VECTORINTRINSICS == 1 && PLATFORM_ENABLE_VECTORINTRINSICS_NEON
	{
		// NEON 等价实现: 用 vceqzq_u8 找出 “减一前等于 0 的字节” (即将变为 0xFF 的位置),
		// 然后用 mask + 移位 + 横向加法 (vaddvq_u16) 拼出 16-bit ObjectsToPoll 掩码。
		const uint16x8_t AndMask = vdupq_n_u16(0x0180U);
		const uint8x16_t AllBitsSet = vdupq_n_u8(255U);
		const int16_t ShiftAmountsData[8] = { -7, -5, -3, -1, 1, 3, 5, 7 };
		const int16x8_t ShiftAmounts = vld1q_s16(ShiftAmountsData);

		for (uint32 ObjectIndex = 0, ObjectEndIndex = MaxInternalHandle;
			ObjectIndex <= ObjectEndIndex;
			ObjectIndex += WordBitCount, CountersData += WordBitCount, FramesBetweenUpdatesData += WordBitCount, ++RelevantObjectsData, ++DirtyObjectsData, ++ObjectsToPollData)
		{
			// Skip ranges with no scopable objects.
			const WordType ObjectsInScopeWord = *RelevantObjectsData;
			if (!ObjectsInScopeWord)
			{
				continue;
			}

			uint8x16_t Values0 = vld1q_u8(CountersData + 0U);
			uint8x16_t Values1 = vld1q_u8(CountersData + 16U);

			const uint8x16_t FramesBetweenUpdates0 = vld1q_u8(FramesBetweenUpdatesData + 0U);
			const uint8x16_t FramesBetweenUpdates1 = vld1q_u8(FramesBetweenUpdatesData + 16U);

			// MaskToUpdate = (Values == 0): 对应 “减一即将变 0xFF” 的对象, 即本帧到时
			uint8x16_t MaskToUpdate0 = vceqzq_u8(Values0);
			Values0 = vaddq_u8(Values0, AllBitsSet);
			Values0 = vbslq_u8(MaskToUpdate0, FramesBetweenUpdates0, Values0);

			uint8x16_t MaskToUpdate1 = vceqzq_u8(Values1);
			Values1 = vaddq_u8(Values1, AllBitsSet);
			Values1 = vbslq_u8(MaskToUpdate1, FramesBetweenUpdates1, Values1);

			// Create bitmasks via masking and horizontal add.
			uint16x8_t MiddleBits0 = vandq_u16(vreinterpretq_u16_u8(MaskToUpdate0), AndMask);
			uint16x8_t MiddleBits1 = vandq_u16(vreinterpretq_u16_u8(MaskToUpdate1), AndMask);

			uint16x8_t BitsInPlace0 = vshlq_u16(MiddleBits0, ShiftAmounts);
			uint16x8_t BitsInPlace1 = vshlq_u16(MiddleBits1, ShiftAmounts);

			const WordType ObjectsToPoll0 = vaddvq_u16(BitsInPlace0);
			const WordType ObjectsToPoll1 = vaddvq_u16(BitsInPlace1);

			vst1q_u8(CountersData + 0U, Values0);
			vst1q_u8(CountersData + 16U, Values1);

			const WordType ObjectsToPoll = ObjectsToPoll0 | (ObjectsToPoll1 << 16U);
			const WordType DirtyObjectWord = *DirtyObjectsData;
			*ObjectsToPollData = (ObjectsToPoll | DirtyObjectWord) & ObjectsInScopeWord;
		}
	}
#else
	// Slower path using scalar integer instructions.
	// (中文) 标量回退路径: 把 32 个对象按 4 字节一组、共 8 组手工展开。
	// 与向量版语义完全一致, 仅用于不支持 SSE2 / NEON 的目标。
	{
		for (uint32 ObjectIndex = 0, ObjectEndIndex = MaxInternalHandle;
			ObjectIndex <= ObjectEndIndex;
			ObjectIndex += WordBitCount, CountersData += WordBitCount, FramesBetweenUpdatesData += WordBitCount, ++RelevantObjectsData, ++DirtyObjectsData, ++ObjectsToPollData)
		{
			// Skip ranges with no scopable objects.
			WordType ObjectsInScopeWord = *RelevantObjectsData;
			if (!ObjectsInScopeWord)
			{
				continue;
			}

			WordType ObjectsToPoll = 0;
			WordType ObjectsInScopeMask = ObjectsInScopeWord;
			for (SIZE_T It = 0, IndexOffset = 0; It < 8; ++It, IndexOffset += 4U, ObjectsInScopeMask >>= 4U)
			{
				// 当前 4 个对象都不在 scope 内则跳过, 注意此处 *不* 减少 counter
				if (!(ObjectsInScopeMask & 15U))
				{
					continue;
				}

				uint8 Counter0 = CountersData[IndexOffset + 0];
				uint8 Counter1 = CountersData[IndexOffset + 1];
				uint8 Counter2 = CountersData[IndexOffset + 2];
				uint8 Counter3 = CountersData[IndexOffset + 3];

				const uint8 FramesBetweenUpdates0 = FramesBetweenUpdatesData[IndexOffset + 0];
				const uint8 FramesBetweenUpdates1 = FramesBetweenUpdatesData[IndexOffset + 1];
				const uint8 FramesBetweenUpdates2 = FramesBetweenUpdatesData[IndexOffset + 2];
				const uint8 FramesBetweenUpdates3 = FramesBetweenUpdatesData[IndexOffset + 3];

				// 减一; 若变成 255 (即原来是 0) 则 “到时”
				--Counter0;
				--Counter1;
				--Counter2;
				--Counter3;

				const uint32 Mask0 = Counter0 == 255 ? 1 : 0;
				const uint32 Mask1 = Counter1 == 255 ? 2 : 0;
				const uint32 Mask2 = Counter2 == 255 ? 4 : 0;
				const uint32 Mask3 = Counter3 == 255 ? 8 : 0;

				// 命中: 用 PollFramePeriod 重置, 未命中: 保持递减后的值
				Counter0 = Mask0 ? FramesBetweenUpdates0 : Counter0;
				Counter1 = Mask1 ? FramesBetweenUpdates1 : Counter1;
				Counter2 = Mask2 ? FramesBetweenUpdates2 : Counter2;
				Counter3 = Mask3 ? FramesBetweenUpdates3 : Counter3;

				const uint32 Mask = Mask3 | Mask2 | Mask1 | Mask0;
				ObjectsToPoll |= (Mask << IndexOffset);

				CountersData[IndexOffset + 0] = Counter0;
				CountersData[IndexOffset + 1] = Counter1;
				CountersData[IndexOffset + 2] = Counter2;
				CountersData[IndexOffset + 3] = Counter3;
			}

			const WordType DirtyObjectWord = *DirtyObjectsData;
			*ObjectsToPollData = (ObjectsToPoll | DirtyObjectWord) & ObjectsInScopeWord;
		}
	}
#endif
}

}
