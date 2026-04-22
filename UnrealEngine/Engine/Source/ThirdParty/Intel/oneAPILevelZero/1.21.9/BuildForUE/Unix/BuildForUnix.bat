@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

set ENGINE_DIR=%CD%\..\..\..\..\..\..\..\

set MAKE_TARGET=libraries_only
set CMAKE_ADDITIONAL_ARGUMENTS=-DBUILD_WITH_LIBCXX=1
set VERSION=1.21.9

echo Creating %MAKE_TARGET% libraries for x86_64-unknown-linux-gnu...
call %ENGINE_DIR%\Build\BatchFiles\RunUAT.sh BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu -TargetLib=Intel\oneAPILevelZero -TargetLibVersion=%VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist

echo Creating %MAKE_TARGET% libraries for aarch64-unknown-linux-gnueabi...
call %ENGINE_DIR%\Build\BatchFiles\RunUAT.sh BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi -TargetLib=Intel\oneAPILevelZero -TargetLibVersion=%VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist


endlocal
