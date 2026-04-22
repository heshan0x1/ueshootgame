@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ENGINE_ROOT=%~dp0..\..\..\..\..\..\..\..\..
set LIBWEBM_VERSION=1.0.0.27
set LIBWEBM_ROOT=%ENGINE_ROOT%\Plugins\Media\WebMMedia\Source\ThirdParty\webm\%LIBWEBM_VERSION%
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=webm -TargetLibVersion=%LIBWEBM_VERSION% -TargetConfigs=Release -LibOutputPath=lib -TargetArchitecture=ARM64 -CMakeGenerator=VS2022 -SkipCreateChangelist -TargetLibSourcePath="%LIBWEBM_ROOT%" -TargetRootDir="%ENGINE_ROOT%\Plugins\Media\WebMMedia\Source\ThirdParty\webm\%LIBWEBM_VERSION%" || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=webm -TargetLibVersion=%LIBWEBM_VERSION% -TargetConfigs=Release -LibOutputPath=lib -TargetArchitecture=x64 -CMakeGenerator=VS2022 -SkipCreateChangelist -TargetLibSourcePath="%LIBWEBM_ROOT%" -TargetRootDir="%ENGINE_ROOT%\Plugins\Media\WebMMedia\Source\ThirdParty\webm\%LIBWEBM_VERSION%" || exit /b
