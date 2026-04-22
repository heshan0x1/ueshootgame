@echo off

rem ## Unreal Engine code build script
rem ## Copyright Epic Games, Inc. All Rights Reserved.
rem ##
rem ## This script is expecting to exist in the /Engine/Build/BatchFiles directory.  It will not work correctly
rem ## if you copy it to a different location and run it.
rem ##
rem ##     %1 is the game name
rem ##     %2 is the platform name
rem ##     %3 is the configuration name
rem ##     additional args are passed directly to UnrealBuildTool

setlocal enabledelayedexpansion

set ExitCode=
set WaitingForLock=0
set LockFile=%~f0
set LockFile=%LockFile:\=-%
set LockFile=%tmp%\%LockFile::=%.lock

call :Lock %*
exit /B !ExitCode!

:Lock
9>&2 2>nul (call :LockAndRestoreStdErr %* 9>"%LockFile%") || (
	if "!ExitCode!" NEQ "" exit /B !ExitCode!
	if %WaitingForLock% == 0 set WaitingForLock=1 && echo %~nx0 is already running, waiting for existing script to terminate...
	ping 127.0.0.1 -n 2 >nul
	goto :Lock
)
exit /B !ExitCode!

:LockAndRestoreStdErr
call :Main %* 2>&9
exit /B !ExitCode!

:Main

rem ## First, make sure the batch file exists in the folder we expect it to.  This is necessary in order to
rem ## verify that our relative path to the /Engine/Source directory is correct
if not exist "%~dp0..\..\Source" goto Error_BatchFileInWrongLocation

rem ## Change the CWD to /Engine/Source.  We always need to run UnrealBuildTool from /Engine/Source!
pushd "%~dp0\..\..\Source"
if not exist ..\Build\BatchFiles\Build.bat goto Error_BatchFileInWrongLocation

set UBTPath="..\..\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"

rem ## Verify that dotnet is present
call "%~dp0GetDotnetPath.bat"
if errorlevel 1 goto Error_NoDotnetSDK
REM ## Skip msbuild detection if using dotnet as this is done for us by dotnet-cli

rem ## Compile UBT if the project file exists
:ReadyToBuildUBT
set ProjectFile="Programs\UnrealBuildTool\UnrealBuildTool.csproj"
if not exist %ProjectFile% goto NoProjectFile

if exist "%~dp0\BuildUBT.bat" (
	call "%~dp0\BuildUBT.bat"
	if errorlevel 1 goto Error_UBTCompileFailed
) ELSE (
	rem ## Only build if UnrealBuildTool.dll is missing if BuildUBT.bat does not exist
	if not exist %UBTPath% (
		echo Building UnrealBuildTool with dotnet...
		dotnet build %ProjectFile% -c Development -v quiet
		dotnet publish %ProjectFile% -c Development --output ..\Binaries\DotNET\UnrealBuildTool --no-build -v quiet
		if errorlevel 1 goto Error_UBTCompileFailed
	)
)

:NoProjectFile

rem ## Run UBT
:ReadyToBuild
if not exist %UBTPath% goto Error_UBTMissing
echo Running UnrealBuildTool: dotnet %UBTPath% %*
dotnet %UBTPath% %*
set ExitCode=!ERRORLEVEL!
EXIT /B !ExitCode!

:Error_BatchFileInWrongLocation
echo ERROR: The batch file does not appear to be located in the Engine/Build/BatchFiles directory. This script must be run from within that directory.
set ExitCode=999
EXIT /B !ExitCode!

:Error_NoDotnetSDK
echo ERROR: Unable to find an install of Dotnet SDK.  Please make sure you have it installed and that `dotnet` is a globally available command.
set ExitCode=999
EXIT /B !ExitCode!

:Error_UBTCompileFailed
echo ERROR: Failed to build UnrealBuildTool.
set ExitCode=999
EXIT /B !ExitCode!

:Error_UBTMissing
echo ERROR: UnrealBuildTool.dll not found in %UBTPath%
set ExitCode=999
EXIT /B !ExitCode!
