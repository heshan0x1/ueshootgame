@echo off
setlocal

rem Copyright Epic Games, Inc. All Rights Reserved.

set LIBRARY_NAME=Blosc
set REPOSITORY_NAME=c-blosc

set BUILD_SCRIPT_NAME=%~n0%~x0
set BUILD_SCRIPT_LOCATION=%~dp0

rem Get version and architecture from arguments.
set LIBRARY_VERSION=%1
if [%LIBRARY_VERSION%]==[] goto usage

set ARCH_NAME=%2
if [%ARCH_NAME%]==[] goto usage

rem Set as VS2015 for backwards compatibility even though VS2022 is used
rem when building.
set COMPILER_VERSION_NAME=VS2015

set UE_MODULE_LOCATION=%BUILD_SCRIPT_LOCATION%
set UE_SOURCE_THIRD_PARTY_LOCATION=%UE_MODULE_LOCATION%\..

set ZLIB_LOCATION=%UE_SOURCE_THIRD_PARTY_LOCATION%\zlib\v1.2.8
set ZLIB_INCLUDE_LOCATION=%ZLIB_LOCATION%\include\Win64\%COMPILER_VERSION_NAME%
set ZLIB_LIB_LOCATION=%ZLIB_LOCATION%\lib\Win64\%COMPILER_VERSION_NAME%\Release\zlibstatic.lib
if %2==ARM64 set ZLIB_LIB_LOCATION=%ZLIB_LOCATION%\lib\Win64\%COMPILER_VERSION_NAME%\arm64\Release\zlibstatic.lib

set SOURCE_LOCATION=%UE_MODULE_LOCATION%\%REPOSITORY_NAME%-%LIBRARY_VERSION%

set BUILD_LOCATION=%UE_MODULE_LOCATION%\Intermediate

set INSTALL_INCLUDEDIR=include

set INSTALL_LOCATION=%UE_MODULE_LOCATION%\Deploy\%REPOSITORY_NAME%-%LIBRARY_VERSION%
set INSTALL_INCLUDE_LOCATION=%INSTALL_LOCATION%\%INSTALL_INCLUDEDIR%
set INSTALL_WIN_ARCH_LOCATION=%INSTALL_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%

if exist %BUILD_LOCATION% (
    rmdir %BUILD_LOCATION% /S /Q)
if exist %INSTALL_INCLUDE_LOCATION% (
    rmdir %INSTALL_INCLUDE_LOCATION% /S /Q)
if exist %INSTALL_WIN_ARCH_LOCATION% (
    rmdir %INSTALL_WIN_ARCH_LOCATION% /S /Q)

mkdir %BUILD_LOCATION%
pushd %BUILD_LOCATION%

set NUM_CPU=8

rem Note that we patch the source for the version of LZ4 that is bundled with
rem Blosc to add a prefix to all of its functions. This ensures that the symbol
rem names do not collide with the version(s) of LZ4 that are embedded in the
rem engine.

rem Copy the source into the build directory so that we can apply patches.
set BUILD_SOURCE_LOCATION=%BUILD_LOCATION%\%REPOSITORY_NAME%-%LIBRARY_VERSION%

robocopy %SOURCE_LOCATION% %BUILD_SOURCE_LOCATION% /E /NFL /NDL /NJH /NJS

pushd %BUILD_SOURCE_LOCATION%
git apply %UE_MODULE_LOCATION%\Blosc_v1.21.0_LZ4_PREFIX.patch
if %errorlevel% neq 0 exit /B %errorlevel%
popd

set C_FLAGS="-DLZ4_PREFIX=BLOSC_"

set ADDITIONAL_FLAGS=
if %2==ARM64 set ADDITIONAL_FLAGS=-DDEACTIVATE_SSE2=ON -DDEACTIVATE_AVX2=ON

echo Configuring build for %LIBRARY_NAME% version %LIBRARY_VERSION%...
cmake -G "Visual Studio 17 2022" %BUILD_SOURCE_LOCATION%^
    -A %ARCH_NAME%^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_LOCATION%"^
    -DPREFER_EXTERNAL_ZLIB=ON^
    -DZLIB_INCLUDE_DIR="%ZLIB_INCLUDE_LOCATION%"^
    -DZLIB_LIBRARY="%ZLIB_LIB_LOCATION%"^
    -DCMAKE_C_FLAGS="%C_FLAGS%"^
    -DCMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP=ON^
    -DBUILD_SHARED=OFF^
    -DBUILD_TESTS=OFF^
    -DBUILD_FUZZERS=OFF^
    -DBUILD_BENCHMARKS=OFF^
    -DCMAKE_DEBUG_POSTFIX=_d^
    %ADDITIONAL_FLAGS%
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building %LIBRARY_NAME% for Debug...
cmake --build . --config Debug -j%NUM_CPU%
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing %LIBRARY_NAME% for Debug...
cmake --install . --config Debug
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building %LIBRARY_NAME% for Release...
cmake --build . --config Release -j%NUM_CPU%
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing %LIBRARY_NAME% for Release...
cmake --install . --config Release
if %errorlevel% neq 0 exit /B %errorlevel%

popd

echo Removing pkgconfig files...
rmdir /S /Q "%INSTALL_LOCATION%\lib\pkgconfig"

echo Moving lib directory into place...
set INSTALL_LIB_DIR=%COMPILER_VERSION_NAME%\%ARCH_NAME%
set INSTALL_LIB_LOCATION=%INSTALL_LOCATION%\%INSTALL_LIB_DIR%
mkdir %INSTALL_LIB_LOCATION%
move "%INSTALL_LOCATION%\lib" "%INSTALL_LIB_LOCATION%"

echo Done.

goto :eof

:usage
echo Usage: %BUILD_SCRIPT_NAME% ^<version^> ^<architecture: x64 or ARM64^>
exit /B 1

endlocal
