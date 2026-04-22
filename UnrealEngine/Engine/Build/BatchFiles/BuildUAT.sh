#!/bin/bash

## Unreal Engine AutomationTool build script
## Copyright Epic Games, Inc. All Rights Reserved
##
## This script is expecting to exist in the Engine/Build/BatchFiles directory.  It will not work correctly
## if you copy it to a different location and run it.

## First, make sure the batch file exists in the folder we expect it to.  This is necessary in order to
## verify that our relative path to the /Engine/Source directory is correct
SCRIPT_DIR=$(cd "`dirname "$0"`" && pwd)
pushd "$SCRIPT_DIR/../../Source" >/dev/null

if [ ! -f ../Build/BatchFiles/BuildUAT.sh ]; then
  echo
  echo "BuildUAT ERROR: The script does not appear to be located in the /Engine/Build/BatchFiles directory.  This script must be run from within that directory."
  echo
  popd >/dev/null
  exit 1
fi

# Ensure UnrealBuildTool is up to date as it is a prereq
"$SCRIPT_DIR/BuildUBT.sh"
if [ $? -ne 0 ]; then
popd >/dev/null
exit 1
fi

MSBuild_Verbosity="${1:-quiet}"

# Check to see if the files in the AutomationTool solution have changed

mkdir -p ../Intermediate/Build

DEPENDS_FILE=../Intermediate/Build/AutomationTool.dep.csv
TEMP_DEPENDS_FILE=$(mktemp)

"$SCRIPT_DIR/DotnetDepends.sh" Programs/AutomationTool/AutomationTool.sln "$TEMP_DEPENDS_FILE" "$MSBuild_Verbosity"

PERFORM_REBUILD=0
if [ "$2" == "FORCE" ]; then
  PERFORM_REBUILD=1
  echo "Rebuilding: Force build requested"

elif [ ! -f ../Binaries/DotNET/AutomationTool/AutomationTool.dll ]; then
  PERFORM_REBUILD=1
  echo "Rebuilding: AutomationTool assembly not found"

elif [ -f ../Intermediate/Build/AutomationTool.dep.csv ]; then
  if ! cmp --silent $DEPENDS_FILE "$TEMP_DEPENDS_FILE"; then
    PERFORM_REBUILD=1
    echo "Rebuilding: Found updated files"
  fi

else
  PERFORM_REBUILD=1
  echo "Rebuilding: No record of previous build"
fi

if [ $PERFORM_REBUILD -eq 1 ]; then
  echo "Building AutomationTool..."
  "$SCRIPT_DIR/DotnetRetry.sh" build Programs/AutomationTool/AutomationTool.csproj -c Development -v "$MSBuild_Verbosity"
  if [ $? -ne 0 ]; then
    echo "AutomationTool compilation failed"
    popd >/dev/null || exit 1
    exit 1
  fi

  echo "Publishing AutomationTool..."
  if [ -d ../Binaries/DotNET/AutomationTool ]; then
    rm -f ../Binaries/DotNET/AutomationTool/* > /dev/null 2>&1
  fi
  if [ ! -d ../Binaries/DotNET/AutomationTool ]; then
    mkdir -p ../Binaries/DotNET/AutomationTool >/dev/null
  fi
  "$SCRIPT_DIR/DotnetRetry.sh" publish Programs/AutomationTool/AutomationTool.csproj -c Development --output ../Binaries/DotNET/AutomationTool --no-build -v "$MSBuild_Verbosity"
  if [ $? -ne 0 ]; then
    echo "AutomationTool publish failed"
    popd >/dev/null || exit 1
    exit 1
  fi

  mv -f "$TEMP_DEPENDS_FILE" $DEPENDS_FILE >/dev/null
else
  echo "AutomationTool is up to date"
fi

popd >/dev/null
exit 0

