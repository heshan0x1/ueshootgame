#! /bin/bash

#
# This script copies all required files back into the plugin folder tree and adds them to a new Perforce CL.
#
# If an intermediate step fails, the script exits.
#

set -Eeuo pipefail

if [ -z "$1" ]; then
	echo "Usage: [Script] [Working dir]"
	exit 1
fi

WORKING_DIR=$1

BUILD_DIR="$WORKING_DIR/iree-org"
BUILD_SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
UE_SOURCE_DIR="$BUILD_SCRIPT_DIR/../../Source"
UE_PLUGIN_ROOT="$BUILD_SCRIPT_DIR/../../../.."

echo Plugin root dir: "$UE_PLUGIN_ROOT"
echo Build dir: "$BUILD_DIR"
echo Source dir: "$UE_SOURCE_DIR"

on_error() {
	local exit_code=$?
	local line=${BASH_LINENO[0]}

	echo "Error: command '${BASH_COMMAND}' failed at line ${line} with exit code ${exit_code}" >&2
	exit "$exit_code"
}
trap on_error ERR

# Common rsync options
# note: -L to copy "resolved" symlinks (e.g. ld.lld)
RSYNC_OPTIONS=(-a -L)

sync() { rsync "${RSYNC_OPTIONS[@]}" -- "$@"; }

echo =========================================
echo ========= INSTALLING PACKAGES ===========
echo =========================================

die() { echo "Error: $*" >&2; exit 1; }

commands=(awk rsync p4)
for p in "${commands[@]}"; do
	command -v "$p" >/dev/null 2>&1 || die "$p is required but not found."
done

echo Successfully checked for required commands.

echo =========================================
echo ======== Copying Compiler Files =========
echo =========================================

TARGET_IREE_FOLDER="$UE_PLUGIN_ROOT/Source/ThirdParty/IREE"
BINARIES_FOLDER="$UE_PLUGIN_ROOT/Binaries/ThirdParty/IREE/Mac"
mkdir -p "$BINARIES_FOLDER" 2>/dev/null

sync "$BUILD_DIR/iree-compiler/llvm-project/bin/ld.lld" "$BINARIES_FOLDER/"
sync "$BUILD_DIR/iree-compiler/llvm-project/bin/ld64.lld" "$BINARIES_FOLDER/"
sync "$BUILD_DIR/iree-compiler/tools/iree-compile" "$BINARIES_FOLDER/"
sync "$BUILD_DIR/iree-compiler/lib/libIREECompiler.dylib" "$BINARIES_FOLDER/"

# Add the ./ folder to the compilers rpath to find the shared lib in the same folder
install_name_tool -add_rpath "@executable_path/." "$BINARIES_FOLDER/iree-compile"
install_name_tool -delete_rpath "$BUILD_DIR/iree-compiler/lib" "$BINARIES_FOLDER/iree-compile"

echo
echo Copying Compiler Files: Done
echo


echo =========================================
echo ====== Copying NNEMlirTools Files =======
echo =========================================

MLIR_TOOLS_INCLUDE_DIR="$UE_PLUGIN_ROOT/Source/ThirdParty/NNEMlirTools/Internal"
MLIR_TOOLS_BINARIES_DIR="$UE_PLUGIN_ROOT/Binaries/ThirdParty/NNEMlirTools/Mac"
mkdir -p "$MLIR_TOOLS_INCLUDE_DIR" 2>/dev/null
mkdir -p "$MLIR_TOOLS_BINARIES_DIR" 2>/dev/null

sync "$BUILD_DIR/NNEMlirTools/libNNEMlirTools.dylib" "$MLIR_TOOLS_BINARIES_DIR/"
sync "$UE_SOURCE_DIR/NNEMlirTools/NNERuntimeIREE/Include/NNEMlirTools.h" "$MLIR_TOOLS_INCLUDE_DIR/"
sync "$UE_SOURCE_DIR/NNEMlirTools/NNERuntimeIREE/Include/NNEMlirTools_cxx_api.h" "$MLIR_TOOLS_INCLUDE_DIR/"

echo
echo Copying NNEMlirTools Files: Done
echo


echo =========================================
echo =========== Copying Mac Files ===========
echo =========================================

LIBRARIES_FOLDER="$TARGET_IREE_FOLDER/Lib/Mac"
mkdir -p "$LIBRARIES_FOLDER" 2>/dev/null

sync "$BUILD_DIR/iree-runtime-mac/iree/build_tools/third_party/flatcc/libflatcc_parsing.a" "$LIBRARIES_FOLDER/"
sync "$BUILD_DIR/iree-runtime-mac/libireert.a" "$LIBRARIES_FOLDER/"

echo
echo Copying Mac Files: Done
echo


echo =========================================
echo ============== Save to CL ===============
echo =========================================

# Check for P4 CLI client
if ! command -v p4 >/dev/null 2>&1; then
	echo "Error: Perforce CLI (p4) not found in PATH." >&2
	exit 1
fi

# Check connection to server
if ! p4 -G info >/dev/null 2>&1; then
	echo "Error: Unable to reach Perforce server." >&2
	exit 1
fi

# Check logged in
if ! p4 login -s >/dev/null 2>&1; then
	echo "Starting interactive login..."
	if ! p4 login; then
		echo "Error: Login failed." >&2
		exit 1
	fi
fi

CL_DESC="IREE Mac install"

CL=$(
	{
		printf 'Change: new\n'
		printf 'Description:\n'
		printf '%s' "$CL_DESC" | sed 's/^/\t/'
	}   | p4 change -i \
		| sed -n 's/^Change \([0-9][0-9]*\).*/\1/p' | head -n1
)

if [[ -z "${CL}" ]]; then
	echo "Failed to create change list (no CL number returned)." >&2
	exit 1
fi

echo "Created changelist #${CL}"

BINARIES_FOLDER="$(cd -- "$BINARIES_FOLDER" && pwd -P)"
LIBRARIES_FOLDER="$(cd -- "$LIBRARIES_FOLDER" && pwd -P)"
MLIR_TOOLS_INCLUDE_DIR="$(cd -- "$MLIR_TOOLS_INCLUDE_DIR" && pwd -P)"
MLIR_TOOLS_BINARIES_DIR="$(cd -- "$MLIR_TOOLS_BINARIES_DIR" && pwd -P)"

# Reconcile modified directories
p4 reconcile -a -e -d -c "$CL" "$BINARIES_FOLDER/..."
p4 reconcile -a -e -d -c "$CL" "$LIBRARIES_FOLDER/..."
p4 reconcile -a -e -d -c "$CL" "$MLIR_TOOLS_INCLUDE_DIR/..."
p4 reconcile -a -e -d -c "$CL" "$MLIR_TOOLS_BINARIES_DIR/..."

echo
echo Save to CL: Done
echo