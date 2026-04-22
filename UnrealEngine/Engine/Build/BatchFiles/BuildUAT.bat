@echo off

rem ## Unreal Engine AutomationTool build script
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
if not exist ..\Build\BatchFiles\BuildUAT.bat goto Error_BatchFileInWrongLocation

rem ## Verify that dotnet is present
call "%~dp0GetDotnetPath.bat"
if errorlevel 1 goto Error_NoDotnetSDK

rem Ensure UnrealBuildTool is up to date as it is a prereq
call "%~dp0\BuildUBT.bat"
if errorlevel 1 goto Error_UBTCompileFailed

rem ## Properties
set DEPENDS_FILE=..\Intermediate\Build\AutomationTool.dep.csv 
set TEMP_DEPENDS_FILE=..\Intermediate\Build\AutomationTool.dep.csv.tmp

rem ## Command line arguments
set MSBUILD_LOGLEVEL=%1
if not defined %MSBUILD_LOGLEVEL set MSBUILD_LOGLEVEL=quiet

rem Ensure intermediate directory exists
if not exist ..\Intermediate\Build mkdir ..\Intermediate\Build >nul 2>&1

rem Force build by deleting dependency file
if /I "%2" == "FORCE" del %DEPENDS_FILE% >nul 2>&1

rem Check to see if the files in the UnrealBuildTool solution have changed
call "%~dp0DotnetDepends.bat" Programs\AutomationTool\AutomationTool.sln %TEMP_DEPENDS_FILE% %MSBUILD_LOGLEVEL%
if errorlevel 1 goto Error_UATDependsFailed

:Check_UpToDate
if not exist ..\Binaries\DotNET\AutomationTool\AutomationTool.dll goto Build_AutomationTool
rem per https://ss64.com/nt/fc.html using redirection syntax rather than errorlevel, based on observed inconsistent results from this function
fc /C "%TEMP_DEPENDS_FILE%" %DEPENDS_FILE% >nul 2>&1 && (
	set RUNUAT_EXITCODE=0
	goto Exit
)

:Build_AutomationTool
echo Building AutomationTool...
dotnet build Programs\AutomationTool\AutomationTool.csproj -c Development -p AutomationToolProjectOnly=true -v %MSBUILD_LOGLEVEL%
if errorlevel 1 goto Error_UATCompileFailed

echo Publishing AutomationTool...
if exist ..\Binaries\DotNET\AutomationTool del /f /q ..\Binaries\DotNET\AutomationTool\* >nul 2>&1
if not exist ..\Binaries\DotNET\AutomationTool mkdir ..\Binaries\DotNET\AutomationTool >nul 2>&1
dotnet publish Programs\AutomationTool\AutomationTool.csproj -c Development -p AutomationToolProjectOnly=true --output ..\Binaries\DotNET\AutomationTool --no-build -v %MSBUILD_LOGLEVEL%
if errorlevel 1 goto Error_UATCompileFailed

rem record input files - regardless of how we got here, these are now our point of reference
move /y "%TEMP_DEPENDS_FILE%" %DEPENDS_FILE% >nul

goto Exit


:Error_BatchFileInWrongLocation
echo.
echo BuildUAT ERROR: The batch file does not appear to be located in the /Engine/Build/BatchFiles directory.  This script must be run from within that directory.
echo.
set RUNUAT_EXITCODE=1
goto Exit

:Error_NoDotnetSDK
echo.
echo RunUBT ERROR: Unable to find a install of Dotnet SDK.  Please make sure you have it installed and that `dotnet` is a globally available command.
echo.
set RUNUAT_EXITCODE=1
goto Exit

:Error_UATDependsFailed
echo.
echo RunUAT ERROR: AutomationTool failed to check dependencies.
echo.
set RUNUAT_EXITCODE=1
goto Exit

:Error_UATCompileFailed
echo.
echo RunUAT ERROR: AutomationTool failed to compile.
echo.
set RUNUAT_EXITCODE=1
goto Exit

:Error_UBTCompileFailed
set RUNUAT_EXITCODE=1
goto Exit

:Exit
rem ## Clean up temp files
del "%TEMP_DEPENDS_FILE%" >nul 2>&1
rem ## Restore original CWD in case we change it
popd
exit /B %RUNUAT_EXITCODE%
