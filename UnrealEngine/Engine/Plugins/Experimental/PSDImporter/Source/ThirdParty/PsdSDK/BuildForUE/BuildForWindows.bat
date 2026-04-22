@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off
set ENGINE_ROOT=%~dp0..\..\..\..\..\..\..
set LIB_VERSION=master
set LIB_SRC_ROOT=%ENGINE_ROOT%\Plugins\Experimental\PSDImporter\Source\ThirdParty\PsdSDK\BuildForUE\psd_sdk-%LIB_VERSION%\src\psd
set LIB_DST_ROOT=%ENGINE_ROOT%\Plugins\Experimental\PSDImporter\Source\ThirdParty\PsdSDK

IF NOT EXIST %LIB_SRC_ROOT% (
    echo please download and unzip psd_sdk-%LIB_VERSION% from https://github.com/MolecularMatters/psd_sdk to %LIB_SRC_ROOT%
    exit /b
)

call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib ^
 	-TargetPlatform=Win64 ^
 	-TargetLib=Psd ^
 	-TargetLibVersion=%LIB_VERSION% ^
 	-TargetConfigs=Debug+Release ^
	-LibOutputPath=Libraries ^
 	-TargetArchitecture=x64 ^
 	-CMakeGenerator=VS2019 ^
 	-SkipCreateChangelist ^
 	-TargetLibSourcePath="%LIB_SRC_ROOT%" ^
 	-TargetRootDir="%LIB_DST_ROOT%" ^
	-SkipCreateChangelist ^
 	|| exit /b

call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib ^
	-TargetPlatform=Win64 ^
	-TargetLib=Psd ^
	-TargetLibVersion=%LIB_VERSION% ^
	-TargetConfigs=Debug+Release ^
	-LibOutputPath=Libraries ^
	-TargetArchitecture=ARM64 ^
	-CMakeGenerator=VS2019 ^
	-SkipCreateChangelist ^
	-TargetLibSourcePath="%LIB_SRC_ROOT%" ^
	-TargetRootDir="%LIB_DST_ROOT%" ^
	-SkipCreateChangelist ^
	|| exit /b

