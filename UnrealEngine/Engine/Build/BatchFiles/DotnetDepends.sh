#!/bin/bash

## Unreal Engine AutomationTool build script
## Copyright Epic Games, Inc. All Rights Reserved
##
## This script is expecting to exist in the Engine/Build/BatchFiles directory.  It will not work correctly
## if you copy it to a different location and run it.

## First, make sure the batch file exists in the folder we expect it to.  This is necessary in order to
## verify that our relative path to the /Engine/Source directory is correct
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
pushd "$SCRIPT_DIR/../../Source" >/dev/null || exit 1

if [ ! -f ../Build/BatchFiles/DotnetDepends.sh ]; then
  echo
  echo "DotnetDepends ERROR: The script does not appear to be located in the /Engine/Build/BatchFiles directory.  This script must be run from within that directory."
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

SLN=$1
DEPENDS_FILE=$2
MSBuild_Verbosity="${3:-quiet}"
TEMPDIR=$(mktemp -q -d)

"$SCRIPT_DIR/DotnetRetry.sh" msbuild "$SLN" -t:Scan -p:Configuration=Development -p:Platform="Any CPU" -p:OutputPath="$TEMPDIR"/ -p:DependsEncoding=Ascii -noLogo -v:"$MSBuild_Verbosity"
cat "$TEMPDIR"/*.dep.csv | sort -u -o "$DEPENDS_FILE"
rm -rf "$TEMPDIR"

popd >/dev/null || return
