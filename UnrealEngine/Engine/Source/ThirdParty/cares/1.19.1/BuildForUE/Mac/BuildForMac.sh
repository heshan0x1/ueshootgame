#!/bin/bash

set -e

TARGET=cares
VERSION=1.19.1
ARCHS=(arm64)

LIB_ROOT=$(realpath "${0%/*}/../../")
ENGINE_ROOT=$(realpath "${0%/*}/../../../../../..")
THIRDPARTY_ROOT=${ENGINE_ROOT}/Source/ThirdParty

CMAKE_ADDITIONAL_ARGUMENTS="-DCARES_STATIC=ON -DCARES_SHARED=OFF -DCARES_INSTALL=ON -DCARES_BUILD_TOOLS=OFF -DCMAKE_INSTALL_PREFIX=${LIB_ROOT} -DCMAKE_INSTALL_LIBDIR=lib\Mac\arm64\Release"

for ARCH in "${ARCHS[@]}"; do
    bash ${ENGINE_ROOT}/Build/BatchFiles/RunUAT.sh BuildCMakeLib -TargetPlatform=Mac -TargetArchitecture=${ARCH} -TargetLib=${TARGET} -TargetLibVersion=${VERSION} -TargetLibSourcePath=${LIB_ROOT}/src -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -SkipCreateChangelist -MakeTarget=install
done
