#!/bin/bash
 
set -e
 
TARGET=Protobuf
VERSION=30.0
ARCHS=(arm64)
 
LIB_ROOT=$(realpath "${0%/*}/../../")
ENGINE_ROOT=$(realpath "${0%/*}/../../../../../..")
THIRDPARTY_ROOT=${ENGINE_ROOT}/Source/ThirdParty
 
CMAKE_ADDITIONAL_ARGUMENTS="-DCMAKE_CXX_STANDARD=20 -DCMAKE_INSTALL_PREFIX=${LIB_ROOT} -DCMAKE_INSTALL_LIBDIR=lib\Mac\arm64\Release -DCMAKE_INSTALL_BINDIR=bin\Mac\arm64\Release -DBUILD_SHARED_LIBS=OFF -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF -Dprotobuf_MSVC_STATIC_RUNTIME=OFF -Dprotobuf_MSVC_STATIC_RUNTIME=OFF -Dprotobuf_BUILD_PROTOBUF_BINARIES=ON -Dprotobuf_BUILD_PROTOC_BINARIES=OFF -Dprotobuf_BUILD_LIBUPB=OFF"
CMAKE_ABSL_ARGUMENTS="-Dprotobuf_ABSL_PROVIDER=package -Dabsl_DIR=${THIRDPARTY_ROOT}/abseil/20240722.0/lib/Mac/arm64/Release/cmake/absl"
CMAKE_ZLIB_ARGUMENTS="-DZLIB_ROOT=${THIRDPARTY_ROOT}/zlib/1.3/lib/Mac/Release -DZLIB_INCLUDE_DIR=${THIRDPARTY_ROOT}/zlib/1.3/include"
 
for ARCH in "${ARCHS[@]}"; do
    bash ${ENGINE_ROOT}/Build/BatchFiles/RunUAT.sh BuildCMakeLib -TargetPlatform=Mac -TargetArchitecture=${ARCH} -TargetLib=${TARGET} -TargetLibVersion=${VERSION} TargetLibSourcePath=${LIB_ROOT}/src -TargetLibSourcePath=${LIB_ROOT}/src -TargetConfigs=Release -LibOutputPath=lib -BinOutputPath=bin -IncludeOutputPath=include -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS} ${CMAKE_ABSL_ARGUMENTS} ${CMAKE_ZLIB_ARGUMENTS}" -SkipCreateChangelist -MakeTarget=install
done