rem @ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

set ENGINE_ROOT=%~dp0..\..\..\..\..
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=arm64 -TargetLib=astcenc -TargetLibVersion=5.0.1 -MakeTarget=astcenc-neon-static -CMakeAdditionalArguments="-DASTCENC_CLI=OFF -DASTCENC_ISA_NEON=ON -DASTCENC_X86_GATHERS=OFF" -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -SkipCreateChangelist || exit /b
