@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set GKLIB_VERSION=8bd6bad
set CMAKE_ADDITIONAL_ARGUMENTS=-DGKLIB_PATH="%ENGINE_ROOT%/Source/ThirdParty/GKlib/%GKLIB_VERSION%"
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=metis -TargetLibVersion=5.2.1 -TargetConfigs=Debug+Release -LibOutputPath=lib -TargetArchitecture=x64 -CMakeGenerator=VS2022 -SkipCreateChangelist -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=metis -TargetLibVersion=5.2.1 -TargetConfigs=Debug+Release -LibOutputPath=lib -TargetArchitecture=ARM64 -CMakeGenerator=VS2022 -SkipCreateChangelist -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" || exit /b
