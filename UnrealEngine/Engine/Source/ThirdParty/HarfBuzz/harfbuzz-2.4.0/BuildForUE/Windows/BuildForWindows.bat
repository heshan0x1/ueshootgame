@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

set PATH_TO_CMAKE_FILE=%CD%\..\

REM At this stage, while it now uses latest VS, even it still uses VS2015 folders. A bigger update (harfbuzz version + usage of UAT will clean all that)
REM Temporary build directories (used as working directories when running CMake)
set VS2015_X64_PATH="%PATH_TO_CMAKE_FILE%\..\lib\Win64\VS2015\Build"
set VS2015_ARM64_PATH="%PATH_TO_CMAKE_FILE%\..\lib\Win64\VS2015\ARM64\Build"

REM Build for VS2015 (64-bit)
echo Generating HarfBuzz solution for VS2022 (WIN64 64-bit)...
if exist %VS2015_X64_PATH% (rmdir %VS2015_X64_PATH% /s/q)
mkdir %VS2015_X64_PATH%
cd %VS2015_X64_PATH%
cmake %PATH_TO_CMAKE_FILE%

FOR /F "tokens=* USEBACKQ" %%F IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property productpath`) DO (
SET devenvexe=%%F
)
echo Will compile using %devenvexe%

echo Building HarfBuzz solution for VS2015 (64-bit, Debug)...
"%devenvexe%" harfbuzz.sln /Build Debug

echo Building HarfBuzz solution for VS2015 (64-bit, Release)...
"%devenvexe%" harfbuzz.sln /Build Release

echo Building HarfBuzz solution for VS2015 (64-bit, RelWithDebInfo)...
"%devenvexe%" harfbuzz.sln /Build RelWithDebInfo

cd %PATH_TO_CMAKE_FILE%
rmdir %VS2015_X64_PATH% /s/q

REM Build for VS2022 (ARM64 64-bit)
echo Generating HarfBuzz solution for VS2022 (ARM64 64-bit)...
if exist %VS2015_ARM64_PATH% (rmdir %VS2015_ARM64_PATH% /s/q)
mkdir %VS2015_ARM64_PATH%
cd %VS2015_ARM64_PATH%
cmake -G "Visual Studio 17 2022" %PATH_TO_CMAKE_FILE% -A ARM64
echo Building HarfBuzz solution for VS2022 (ARM64 64-bit, Debug)...
"%devenvexe%" harfbuzz.sln /Build Debug
echo Building HarfBuzz solution for VS2022 (ARM64 64-bit, Release)...
"%devenvexe%" harfbuzz.sln /Build Release
echo Building HarfBuzz solution for VS2022 (ARM64 64-bit, RelWithDebInfo)...
"%devenvexe%" harfbuzz.sln /Build RelWithDebInfo
rmdir %VS2015_ARM64_PATH% /s/q

endlocal
