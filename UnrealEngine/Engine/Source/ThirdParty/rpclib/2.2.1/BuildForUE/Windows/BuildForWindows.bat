@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=x64 -TargetLib=rpclib -TargetLibVersion=2.2.1 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2019 -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=arm64 -TargetLib=rpclib -TargetLibVersion=2.2.1 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2019 -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS% -DRPCLIB_EXTRA_BUILD_FLAGS=-D__arm64 -DCMAKE_SYSTEM_VERSION=10.0.22621.0" -SkipCreateChangelist || exit /b
