@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

set ENGINE_DIR=%CD%\..\..\..\..\..\..\..\

set MAKE_TARGET=libraries_only
set VERSION=1.21.9

echo Creating %MAKE_TARGET% libraries for x86_64...
call %ENGINE_DIR%\Build\BatchFiles\RunUAT.bat BuildCMakeLib -TargetPlatform=Win64 -CMakeGenerator=VS2019 -TargetLib=Intel\oneAPILevelZero -TargetLibVersion=%VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist

endlocal
