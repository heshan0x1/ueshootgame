#!/bin/bash
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
open "$SCRIPT_DIR"/Mac/BuildStorageTool.app --args "$@"