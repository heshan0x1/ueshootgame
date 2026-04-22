@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set TARGET_CONFIGS=Debug

set LIB_ROOT=%~dp0..\..
set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty
set "VS_VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"

SETLOCAL ENABLEDELAYEDEXPANSION
for %%c in (%TARGET_CONFIGS%) do (
	setlocal
	set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_CXX_STANDARD=20 -DBUILD_SHARED_LIBS=OFF -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=ON -Dprotobuf_MSVC_STATIC_RUNTIME=OFF -Dprotobuf_BUILD_PROTOBUF_BINARIES=ON -Dprotobuf_BUILD_PROTOC_BINARIES=ON -Dprotobuf_BUILD_LIBUPB=ON -Dprotobuf_LOCAL_DEPENDENCIES_ONLY=ON -Dprotobuf_INSTALL=ON
	set CMAKE_ABSL_ARGUMENTS=-Dprotobuf_ABSL_PROVIDER=package -Dabsl_DIR=%THIRDPARTY_ROOT%\abseil\20240722.0\lib\Win64\x64\%%c\cmake\absl
	set CMAKE_ZLIB_ARGUMENTS=-DZLIB_ROOT=%THIRDPARTY_ROOT%\zlib\1.2.12\lib\Win64\Release -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\1.2.12\include
	call "%VS_VCVARS_PATH%" x64
	call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=x64 -TargetLib=Protobuf -TargetLibVersion=30.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=%%c -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Ninja -CMakeAdditionalArguments="!CMAKE_ADDITIONAL_ARGUMENTS! !CMAKE_ABSL_ARGUMENTS! !CMAKE_ZLIB_ARGUMENTS! !CMAKE_ABSL_ARGUMENTS!" -SkipCreateChangelist -SkipCleanup || exit /b
	endlocal
	
	setlocal
	set CMAKE_ABSL_ARGUMENTS=-Dprotobuf_ABSL_PROVIDER=package -Dabsl_DIR=%THIRDPARTY_ROOT%\abseil\20240722.0\lib\Win64\arm64\%%c\cmake\absl
	set CMAKE_ZLIB_ARGUMENTS=-DZLIB_ROOT=%THIRDPARTY_ROOT%\zlib\1.2.12\lib\Win64\arm64 -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\1.2.12\include
	call "%VS_VCVARS_PATH%" amd64_arm64
	call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=arm64 -TargetLib=Protobuf -TargetLibVersion=30.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=%%c -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Ninja -CMakeAdditionalArguments="!CMAKE_ADDITIONAL_ARGUMENTS! !CMAKE_ABSL_ARGUMENTS! !CMAKE_ZLIB_ARGUMENTS! !CMAKE_ABSL_ARGUMENTS!" -SkipCreateChangelist -SkipCleanup || exit /b
	endlocal
)
