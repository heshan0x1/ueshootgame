#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.

ENGINE_ROOT=$(dirname "$0")/../../../../../..
ENGINE_ROOT=$(realpath "$ENGINE_ROOT")
GKLIB_VERSION="8bd6bad"
CMAKEARGS="-DCMAKE_OSX_DEPLOYMENT_TARGET=13.00"
"${ENGINE_ROOT}/Build/BatchFiles/RunUAT.sh" BuildCMakeLib \
  -TargetPlatform="Mac" \
  -TargetLib="GKlib" \
  -TargetLibVersion="${GKLIB_VERSION}" \
  -TargetConfigs="Debug+Release" \
  -LibOutputPath="lib" \
  -TargetArchitecture="x86_64;arm64" \
  -CMakeGenerator="Xcode" \
  -CMakeAdditionalArguments="${CMAKEARGS}" \
  -SkipCreateChangelist
