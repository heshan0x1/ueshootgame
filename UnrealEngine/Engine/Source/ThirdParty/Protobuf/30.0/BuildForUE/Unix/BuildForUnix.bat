@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ARCHS=x86_64-unknown-linux-gnu aarch64-unknown-linux-gnueabi

set LIB_ROOT=%~dp0..\..
set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty

SETLOCAL ENABLEDELAYEDEXPANSION
for %%a in (%ARCHS%) do (
		@rem TODO: Build the protobuf binaries separately without libcxx
		
        @rem Using C++17 because of an issue with the current version of clang and #include <version>
        @rem set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_CXX_STANDARD=17 -DCMAKE_INSTALL_PREFIX=%LIB_ROOT% -DCMAKE_INSTALL_LIBDIR=lib\Unix\%%a\Release -DCMAKE_INSTALL_BINDIR=bin\Unix\%%a\Release -DBUILD_SHARED_LIBS=OFF -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF -Dprotobuf_MSVC_STATIC_RUNTIME=OFF -DBUILD_WITH_LIBCXX=ON -Dprotobuf_BUILD_PROTOBUF_BINARIES=ON -Dprotobuf_BUILD_PROTOC_BINARIES=ON -Dprotobuf_BUILD_LIBUPB=OFF
        set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_CXX_STANDARD=20 -DBUILD_SHARED_LIBS=OFF -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF -Dprotobuf_MSVC_STATIC_RUNTIME=OFF -DBUILD_WITH_LIBCXX=ON -Dprotobuf_BUILD_PROTOBUF_BINARIES=ON -Dprotobuf_BUILD_PROTOC_BINARIES=OFF -Dprotobuf_BUILD_LIBPROTOC=ON -Dprotobuf_BUILD_LIBUPB=OFF
        set CMAKE_ABSL_ARGUMENTS=-Dabsl_DIR=%THIRDPARTY_ROOT%\abseil\20240722.0\lib\Unix\%%a\Release\cmake\absl
        set CMAKE_ZLIB_ARGUMENTS=-DZLIB_ROOT=%THIRDPARTY_ROOT%\zlib\1.3\lib\Unix\%%a\Release -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\1.3\include

        @rem Using ninja generator because of issues with command-line length
        call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=%%a -TargetLib=Protobuf -TargetLibVersion=30.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Ninja -CMakeAdditionalArguments="!CMAKE_ADDITIONAL_ARGUMENTS! !CMAKE_ABSL_ARGUMENTS! !CMAKE_ZLIB_ARGUMENTS!" -SkipCreateChangelist -SkipCleanup || exit /b
)
