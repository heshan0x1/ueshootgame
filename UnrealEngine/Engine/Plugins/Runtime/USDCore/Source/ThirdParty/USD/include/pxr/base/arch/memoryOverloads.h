#ifndef PXR_BASE_ARCH_MEMORY_OVERLOADS_H
#define PXR_BASE_ARCH_MEMORY_OVERLOADS_H

#include "pxr/pxr.h"

#ifdef PXR_RUNTIME_MEMORY_OVERLOADS_ENABLED

#include "pxr/base/arch/api.h"
#include <cstdlib>
#include <new>

PXR_NAMESPACE_OPEN_SCOPE

typedef void* (*MallocFuncType)(std::size_t, std::size_t);
typedef void (*FreeFuncType)(void*);

struct MemoryOverloads
{
    static ARCH_API void InitMemoryOverrides();

    static ARCH_API void* Malloc(std::size_t size, std::size_t alignment);
    static ARCH_API void Free(void* ptr);
    static ARCH_API void RegisterMallocFree(MallocFuncType mallocFunc, FreeFuncType freeFunc);

private:
    static MallocFuncType mallocFunction;
    static FreeFuncType freeFunction;
};

PXR_NAMESPACE_CLOSE_SCOPE

#if !defined(ARCH_COMPILER_MSVC)
    #if __has_feature(cxx_noexcept)
        #define OPERATOR_NEW_THROW_SPEC noexcept(false)
    #else
        #define OPERATOR_NEW_THROW_SPEC throw(std::bad_alloc)
    #endif
    #define OPERATOR_DELETE_THROW_SPEC noexcept(true)
    #define OPERATOR_NEW_NOTHROW_SPEC noexcept(true)
    #define OPERATOR_DELETE_NOTHROW_SPEC noexcept(true)
#endif // !defined(ARCH_COMPILER_MSVC)

#ifndef OPERATOR_NEW_THROW_SPEC
    #define OPERATOR_NEW_THROW_SPEC
#endif
#ifndef OPERATOR_DELETE_THROW_SPEC
    #define OPERATOR_DELETE_THROW_SPEC
#endif
#ifndef OPERATOR_NEW_NOTHROW_SPEC
    #define OPERATOR_NEW_NOTHROW_SPEC throw()
#endif
#ifndef OPERATOR_DELETE_NOTHROW_SPEC
    #define OPERATOR_DELETE_NOTHROW_SPEC throw()
#endif

#define OPERATOR_NEW_AND_DELETE_OVERLOADS \
void* operator new  (std::size_t size) OPERATOR_NEW_THROW_SPEC { return PXR_NS::MemoryOverloads::Malloc(size ? size : 1, __STDCPP_DEFAULT_NEW_ALIGNMENT__); } \
void* operator new[](std::size_t size) OPERATOR_NEW_THROW_SPEC { return PXR_NS::MemoryOverloads::Malloc(size ? size : 1, __STDCPP_DEFAULT_NEW_ALIGNMENT__); } \
void* operator new  (std::size_t size, const std::nothrow_t&) OPERATOR_NEW_NOTHROW_SPEC { return PXR_NS::MemoryOverloads::Malloc(size ? size : 1, __STDCPP_DEFAULT_NEW_ALIGNMENT__); } \
void* operator new[](std::size_t size, const std::nothrow_t&) OPERATOR_NEW_NOTHROW_SPEC { return PXR_NS::MemoryOverloads::Malloc(size ? size : 1, __STDCPP_DEFAULT_NEW_ALIGNMENT__); } \
void* operator new  (std::size_t size, std::align_val_t alignment) OPERATOR_NEW_THROW_SPEC { return PXR_NS::MemoryOverloads::Malloc(size ? size : 1, (std::size_t)alignment); } \
void* operator new[](std::size_t size, std::align_val_t alignment) OPERATOR_NEW_THROW_SPEC { return PXR_NS::MemoryOverloads::Malloc(size ? size : 1, (std::size_t)alignment); } \
void* operator new  (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) OPERATOR_NEW_NOTHROW_SPEC { return PXR_NS::MemoryOverloads::Malloc(size ? size : 1, (std::size_t)alignment); } \
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) OPERATOR_NEW_NOTHROW_SPEC { return PXR_NS::MemoryOverloads::Malloc(size ? size : 1, (std::size_t)alignment); } \
void operator delete  (void* ptr) OPERATOR_DELETE_THROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete[](void* ptr) OPERATOR_DELETE_THROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete  (void* ptr, const std::nothrow_t&) OPERATOR_DELETE_NOTHROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete[](void* ptr, const std::nothrow_t&) OPERATOR_DELETE_NOTHROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete  (void* ptr, std::size_t Size) OPERATOR_DELETE_THROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete[](void* ptr, std::size_t Size) OPERATOR_DELETE_THROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete  (void* ptr, std::size_t Size, const std::nothrow_t&) OPERATOR_DELETE_NOTHROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete[](void* ptr, std::size_t Size, const std::nothrow_t&) OPERATOR_DELETE_NOTHROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete  (void* ptr, std::align_val_t alignment) OPERATOR_DELETE_THROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete[](void* ptr, std::align_val_t alignment) OPERATOR_DELETE_THROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete  (void* ptr, std::align_val_t alignment, const std::nothrow_t&) OPERATOR_DELETE_NOTHROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete[](void* ptr, std::align_val_t alignment, const std::nothrow_t&) OPERATOR_DELETE_NOTHROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete  (void* ptr, std::size_t Size, std::align_val_t alignment) OPERATOR_DELETE_THROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete[](void* ptr, std::size_t Size, std::align_val_t alignment) OPERATOR_DELETE_THROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete  (void* ptr, std::size_t Size, std::align_val_t alignment, const std::nothrow_t&) OPERATOR_DELETE_NOTHROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); } \
void operator delete[](void* ptr, std::size_t Size, std::align_val_t alignment, const std::nothrow_t&) OPERATOR_DELETE_NOTHROW_SPEC { PXR_NS::MemoryOverloads::Free(ptr); }

#else

#define OPERATOR_NEW_AND_DELETE_OVERLOADS

#endif // PXR_RUNTIME_MEMORY_OVERLOADS_ENABLED

#endif // PXR_BASE_ARCH_MEMORY_OVERLOADS_H
