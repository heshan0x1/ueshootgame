// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

//
// Interface to callstack system used for memory tracking and profiling
//

class EpicRtcMemoryInterface;
class EpicRtcStringInterface;

// Flags to identify properties or desired information aquisition for the scope (eg it's a known leak, or record cpu profiling info)
enum class EpicRtcScopeFlag : uint8_t
{
    NONE        = 0,
    DEBUG       = 1 << 0,   // Scope requires investigation for possible leaks
    KNOWN_LEAK  = 1 << 1,   // Scope has known unfixable leaks (normally just static allocations freed at main exits)
    LITERAL     = 1 << 2,   // Scope uses the flag-specific string as it's name
    CPUPROFILE  = 1 << 3,   // Scope is used for cpu profiling
    REPORTED    = 1 << 4,   // Scope has been reported already (used by MemoryTracker to prevent double-reporting of leaks)
};

enum class EpicRtcCallstackType : uint8_t
{
    NONE        = 0,
    MEMORY      = 1 << 0,   // Memory tracking
    CPUPROFILE  = 1 << 1,   // Cpu profiling
};

enum class EpicRtcTagFlag : uint8_t
{
    NONE            = 0,
    REPORTED        = 1 << 0,   // Stats have been reported already (used by MemoryTracker to prevent double-reporting)
    LOCAL_MEM_STATS = 1 << 1,   // Maintain local memory stats for this tag
};

class EpicRtcTagInfo
{
public:
    const EpicRtcTagInfo*  _next;                // All tags are in a global list
    const char*            _name;                // Name for the tag (scope name when tag in use)
    void*                  _localStaticStorage;  // For use by external memory tracker or OnEntry/OnExit callbacks to retain state
    mutable EpicRtcTagFlag _flags;               // Flags for memory tracker to respond to
};

// Convenience operators for flags
inline EpicRtcScopeFlag operator|(EpicRtcScopeFlag a, EpicRtcScopeFlag b)             { return (EpicRtcScopeFlag)(int(a) | int(b)); }
inline EpicRtcCallstackType operator|(EpicRtcCallstackType a, EpicRtcCallstackType b) { return (EpicRtcCallstackType)(int(a) | int(b)); }
inline EpicRtcTagFlag operator|(EpicRtcTagFlag a, EpicRtcTagFlag b)                   { return (EpicRtcTagFlag)(int(a) | int(b)); }
inline int operator&(EpicRtcScopeFlag a, EpicRtcScopeFlag b)                          { return int(a) & int(b); }
inline int operator&(EpicRtcCallstackType a, EpicRtcCallstackType b)                  { return int(a) & int(b); }
inline int operator&(EpicRtcTagFlag a, EpicRtcTagFlag b)                              { return int(a) & int(b); }

class EpicRtcCallstackInterface
{
    public:

    // Optional extry/exit functions used to hook into Unreal LLM
    inline static constexpr uint32_t LOCALSTACK_BYTES = 16; // Amount of stackspace reserved for Entry/Exit functions to hold state for their scope
    
    // Report which types of callstack activity entry/exit functions supported for (for example, just MEMORY if no cpu profiling is wanted)
    virtual EpicRtcCallstackType SupportsEntryExit() const = 0;

    // Upon entry to each scope, method is called with the scope name, whether it's memory/cpuprofile (or both), a pointer to local static void* and stack storage
    virtual void OnEntry(const char* scopeName, EpicRtcCallstackType type, void* volatile* localStatic, uint8_t localStackStorage[LOCALSTACK_BYTES]) = 0;
    
    // Upon exit of each scope, method is called with exact same type and local stack storage that was passed to OnEntry to allow any cleanup
    virtual void OnExit(EpicRtcCallstackType type, uint8_t localStackStorage[LOCALSTACK_BYTES]) = 0;
};

class EpicRtcCallstack
{
    public:

    // Return true if callstack macros are enabled within EpicRtc
    static bool IsEnabled();

    // Assign the memory interface to relay callstack entry/exit's
    static void SetInterface(EpicRtcCallstackInterface* callstackInterface);

    // Get info about current scope for reporting, tagInfo will be non-null if scope has a tag
    static EpicRtcScopeFlag GetScopeInfo(const char** scopeName, const char** flagString, const EpicRtcTagInfo** tagInfo);

    // Get the current tag for memory tracking, returning inNoTagValue if no tag found
    static const char* GetTag(const char* noTagValue = nullptr);

    // Get the taginfo for a given tag name
    static const EpicRtcTagInfo* GetTagInfo(const char* tagName);

    // Generate a json string of cpuprofiler info, by default no indentation or newlines
    static EpicRtcStringInterface* GetCpuProfileInfo(int indent = -1);

    // Get the head of the taginfo list
    static const EpicRtcTagInfo* GetTagInfoHead();
};
