#!/bin/bash

## Unreal Engine Build script for SDL3
## Copyright Epic Games, Inc. All Rights Reserved.

# This script just builds the libraries for x86-64 using the local machine.  It then copies the libraries into the correct
# location for use by UE.  It should not be used to build the libraries for distribution, use the docker script.  But for debugging and development
# having them build quickly with full paths can be invaluable
#
#
# Built libSDL libraries are in build directory
SCRIPT_DIR=$(dirname $(readlink -f "${BASH_SOURCE[0]}"))
export SDL_DIR=../../../SDL-gui-backend

export ARCH=$(uname -m)
mkdir -p $SCRIPT_DIR/build
pushd $SCRIPT_DIR/build

# Get num of cores
export CORES=$(getconf _NPROCESSORS_ONLN)
echo Using ${CORES} cores for building

BuildWithOptions()
{
	local BuildDir=$1
	local StaticLibName=$2
	local SdlLibName=$3
	local SdlDir=$4
	local Cflags="$5"
	shift 5
	local Options="$@"

	rm -rf $BuildDir
	mkdir -p $BuildDir
	pushd $BuildDir

	# Building with OGL breaks SDL_CreateWindow() on embedded devices w/o proper GL libraries
	#   http://lists.libsdl.org/pipermail/commits-libsdl.org/2017-September/001967.html
	if [[ ${ARCH} == 'aarch64' ]]; then
		Options+=' -DSDL_VIDEO_OPENGL=OFF'
	fi

	# first build SDL3 lib
	set -x
	cmake $Options -DCMAKE_C_FLAGS="${Cflags}" $SdlDir
	set +x

	make -j${CORES}

	mv $StaticLibName ../$SdlLibName
	popd
}

set -e

OPTS=()
OPTS+=(-DSDL_STATIC=ON)
OPTS+=(-DSDL_SHARED=OFF)
OPTS+=(-DSDL_KMSDRM=OFF)

# build Debug with -fPIC so it's usable in any type of build
BuildWithOptions Debug            libSDL3.a  libSDL3_fPIC_Debug.a ${SDL_DIR}      "-gdwarf-4 -fPIC"  -DCMAKE_BUILD_TYPE=Debug   -DSDL_STATIC_PIC=ON    "${OPTS[@]}"
BuildWithOptions Release          libSDL3.a  libSDL3.a            ${SDL_DIR}      "-gdwarf-4"        -DCMAKE_BUILD_TYPE=Release                        "${OPTS[@]}"
BuildWithOptions ReleasePIC       libSDL3.a  libSDL3_fPIC.a       ${SDL_DIR}      "-gdwarf-4 -fPIC"  -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC_PIC=ON    "${OPTS[@]}" 

set +e
popd
(set -x; cp $SCRIPT_DIR/build/*.a $SCRIPT_DIR/../SDL-gui-backend/lib/Unix/x86_64-unknown-linux-gnu/)

