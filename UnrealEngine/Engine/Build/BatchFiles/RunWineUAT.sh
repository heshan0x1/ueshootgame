#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
# Entrypoint for UAT when running under Linux and Wine
# Building under Wine is highly experimental and not officially supported

set -o errexit
set -o pipefail

# Put ourselves into Engine directory (two up from location of this script)
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
cd "$SCRIPT_DIR/../.." || exit

# Remove empty "qagame" directory. This collides with directory "QAGame" and confuses directory caching in UAT/UBT.
# This is a problem on Linux with ext4 and the Perforce history for this directory.
rm -rf ../qagame

[ ! -d "$UE_SDKS_ROOT/HostWin64" ] && echo "AutoSDK dir '$UE_SDKS_ROOT' does not contain sub-dir 'HostWin64'" && exit 1
# Reformat UE_SDKS_ROOT under Z: and with backslashes
export UE_SDKS_ROOT="Z:${UE_SDKS_ROOT//\//\\}"

# Never UBT/UAT compile through Wine (see comment above)
/opt/uebuilder/bin/wine "$SCRIPT_DIR/RunUAT.bat" "$@" 2>&1
