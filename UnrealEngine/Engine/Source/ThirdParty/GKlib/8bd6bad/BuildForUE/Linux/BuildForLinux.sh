#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.

ENGINE_ROOT=$(dirname "$0")/../../../../../..
ENGINE_ROOT=$(realpath "$ENGINE_ROOT")
GKLIB_VERSION="8bd6bad"
"${ENGINE_ROOT}/Build/BatchFiles/RunUAT.sh" BuildCMakeLib \
  -TargetPlatform="Linux" \
  -TargetLib="GKlib" \
  -TargetLibVersion="${GKLIB_VERSION}" \
  -TargetConfigs="Debug+Release" \
  -LibOutputPath="lib" \
  -TargetArchitecture="x86_64-unknown-linux-gnu" \
  -CMakeGenerator="Makefile" \
  -SkipCreateChangelist
