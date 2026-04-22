// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaTimer.h"
#include "UbaPlatform.h"

#include <wchar.h>
#if !PLATFORM_WINDOWS
#include <sys/time.h>
#endif

#define UBA_USE_GETTIMEOFDAY 0

namespace uba
{
	#if !PLATFORM_WINDOWS
	u64 GetMonoticTimeNs()
	{
		#if PLATFORM_MAC
		return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
		#else
		struct timespec ts;
		if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
			FatalError(1401, TC("clock_gettime(CLOCK_MONOTONIC) failed"));
		return u64(ts.tv_sec * 1'000'000'000LL + ts.tv_nsec);
		#endif
	}
	#endif

	u64 GetTime()
	{
	#if PLATFORM_WINDOWS
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		return li.QuadPart;
	#elif UBA_USE_GETTIMEOFDAY
		timeval tv;
		gettimeofday(&tv, NULL); // Returns time in microseconds since 1 Jan 1970
		return u64(tv.tv_sec) * 1'000'000ull + u64(tv.tv_usec);
	#else
		return GetMonoticTimeNs();
	#endif
	}

	u64 GetFrequency()
	{
	#if PLATFORM_WINDOWS
		static u64 frequency = []() { LARGE_INTEGER li; QueryPerformanceFrequency(&li); return li.QuadPart; }();
		return frequency;
	#elif UBA_USE_GETTIMEOFDAY
		return 1000000LL;
	#else
		return 1'000'000'000LL;
		//static u64 frequency = []() { timespec ts; clock_getres(CLOCK_MONOTONIC, &ts); return u64(ts.tv_sec * 1'000'000'000LL + ts.tv_nsec); }();
		//return frequency;
	#endif
	}

	u64 GetSystemTimeUs()
	{
		#if PLATFORM_WINDOWS
		constexpr u64 EPOCH_DIFF = 11'644'473'600ull; // Seconds from 1 Jan. 1601 to 1970 (windows to linux)
		FILETIME st;
		GetSystemTimeAsFileTime(&st);
		return *(u64*)&st / 10 - (EPOCH_DIFF*1'000'000ull);
		#else
		timeval tv;
		gettimeofday(&tv, NULL); // Returns time in microseconds since 1 Jan 1970
		return u64(tv.tv_sec) * 1'000'000ull + u64(tv.tv_usec);
		#endif
	}
}