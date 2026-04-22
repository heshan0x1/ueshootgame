@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set LIB_ROOT=%~dp0..\..
set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty
set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_CXX_STANDARD=17 -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_INSTALL_PREFIX=%LIB_ROOT% -DCMAKE_INSTALL_LIBDIR=lib\Unix\x86_64-unknown-linux-gnu\Release -DBUILD_SHARED_LIBS=OFF -DABSL_PROPAGATE_CXX_STD=ON -DABSL_MSVC_STATIC_RUNTIME=OFF -DBUILD_WITH_LIBCXX=ON
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu -TargetLib=abseil -TargetLibVersion=20240722.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist -MakeTarget=INSTALL || exit /b

set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_CXX_STANDARD=17 -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_INSTALL_PREFIX=%LIB_ROOT% -DCMAKE_INSTALL_LIBDIR=lib\Unix\aarch64-unknown-linux-gnueabi\Release -DBUILD_SHARED_LIBS=OFF -DABSL_PROPAGATE_CXX_STD=ON -DABSL_MSVC_STATIC_RUNTIME=OFF -DBUILD_WITH_LIBCXX=ON
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi -TargetLib=abseil -TargetLibVersion=20240722.0 -TargetLibSourcePath=%LIB_ROOT%\src -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist -MakeTarget=INSTALL || exit /b
