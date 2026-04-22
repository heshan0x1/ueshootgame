@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set LIB_ROOT=%~dp0..\..
set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty
set CMAKE_ADDITIONAL_ARGUMENTS=-DCARES_STATIC=ON -DCARES_SHARED=OFF -DCARES_INSTALL=ON -DCARES_BUILD_TOOLS=OFF -DCMAKE_INSTALL_PREFIX=%LIB_ROOT% -DBUILD_WITH_LIBCXX=ON
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu -TargetLib=cares -TargetLibVersion=1.19.1 -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist -MakeTarget=INSTALL || exit /b

set CMAKE_ADDITIONAL_ARGUMENTS=-DCARES_STATIC=ON -DCARES_SHARED=OFF -DCARES_INSTALL=ON -DCARES_BUILD_TOOLS=OFF -DCMAKE_INSTALL_PREFIX=%LIB_ROOT% -DBUILD_WITH_LIBCXX=ON
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi -TargetLib=cares -TargetLibVersion=1.19.1 -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist -MakeTarget=INSTALL || exit /b
