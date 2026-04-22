#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.

# Set environment variables
ENGINE_ROOT=$(dirname "$0")/../../../../../../../../..
ENGINE_ROOT=$(realpath "$ENGINE_ROOT")
LIBWEBM_VERSION=1.0.0.27
LIBWEBM_ROOT=${ENGINE_ROOT}/Plugins/Media/WebMMedia/Source/ThirdParty/webm/${LIBWEBM_VERSION}

# Build for both ARM64 & x64
"${ENGINE_ROOT}/Build/BatchFiles/RunUAT.sh" BuildCMakeLib \
  -TargetPlatform="Linux" \
  -TargetLib="webm" \
  -TargetLibVersion="${LIBWEBM_VERSION}" \
  -TargetConfigs="Release" \
  -LibOutputPath="lib" \
  -TargetArchitecture="x86_64-unknown-linux-gnu" \
  -CMakeGenerator="Makefile" \
  -SkipCreateChangelist \
  -TargetLibSourcePath="${LIBWEBM_ROOT}" \
  -TargetRootDir="${LIBWEBM_ROOT}"
