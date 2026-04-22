#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.

ENGINE_ROOT=$(dirname "$0")/../../../../../..
ENGINE_ROOT=$(realpath "$ENGINE_ROOT")
METIS_VERSION="5.2.1"
GKLIB_VERSION="8bd6bad"
CMAKEARGS="-DGKLIB_PATH=\"${ENGINE_ROOT}/Source/ThirdParty/GKlib/${GKLIB_VERSION}\""
"${ENGINE_ROOT}/Build/BatchFiles/RunUAT.sh" BuildCMakeLib \
  -TargetPlatform="Linux" \
  -TargetLib="metis" \
  -TargetLibVersion="${METIS_VERSION}" \
  -TargetConfigs="Debug+Release" \
  -LibOutputPath="lib" \
  -TargetArchitecture="x86_64-unknown-linux-gnu" \
  -CMakeGenerator="Makefile" \
  -CMakeAdditionalArguments="${CMAKEARGS}" \
  -SkipCreateChangelist
