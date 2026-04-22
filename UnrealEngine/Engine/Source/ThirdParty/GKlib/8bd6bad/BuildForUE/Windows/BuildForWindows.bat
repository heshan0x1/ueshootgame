@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=GKlib -TargetLibVersion=8bd6bad -TargetConfigs=Debug+Release -LibOutputPath=lib -TargetArchitecture=x64 -CMakeGenerator=VS2022 -SkipCreateChangelist || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=GKlib -TargetLibVersion=8bd6bad -TargetConfigs=Debug+Release -LibOutputPath=lib -TargetArchitecture=ARM64 -CMakeGenerator=VS2022 -SkipCreateChangelist || exit /b
