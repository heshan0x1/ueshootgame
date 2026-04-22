// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

//
// Interface passing custom memory allocator to EpicRtc, note that new and delete still need to be overridden to use it
// See also callstack.h which makes use of hooks in the allocator interface to allow a caller to include epicrtc allocations 
// in it's own tracking system (eg Unreal Insights). A default system is also provided (default_memory_impl.h) to allow 
// tracking and leak detection for anything statically linking epicrtc (eg tests).
//

#include <cstdint>

// Interface for defining custom allocation functions
class EpicRtcMemoryInterface
{
public:
    [[nodiscard]] virtual void* Allocate(uint64_t size, uint64_t alignment, const char* tag) = 0;
    [[nodiscard]] virtual void* Reallocate(void* pointer, uint64_t size, uint64_t alignment, const char* tag) = 0;
    virtual void Free(void* pointer) = 0;
    virtual ~EpicRtcMemoryInterface() = default;
};
