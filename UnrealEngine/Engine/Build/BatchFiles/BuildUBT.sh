#!/bin/bash

## Unreal Engine UnrealBuildTool build script
## Copyright Epic Games, Inc. All Rights Reserved
##
## This script is expecting to exist in the Engine/Build/BatchFiles directory.  It will not work correctly
## if you copy it to a different location and run it.

## First, make sure the batch file exists in the folder we expect it to.  This is necessary in order to
## verify that our relative path to the /Engine/Source directory is correct
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
pushd "$SCRIPT_DIR/../../Source" >/dev/null || exit 1

if [ ! -f ../Build/BatchFiles/BuildUBT.sh ]; then
  echo
  echo "BuildUBT ERROR: The script does not appear to be located in the /Engine/Build/BatchFiles directory.  This script must be run from within that directory."
  echo
  popd >/dev/null || exit 1
  exit 1
fi

if [ "$(uname)" = "Darwin" ]; then
  # Setup Environment
  source "$SCRIPT_DIR/Mac/SetupEnvironment.sh" -dotnet "$SCRIPT_DIR/Mac"
elif [ "$(uname)" = "Linux" ]; then
  # Setup Environment
  source "$SCRIPT_DIR/Linux/SetupEnvironment.sh" -dotnet "$SCRIPT_DIR/Linux"
fi

MSBuild_Verbosity="${1:-quiet}"

# Check to see if the files in the UnrealBuildTool solution have changed

mkdir -p ../Intermediate/Build

DEPENDS_FILE=../Intermediate/Build/UnrealBuildTool.dep.csv
TEMP_DEPENDS_FILE=$(mktemp)

"$SCRIPT_DIR/DotnetDepends.sh" Programs/UnrealBuildTool/UnrealBuildTool.sln "$TEMP_DEPENDS_FILE" "$MSBuild_Verbosity"

PERFORM_REBUILD=0
if [ "$2" == "FORCE" ]; then
  PERFORM_REBUILD=1
  echo "Rebuilding: Force build requested"

elif [ ! -f ../Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll ]; then
  PERFORM_REBUILD=1
  echo "Rebuilding: UnrealBuildTool assembly not found"

elif [ -f ../Intermediate/Build/UnrealBuildTool.dep.csv ]; then
  if ! cmp --silent $DEPENDS_FILE "$TEMP_DEPENDS_FILE"; then
    PERFORM_REBUILD=1
    echo "Rebuilding: Found updated files"
  fi

else
  PERFORM_REBUILD=1
  echo "Rebuilding: No record of previous build"
fi

if [ $PERFORM_REBUILD -eq 1 ]; then
  echo "Building UnrealBuildTool..."
  "$SCRIPT_DIR/DotnetRetry.sh" build Programs/UnrealBuildTool/UnrealBuildTool.csproj -c Development -v "$MSBuild_Verbosity"
  if [ $? -ne 0 ]; then
    echo "UnrealBuildTool compilation failed"
    popd >/dev/null || exit 1
    exit 1
  fi

  echo "Publishing UnrealBuildTool..."
  if [ -d ../Binaries/DotNET/UnrealBuildTool ]; then
    rm -f ../Binaries/DotNET/UnrealBuildTool/* > /dev/null 2>&1
  fi
  if [ ! -d ../Binaries/DotNET/UnrealBuildTool ]; then
    mkdir -p ../Binaries/DotNET/UnrealBuildTool >/dev/null
  fi
  "$SCRIPT_DIR/DotnetRetry.sh" publish Programs/UnrealBuildTool/UnrealBuildTool.csproj -c Development --output ../Binaries/DotNET/UnrealBuildTool --no-build -v "$MSBuild_Verbosity"
  if [ $? -ne 0 ]; then
    echo "UnrealBuildTool publish failed"
    popd >/dev/null || exit 1
    exit 1
  fi

  mv -f "$TEMP_DEPENDS_FILE" $DEPENDS_FILE >/dev/null
else
  echo "UnrealBuildTool is up to date"
fi

popd >/dev/null || exit 1
exit 0
