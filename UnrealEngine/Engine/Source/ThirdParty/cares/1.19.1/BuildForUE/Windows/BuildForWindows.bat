@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

@rem set ARCHS=x64 arm64
set ARCHS=arm64
set TARGET_CONFIGS=Release Debug

set LIB_ROOT=%~dp0..\..
set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty
set "VS_VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"

SETLOCAL ENABLEDELAYEDEXPANSION
for %%a in (%ARCHS%) do (
	if %%a==arm64 (
		call "%VS_VCVARS_PATH%" amd64_arm64
	) else (
		call "%VS_VCVARS_PATH%" x64
	)
    for %%c in (%TARGET_CONFIGS%) do (
    	setlocal
        set CMAKE_ADDITIONAL_ARGUMENTS=!CMAKE_ADDITIONAL_ARGUMENTS! -DCARES_STATIC=ON -DCARES_SHARED=OFF -DCARES_INSTALL=ON -DCARES_BUILD_TOOLS=OFF
        call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=%%a -TargetLib=cares -TargetLibVersion=1.19.1 -TargetConfigs=%%c -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Ninja -CMakeAdditionalArguments="!CMAKE_ADDITIONAL_ARGUMENTS!" -SkipCreateChangelist -SkipCleanup || exit /b
        endlocal
    )
)
