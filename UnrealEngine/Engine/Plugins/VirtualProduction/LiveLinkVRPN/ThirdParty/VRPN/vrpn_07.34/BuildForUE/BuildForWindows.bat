@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off
set ENGINE_ROOT=%~dp0..\..\..\..\..\..\..
set LIB_VERSION=07.34
set LIB_SRC_ROOT=%ENGINE_ROOT%\Plugins\VirtualProduction\LiveLinkVRPN\ThirdParty\VRPN\vrpn_%LIB_VERSION%\vrpn
set LIB_DST_ROOT=%ENGINE_ROOT%\Plugins\VirtualProduction\LiveLinkVRPN\ThirdParty\VRPN

IF NOT EXIST %LIB_SRC_ROOT% (
    echo please download and unzip https://github.com/vrpn/vrpn/releases/download/version_07.34/vrpn_07.34.zip
    exit /b
)

set CMAKE_EXTRA=-DVRPN_BUILD_SERVER_LIBRARY=OFF -DVRPN_SUBPROJECT_BUILD=OFF -DVRPN_INSTALL=OFF -DBUILD_TESTING=OFF

call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib ^
 	-TargetPlatform=Win64 ^
 	-TargetLib=vrpn ^
 	-TargetLibVersion=%LIB_VERSION% ^
 	-TargetConfigs=Release ^
	-LibOutputPath=Lib ^
 	-TargetArchitecture=x64 ^
 	-CMakeGenerator=VS2019 ^
 	-SkipCreateChangelist ^
 	-TargetLibSourcePath="%LIB_SRC_ROOT%" ^
 	-TargetRootDir="%LIB_DST_ROOT%" ^
	-SkipCreateChangelist ^
	-CMakeAdditionalArguments="%CMAKE_EXTRA%" ^
 	|| exit /b

call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib ^
	-TargetPlatform=Win64 ^
	-TargetLib=vrpn ^
	-TargetLibVersion=%LIB_VERSION% ^
	-TargetConfigs=Release ^
	-LibOutputPath=Lib ^
	-TargetArchitecture=ARM64 ^
	-CMakeGenerator=VS2019 ^
	-SkipCreateChangelist ^
	-TargetLibSourcePath="%LIB_SRC_ROOT%" ^
	-TargetRootDir="%LIB_DST_ROOT%" ^
	-SkipCreateChangelist ^
	-CMakeAdditionalArguments="%CMAKE_EXTRA%" ^
	|| exit /b

