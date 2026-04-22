// Copyright Epic Games, Inc. All Rights Reserved.

#if !defined(D3D12_CORE_ENABLED)
	#define D3D12_CORE_ENABLED 0
#endif

// Opt in to new D3D12 redist and tell the loader where to search for D3D12Core.dll.
// The D3D loader looks for these symbol exports in the .exe module.
// We only support this on x64 Windows Desktop platforms. Other platforms or non-redist-aware 
// versions of Windows will transparently load default OS-provided D3D12 library.

#if D3D12_CORE_ENABLED

#include <d3d12.h>

#if !defined(D3D12_SDK_VERSION)
	#error "D3D12_SDK_VERSION not defined, please make sure you have the right version of d3d12.h"
#endif

extern "C"
{
	_declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION;
}
extern "C"
{
	_declspec(dllexport) extern const char* D3D12SDKPath =
#if PLATFORM_CPU_ARM_FAMILY && !PLATFORM_WINDOWS_ARM64EC
		".\\D3D12\\arm64\\";
#else
		".\\D3D12\\x64\\";
#endif // PLATFORM_CPU_ARM_FAMILY && !PLATFORM_WINDOWS_ARM64EC
}
#endif // D3D12_CORE_ENABLED
