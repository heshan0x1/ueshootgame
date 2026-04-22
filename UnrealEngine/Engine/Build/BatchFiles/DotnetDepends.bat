@echo off

rem ## Unreal Engine UnrealBuildTool build script
rem ## Copyright Epic Games, Inc. All Rights Reserved.
rem ##
rem ## This script is expecting to exist in the Engine/Build/BatchFiles directory.  It will not work correctly
rem ## if you copy it to a different location and run it.

setlocal

rem ## First, make sure the batch file exists in the folder we expect it to.  This is necessary in order to
rem ## verify that our relative path to the /Engine/Source directory is correct
if not exist "%~dp0..\..\Source" goto Error_BatchFileInWrongLocation

rem ## Change the CWD to /Engine/Source.
pushd "%~dp0..\..\Source"
if not exist ..\Build\BatchFiles\DotnetDepends.bat goto Error_DependsFailed

rem ## Verify that dotnet is present
call "%~dp0GetDotnetPath.bat"
if errorlevel 1 goto Error_DependsFailed

set SLN=%1
set DEPENDS_FILE=%2
set MSBUILD_LOGLEVEL=%3
if not defined %MSBUILD_LOGLEVEL set MSBUILD_LOGLEVEL=quiet

:tempRetry
set TEMP_DEPENDS_PATH=%TEMP%\DotnetDepends-%random%%random%
if exist %TEMP_DEPENDS_PATH% goto :tempRetry
set TEMP_DEPENDS_FILE=%TEMP_DEPENDS_PATH%\out.csv

rem Check to see if the files in the UnrealBuildTool solution have changed
mkdir "%TEMP_DEPENDS_PATH%"
dotnet msbuild "%SLN%" -t:Scan -p:Configuration=Development -p:Platform="Any CPU" -p:OutputPath="%TEMP_DEPENDS_PATH%\\" -p:DependsEncoding=Ascii -noLogo -v:%MSBUILD_LOGLEVEL%
if errorlevel 1 goto Error_DependsFailed
copy /b "%TEMP_DEPENDS_PATH%\*.dep.csv" "%TEMP_DEPENDS_FILE%" >nul 2>&1
"%SYSTEMROOT%\System32\sort.exe" /+64 /unique /o "%DEPENDS_FILE%" "%TEMP_DEPENDS_FILE%"
if errorlevel 1 goto Error_DependsFailed

set DEPENDS_EXITCODE=0
goto Exit

:Error_DependsFailed
set DEPENDS_EXITCODE=1
goto Exit

:Exit
rem ## Clean up temp files
rmdir /s /q "%TEMP_DEPENDS_PATH%" >nul 2>&1
exit /B %DEPENDS_EXITCODE%