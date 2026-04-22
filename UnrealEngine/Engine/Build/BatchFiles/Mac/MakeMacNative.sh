#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.

SourceDir=$1
DestinationDir=$2
AppName=$3			# including the .app extension

# Delete previous bundle if any
rm -rf "$DestinationDir/$AppName"
mkdir -p "$DestinationDir/$AppName/Wrapper"
ln -s "Wrapper/$AppName" "$DestinationDir/$AppName/WrappedBundle"
rsync -au "$SourceDir/$AppName" "$DestinationDir/$AppName/Wrapper"
