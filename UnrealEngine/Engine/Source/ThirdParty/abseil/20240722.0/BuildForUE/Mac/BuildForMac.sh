#!/bin/bash

set -e

TARGET=abseil
VERSION=20240722.0
ARCHS=(arm64)

LIB_ROOT=$(realpath "${0%/*}/../../")
ENGINE_ROOT=$(realpath "${0%/*}/../../../../../..")
THIRDPARTY_ROOT=${ENGINE_ROOT}/Source/ThirdParty

CMAKE_ADDITIONAL_ARGUMENTS="-DCMAKE_CXX_STANDARD=17 -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_INSTALL_PREFIX=${LIB_ROOT} -DCMAKE_INSTALL_LIBDIR=lib\Mac\arm64\Release -DBUILD_SHARED_LIBS=OFF -DABSL_PROPAGATE_CXX_STD=ON -DABSL_MSVC_STATIC_RUNTIME=OFF"

for ARCH in "${ARCHS[@]}"; do
    bash ${ENGINE_ROOT}/Build/BatchFiles/RunUAT.sh BuildCMakeLib -TargetPlatform=Mac -TargetArchitecture=${ARCH} -TargetLib=${TARGET} -TargetLibVersion=${VERSION} -TargetLibSourcePath=${LIB_ROOT}/src -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -SkipCreateChangelist -MakeTarget=install
done
