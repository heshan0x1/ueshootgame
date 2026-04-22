#!/bin/bash

set -e

LIBRARY_NAME=OpenUSD
REPOSITORY_NAME=OpenUSD

# Informational, for the usage message.
CURRENT_LIBRARY_VERSION=25.08

BUILD_SCRIPT_NAME="$(basename $BASH_SOURCE)"
BUILD_SCRIPT_LOCATION=`cd $(dirname "$BASH_SOURCE"); pwd`

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

# This path may be adjusted to point to wherever the OpenUSD source is
# located. It is typically obtained by either downloading a zip/tarball of
# the source code, or more commonly by cloning the GitHub repository, e.g.:
#     git clone --branch <version tag> https://github.com/PixarAnimationStudios/OpenUSD.git OpenUSD_src
# Then from inside the cloned OpenUSD_src directory, apply all patches sitting
# next to this build script:
#     git apply <build script location>/OpenUSD_*.patch
# Note also that this path may be emitted as part of OpenUSD error messages, so
# it is suggested that it not reveal any sensitive information.
OPENUSD_SOURCE_LOCATION="/tmp/${REPOSITORY_NAME}_src"

PLATFORM_NAME=Mac

UE_ENGINE_LOCATION=`cd $BUILD_SCRIPT_LOCATION/../../../../../..; pwd`

UE_SOURCE_THIRD_PARTY_LOCATION="$UE_ENGINE_LOCATION/Source/ThirdParty"
TBB_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/Intel/TBB/Deploy/oneTBB-2021.13.0"
TBB_LIB_LOCATION="$TBB_LOCATION/Mac/lib"
TBB_CMAKE_LOCATION="$TBB_LIB_LOCATION/cmake/TBB"
IMATH_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/Imath/Deploy/Imath-3.1.12"
IMATH_LIB_LOCATION="$IMATH_LOCATION/Mac/lib"
IMATH_CMAKE_LOCATION="$IMATH_LIB_LOCATION/cmake/Imath"
OPENSUBDIV_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/OpenSubdiv/Deploy/OpenSubdiv-3.6.0"
OPENSUBDIV_INCLUDE_DIR="$OPENSUBDIV_LOCATION/include"
OPENSUBDIV_LIB_LOCATION="$OPENSUBDIV_LOCATION/Mac/lib"
ALEMBIC_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/Alembic/Deploy/alembic-1.8.7"
ALEMBIC_LIB_LOCATION="$ALEMBIC_LOCATION/Mac/lib"
ALEMBIC_CMAKE_LOCATION="$ALEMBIC_LIB_LOCATION/cmake/Alembic"
MATERIALX_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/MaterialX/Deploy/MaterialX-1.39.3"
MATERIALX_LIB_LOCATION="$MATERIALX_LOCATION/Mac/lib"
MATERIALX_CMAKE_LOCATION="$MATERIALX_LIB_LOCATION/cmake/MaterialX"

PYTHON_BINARIES_LOCATION="$UE_ENGINE_LOCATION/Binaries/ThirdParty/Python3/$PLATFORM_NAME"
PYTHON_EXECUTABLE_LOCATION="$PYTHON_BINARIES_LOCATION/bin/python3"
PYTHON_SOURCE_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/Python3/$PLATFORM_NAME"
PYTHON_INCLUDE_LOCATION="$PYTHON_SOURCE_LOCATION/include"
PYTHON_LIBRARY_LOCATION="$PYTHON_BINARIES_LOCATION/libpython3.11.dylib"

UE_MODULE_USD_LOCATION=$BUILD_SCRIPT_LOCATION

BUILD_LOCATION="$UE_MODULE_USD_LOCATION/Intermediate"

# OpenUSD build products are written into a deployment directory and must then
# be manually copied from there into place.
INSTALL_LOCATION="$BUILD_LOCATION/Deploy/$REPOSITORY_NAME-$LIBRARY_VERSION"

rm -rf $BUILD_LOCATION

mkdir $BUILD_LOCATION
pushd $BUILD_LOCATION > /dev/null

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_LOCATION"
    -DCMAKE_PREFIX_PATH="$TBB_CMAKE_LOCATION;$IMATH_CMAKE_LOCATION;$ALEMBIC_CMAKE_LOCATION;$MATERIALX_CMAKE_LOCATION"
    -DCMAKE_OSX_DEPLOYMENT_TARGET="14.0"
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
    -DPython3_EXECUTABLE="$PYTHON_EXECUTABLE_LOCATION"
    -DPython3_INCLUDE_DIR="$PYTHON_INCLUDE_LOCATION"
    -DPython3_LIBRARY="$PYTHON_LIBRARY_LOCATION"
    -DPXR_BUILD_ALEMBIC_PLUGIN=ON
    -DPXR_ENABLE_HDF5_SUPPORT=OFF
    -DOPENSUBDIV_INCLUDE_DIR="$OPENSUBDIV_INCLUDE_DIR"
    -DOPENSUBDIV_ROOT_DIR="$OPENSUBDIV_LIB_LOCATION"
    -DPXR_ENABLE_MATERIALX_SUPPORT=ON
    -DBUILD_SHARED_LIBS=ON
    -DPXR_BUILD_TESTS=OFF
    -DPXR_BUILD_EXAMPLES=OFF
    -DPXR_BUILD_TUTORIALS=OFF
    -DPXR_BUILD_USD_TOOLS=OFF
    -DPXR_BUILD_IMAGING=ON
    -DPXR_BUILD_USD_IMAGING=ON
    -DPXR_BUILD_USDVIEW=OFF
)

NUM_CPU=`sysctl -n hw.ncpu`

echo Configuring build for $LIBRARY_NAME version $LIBRARY_VERSION...
cmake -G "Xcode" $OPENUSD_SOURCE_LOCATION "${CMAKE_ARGS[@]}"

echo Building $LIBRARY_NAME for Release...
cmake --build . --config Release -j$NUM_CPU

echo Installing $LIBRARY_NAME for Release...
cmake --install . --config Release

popd > /dev/null

BUILD_BIN_LOCATION="$INSTALL_LOCATION/bin"
BUILD_LIB_LOCATION="$INSTALL_LOCATION/lib"

INSTALL_LIB_LOCATION="$INSTALL_LOCATION/Mac/lib"

echo Removing command-line tools...
rm -rf "$BUILD_BIN_LOCATION"

echo Moving shared libraries to lib directory...
mkdir -p $INSTALL_LIB_LOCATION
mv $BUILD_LIB_LOCATION/*.dylib $INSTALL_LIB_LOCATION

echo Moving built-in $LIBRARY_NAME plugins to UsdResources plugins directory...
INSTALL_RESOURCES_LOCATION="$INSTALL_LOCATION/Resources/UsdResources/$PLATFORM_NAME"
INSTALL_RESOURCES_PLUGINS_LOCATION="$INSTALL_RESOURCES_LOCATION/plugins"
mkdir -p $INSTALL_RESOURCES_LOCATION
mv "$BUILD_LIB_LOCATION/usd" "$INSTALL_RESOURCES_PLUGINS_LOCATION"

echo Moving $LIBRARY_NAME plugin shared libraries to lib directory...
INSTALL_PLUGIN_LOCATION="$INSTALL_LOCATION/plugin"
INSTALL_PLUGIN_USD_LOCATION="$INSTALL_PLUGIN_LOCATION/usd"
mv $INSTALL_PLUGIN_USD_LOCATION/*.dylib "$INSTALL_LIB_LOCATION"

echo Removing top-level $LIBRARY_NAME plugins plugInfo.json file...
rm -f "$INSTALL_PLUGIN_USD_LOCATION/plugInfo.json"

echo Moving $LIBRARY_NAME plugin resource directories to UsdResources plugins directory
mv "$INSTALL_PLUGIN_USD_LOCATION/hdStorm" "$INSTALL_RESOURCES_PLUGINS_LOCATION"
mv "$INSTALL_PLUGIN_USD_LOCATION/hioAvif" "$INSTALL_RESOURCES_PLUGINS_LOCATION"
mv "$INSTALL_PLUGIN_USD_LOCATION/hioImageIO" "$INSTALL_RESOURCES_PLUGINS_LOCATION"
mv "$INSTALL_PLUGIN_USD_LOCATION/sdrGlslfx" "$INSTALL_RESOURCES_PLUGINS_LOCATION"
mv "$INSTALL_PLUGIN_USD_LOCATION/usdAbc" "$INSTALL_RESOURCES_PLUGINS_LOCATION"
mv "$INSTALL_PLUGIN_USD_LOCATION/usdShaders" "$INSTALL_RESOURCES_PLUGINS_LOCATION"

rmdir "$INSTALL_PLUGIN_USD_LOCATION"
rmdir "$INSTALL_PLUGIN_LOCATION"

echo Removing CMake files...
rm -rf "$INSTALL_LOCATION/cmake"
rm -f $INSTALL_LOCATION/*.cmake

echo Removing Python .pyc files...
find "$INSTALL_LOCATION" -name "*.pyc" -delete

echo Removing pxr.Tf.testenv Python module...
rm -rf "$BUILD_LIB_LOCATION/python/pxr/Tf/testenv"

echo Moving Python modules to Content...
INSTALL_CONTENT_LOCATION="$INSTALL_LOCATION/Content/Python/Lib/$PLATFORM_NAME/site-packages"
mkdir -p "$INSTALL_CONTENT_LOCATION"
mv "$BUILD_LIB_LOCATION/python/pxr" "$INSTALL_CONTENT_LOCATION"
rmdir "$BUILD_LIB_LOCATION/python"

rmdir "$BUILD_LIB_LOCATION"

# The locations of the shared libraries where they will live when ultimately
# deployed are used to generate relative paths for use as LibraryPaths in
# plugInfo.json files.
# The OpenUSD plugins all exist at the same directory level, so any of them can
# be used to generate a relative path.
USD_PLUGIN_LOCATION="$UE_ENGINE_LOCATION/Plugins/Runtime/USDCore/Resources/UsdResources/$PLATFORM_NAME/plugins/usd"
USD_LIBS_LOCATION="$UE_ENGINE_LOCATION/Plugins/Runtime/USDCore/Source/ThirdParty/USD/Mac/lib"

echo Adjusting plugInfo.json LibraryPath fields...
USD_PLUGIN_TO_USD_LIBS_REL_PATH=`$PYTHON_EXECUTABLE_LOCATION -c "import os.path; print(os.path.relpath('$USD_LIBS_LOCATION', '$USD_PLUGIN_LOCATION'))"`

for PLUG_INFO_FILE in `find $INSTALL_RESOURCES_LOCATION -name plugInfo.json | xargs grep LibraryPath -l`
do
    $PYTHON_EXECUTABLE_LOCATION $BUILD_SCRIPT_LOCATION/modify_plugInfo_file.py $PLUG_INFO_FILE $USD_PLUGIN_TO_USD_LIBS_REL_PATH
done

echo Cleaning @rpath entries for shared libraries...
for SHARED_LIB in `find $INSTALL_LOCATION -type f -name '*.dylib' -o -name '*.so'`
do
    RPATHS_TO_DELETE=(
        $TBB_LIB_LOCATION
        $PYTHON_BINARIES_LOCATION
        $INSTALL_LIB_LOCATION
        $INSTALL_PLUGIN_USD_LOCATION
    )

    OTOOL_OUTPUT=`otool -l $SHARED_LIB`

    for RPATH_TO_DELETE in ${RPATHS_TO_DELETE[@]}
    do
        if [[ $OTOOL_OUTPUT == *"path $RPATH_TO_DELETE"* ]]
        then
            install_name_tool -delete_rpath $RPATH_TO_DELETE $SHARED_LIB
        fi
    done
done

echo Checking for invalid code signatures in generated libraries...
for SHARED_LIB in `find $INSTALL_LOCATION -type f -name '*.dylib' -o -name '*.so'`
do
    if ! codesign -v $SHARED_LIB
    then
        echo Invalid code signature detected in library: $SHARED_LIB
        echo Replacing with ad-hoc signature...
        codesign -f -s - $SHARED_LIB
    fi
done

echo Done.
