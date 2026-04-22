#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.

set -e

SCRIPT_PATH=$0
if [ -L "$SCRIPT_PATH" ]; then
	SCRIPT_PATH="$(dirname "$SCRIPT_PATH")/$(readlink "$SCRIPT_PATH")"
fi

cd "$(dirname "$SCRIPT_PATH")" && SCRIPT_PATH="`pwd`/$(basename "$SCRIPT_PATH")"

cd ../../../..

# Select the preferred architecture for the current system
ARCH=x64
[ $(uname -m) == "arm64" ] && ARCH=arm64 

GIT_DEP_BIN_DIR=./Engine/Binaries/DotNET/GitDependencies/osx-$ARCH
GIT_DEP_EXE=$GIT_DEP_BIN_DIR/GitDependencies

if [[ $(xattr $GIT_DEP_EXE) = "com.apple.quarantine" ]]
then
    xattr -d com.apple.quarantine $GIT_DEP_EXE $GIT_DEP_BIN_DIR/*.dylib
fi

$GIT_DEP_EXE "$@"

pushd "$(dirname "$SCRIPT_PATH")" > /dev/null
sh FixDependencyFiles.sh
popd > /dev/null
