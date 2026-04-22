#!/bin/bash

set -e

LIBRARY_NAME="Blosc"
REPOSITORY_NAME="c-blosc"

# Informational, for the usage message.
CURRENT_LIBRARY_VERSION=1.21.0

BUILD_SCRIPT_NAME="$(basename $BASH_SOURCE)"
BUILD_SCRIPT_DIR=`cd $(dirname "$BASH_SOURCE"); pwd`

UsageAndExit()
{
    echo "Build $LIBRARY_NAME for use with Unreal Engine on Mac"
    echo
    echo "Usage:"
    echo
    echo "    $BUILD_SCRIPT_NAME <$LIBRARY_NAME Version>"
    echo
    echo "Usage examples:"
    echo
    echo "    $BUILD_SCRIPT_NAME $CURRENT_LIBRARY_VERSION"
    echo "      -- Installs $LIBRARY_NAME version $CURRENT_LIBRARY_VERSION."
    echo
    exit 1
}

# Get version from arguments.
LIBRARY_VERSION=$1
if [ -z "$LIBRARY_VERSION" ]
then
    UsageAndExit
fi

UE_MODULE_LOCATION=$BUILD_SCRIPT_DIR
UE_SOURCE_THIRD_PARTY_LOCATION=`cd $UE_MODULE_LOCATION/..; pwd`

ZLIB_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/zlib/v1.2.8"
ZLIB_INCLUDE_LOCATION="$ZLIB_LOCATION/include/Mac"
ZLIB_LIB_LOCATION="$ZLIB_LOCATION/lib/Mac/libz.a"

SOURCE_LOCATION="$UE_MODULE_LOCATION/$REPOSITORY_NAME-$LIBRARY_VERSION"

BUILD_LOCATION="$UE_MODULE_LOCATION/Intermediate"

INSTALL_INCLUDEDIR=include

INSTALL_LOCATION="$UE_MODULE_LOCATION/Deploy/$REPOSITORY_NAME-$LIBRARY_VERSION"
INSTALL_INCLUDE_LOCATION="$INSTALL_LOCATION/$INSTALL_INCLUDEDIR"
INSTALL_MAC_LOCATION="$INSTALL_LOCATION/Mac"

rm -rf $BUILD_LOCATION
rm -rf $INSTALL_INCLUDE_LOCATION
rm -rf $INSTALL_MAC_LOCATION

mkdir $BUILD_LOCATION
pushd $BUILD_LOCATION > /dev/null

# Note that we patch the source for the version of LZ4 that is bundled with
# Blosc to add a prefix to all of its functions. This ensures that the symbol
# names do not collide with the version(s) of LZ4 that are embedded in the
# engine.

# Copy the source into the build directory so that we can apply patches.
BUILD_SOURCE_LOCATION="$BUILD_LOCATION/$REPOSITORY_NAME-$LIBRARY_VERSION"

cp -r $SOURCE_LOCATION $BUILD_SOURCE_LOCATION

pushd $BUILD_SOURCE_LOCATION > /dev/null
git apply $UE_MODULE_LOCATION/Blosc_v1.21.0_LZ4_PREFIX.patch
popd > /dev/null

C_FLAGS="-DLZ4_PREFIX=BLOSC_"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_LOCATION"
    -DPREFER_EXTERNAL_ZLIB=ON
    -DZLIB_INCLUDE_DIR="$ZLIB_INCLUDE_LOCATION"
    -DZLIB_LIBRARY="$ZLIB_LIB_LOCATION"
    -DCMAKE_C_FLAGS="$C_FLAGS"
    -DCMAKE_OSX_DEPLOYMENT_TARGET="14.0"
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
    -DDEACTIVATE_SSE2=ON
    -DDEACTIVATE_AVX2=ON
    -DBUILD_SHARED=OFF
    -DBUILD_TESTS=OFF
    -DBUILD_FUZZERS=OFF
    -DBUILD_BENCHMARKS=OFF
    -DCMAKE_DEBUG_POSTFIX=_d
)

NUM_CPU=`sysctl -n hw.ncpu`

echo Configuring build for $LIBRARY_NAME version $LIBRARY_VERSION...
cmake -G "Xcode" $BUILD_SOURCE_LOCATION "${CMAKE_ARGS[@]}"

echo Building $LIBRARY_NAME for Debug...
cmake --build . --config Debug -j$NUM_CPU

echo Installing $LIBRARY_NAME for Debug...
cmake --install . --config Debug

echo Building $LIBRARY_NAME for Release...
cmake --build . --config Release -j$NUM_CPU

echo Installing $LIBRARY_NAME for Release...
cmake --install . --config Release

popd > /dev/null

echo Removing pkgconfig files...
rm -rf "$INSTALL_LOCATION/lib/pkgconfig"

echo Moving lib directory into place...
INSTALL_LIB_LOCATION="$INSTALL_LOCATION/Mac"
mkdir $INSTALL_LIB_LOCATION
mv "$INSTALL_LOCATION/lib" "$INSTALL_LIB_LOCATION"

echo Done.
