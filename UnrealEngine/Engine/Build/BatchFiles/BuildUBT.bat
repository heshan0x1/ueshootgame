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
if not exist ..\Build\BatchFiles\BuildUBT.bat goto Error_BatchFileInWrongLocation

rem ## Verify that dotnet is present
call "%~dp0GetDotnetPath.bat"
if errorlevel 1 goto Error_NoDotnetSDK

rem ## Properties
set DEPENDS_FILE=..\Intermediate\Build\UnrealBuildTool.dep.csv
set TEMP_DEPENDS_FILE=..\Intermediate\Build\UnrealBuildTool.dep.csv.tmp

rem ## Command line arguments
set MSBUILD_LOGLEVEL=%1
if not defined %MSBUILD_LOGLEVEL set MSBUILD_LOGLEVEL=quiet

rem Ensure intermediate directory exists
if not exist ..\Intermediate\Build mkdir ..\Intermediate\Build >nul 2>&1

rem Force build by deleting dependency file
if /I "%2" == "FORCE" del %DEPENDS_FILE% >nul 2>&1

rem Check to see if the files in the UnrealBuildTool solution have changed
call "%~dp0DotnetDepends.bat" Programs\UnrealBuildTool\UnrealBuildTool.sln %TEMP_DEPENDS_FILE% %MSBUILD_LOGLEVEL%
if errorlevel 1 goto Error_UBTDependsFailed

:Check_UpToDate
if not exist ..\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll goto Build_UnrealBuildTool
rem per https://ss64.com/nt/fc.html using redirection syntax rather than errorlevel, based on observed inconsistent results from this function
fc /C "%TEMP_DEPENDS_FILE%" %DEPENDS_FILE% >nul 2>&1 && (
	set RUNUBT_EXITCODE=0
	goto Exit
)

:Build_UnrealBuildTool
echo Building UnrealBuildTool...
dotnet build Programs\UnrealBuildTool\UnrealBuildTool.csproj -c Development -v %MSBUILD_LOGLEVEL%
if errorlevel 1 goto Error_UBTCompileFailed

echo Publishing UnrealBuildTool...
if exist ..\Binaries\DotNET\UnrealBuildTool del /f /q ..\Binaries\DotNET\UnrealBuildTool\* >nul 2>&1
if not exist ..\Binaries\DotNET\UnrealBuildTool mkdir ..\Binaries\DotNET\UnrealBuildTool >nul 2>&1
dotnet publish Programs\UnrealBuildTool\UnrealBuildTool.csproj -c Development --output ..\Binaries\DotNET\UnrealBuildTool --no-build -v %MSBUILD_LOGLEVEL%
if errorlevel 1 goto Error_UBTCompileFailed

rem record input files - regardless of how we got here, these are now our point of reference
move /y "%TEMP_DEPENDS_FILE%" %DEPENDS_FILE% >nul

goto Exit


:Error_BatchFileInWrongLocation
echo.
echo BuildUBT ERROR: The batch file does not appear to be located in the /Engine/Build/BatchFiles directory.  This script must be run from within that directory.
echo.
set RUNUBT_EXITCODE=1
goto Exit

:Error_NoDotnetSDK
echo.
echo RunUBT ERROR: Unable to find a install of Dotnet SDK.  Please make sure you have it installed and that `dotnet` is a globally available command.
echo.
set RUNUBT_EXITCODE=1
goto Exit

:Error_UBTDependsFailed
echo.
echo RunUBT ERROR: UnrealBuildTool failed to check dependencies.
echo.
set RUNUBT_EXITCODE=1
goto Exit

:Error_UBTCompileFailed
echo.
echo RunUBT ERROR: UnrealBuildTool failed to compile.
echo.
set RUNUBT_EXITCODE=1
goto Exit

:Exit
rem ## Clean up temp files
del "%TEMP_DEPENDS_FILE%" >nul 2>&1
rem ## Restore original CWD in case we change it
popd
exit /B %RUNUBT_EXITCODE%
