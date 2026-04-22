@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set LIB_ROOT=%~dp0..\..
set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty
set "VS_VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"

@rem Release
setlocal
set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_CXX_STANDARD=20 -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DBUILD_SHARED_LIBS=OFF -DABSL_PROPAGATE_CXX_STD=ON -DABSL_MSVC_STATIC_RUNTIME=OFF -DABSL_ENABLE_INSTALL=ON
call "%VS_VCVARS_PATH%" x64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=x64 -TargetLib=abseil -TargetLibVersion=20240722.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=Release-LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Ninja -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist -SkipCleanup || exit /b
call "%VS_VCVARS_PATH%" amd64_arm64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=arm64 -TargetLib=abseil -TargetLibVersion=20240722.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Ninja -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist -SkipCleanup || exit /b
endlocal

@rem Debug
setlocal
set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_CXX_STANDARD=20 -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DBUILD_SHARED_LIBS=OFF -DABSL_PROPAGATE_CXX_STD=ON -DABSL_MSVC_STATIC_RUNTIME=OFF -DABSL_ENABLE_INSTALL=ON -DCMAKE_CXX_FLAGS="/D_ITERATOR_DEBUG_LEVEL=2"
call "%VS_VCVARS_PATH%" x64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=x64 -TargetLib=abseil -TargetLibVersion=20240722.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=Debug -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Ninja -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist -SkipCleanup || exit /b
call "%VS_VCVARS_PATH%" amd64_arm64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=arm64 -TargetLib=abseil -TargetLibVersion=20240722.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=Debug -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Ninja -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist -SkipCleanup || exit /b
endlocal