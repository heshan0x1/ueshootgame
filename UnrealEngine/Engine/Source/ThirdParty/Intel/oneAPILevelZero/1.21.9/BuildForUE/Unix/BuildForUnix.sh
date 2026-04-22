#!/bin/bash

set -e
SCRIPT_DIR=$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )

# We can deduce the version via the directory structure that is enforced by BuildCMakeLib
VERSION=$(basename "$(realpath -s $SCRIPT_DIR/../..)")
MINIMUM_PYTHON_VERSION="3.0.0"

ENGINE_DIR="$(realpath -s $SCRIPT_DIR/../../../../../../..)"
if [[ -z $UE_SDKS_ROOT ]]; then
	PYTHON_VERSION=$(python --version)
	if [ "$(printf '%s\n' "$MINIMUM_PYTHON_VERSION" "$PYTHON_VERSION" | sort -V | head -n1)" = "$MINIMUM_PYTHON_VERSION" ]; then 
	    echo "Found $PYTHON_VERSION"
	else
	    echo "No appropriate python version found: $PYTHON_VERSION < $MINIMUM_PYTHON_VERSION"
	    exit
	fi
	SDK_VERSION=$(python $ENGINE_DIR/Build/BatchFiles/Linux/GetLinuxSDKVersion.py)
	export LINUX_MULTIARCH_ROOT=$ENGINE_DIR/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/$SDK_VERSION
else
	source $UE_SDKS_ROOT/HostLinux/Linux_x64/OutputEnvVars.txt
fi

MAKE_TARGET=libraries_only
THIRD_PARTY_LIB_PATH=Intel/oneAPILevelZero
CMAKE_ADDITIONAL_ARGUMENTS="-DBUILD_WITH_LIBCXX=1"

Build()
{
	local ARCH=$1

	if [[ -z $ARCH ]]; then
		echo "ARCH must be set"
		exit
	fi

	$ENGINE_DIR/Build/BatchFiles/RunUAT.sh BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=$ARCH -TargetLib=$THIRD_PARTY_LIB_PATH -TargetLibVersion=$VERSION -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="$CMAKE_ADDITIONAL_ARGUMENTS" -MakeTarget=$MAKE_TARGET -SkipCreateChangelist
}

Build "x86_64-unknown-linux-gnu"
Build "aarch64-unknown-linux-gnueabi"
