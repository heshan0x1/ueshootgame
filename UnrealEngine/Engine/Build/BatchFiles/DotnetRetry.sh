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

if [ ! -f ../Build/BatchFiles/DotnetRetry.sh ]; then
  echo
  echo "DotnetRetry ERROR: The script does not appear to be located in the /Engine/Build/BatchFiles directory.  This script must be run from within that directory."
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

MAX_RETRIES=3
RETRY_DELAY=1
COUNT=0
ERRORCODE=0

while [ $COUNT -lt $MAX_RETRIES ]; do
  # Run the dotnet command to retry if a failure occurs
  output=$(dotnet "$@")

  ERRORCODE=$?
  if [ $ERRORCODE -eq 0 ]; then
    echo "$output"
    break
  fi

  # Command failed, retry up to our max
  COUNT=$((COUNT + 1))
  if [ $COUNT -lt $MAX_RETRIES ]; then
    echo "dotnet command failed with errorcode $ERRORCODE. Retrying in $RETRY_DELAY seconds... ($COUNT/$MAX_RETRIES)"
    sleep $RETRY_DELAY
  else
    echo "dotnet command failed with errorcode $ERRORCODE after $MAX_RETRIES attempts"
    echo "$output"
    break
  fi
done

popd >/dev/null || exit 1
exit $ERRORCODE
