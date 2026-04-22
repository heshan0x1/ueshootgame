@echo off
setlocal

rem Copyright Epic Games, Inc. All Rights Reserved.

set LIBRARY_NAME=OpenUSD
set REPOSITORY_NAME=OpenUSD

set BUILD_SCRIPT_NAME=%~n0%~x0
set BUILD_SCRIPT_LOCATION=%~dp0

rem Get version and architecture from arguments.
set LIBRARY_VERSION=%1
if [%LIBRARY_VERSION%]==[] goto usage

set ARCH_NAME=%2
if [%ARCH_NAME%]==[] goto usage

rem This path may be adjusted to point to wherever the OpenUSD source is
rem located. It is typically obtained by either downloading a zip/tarball of
rem the source code, or more commonly by cloning the GitHub repository, e.g.:
rem     git clone --branch <version tag> https://github.com/PixarAnimationStudios/OpenUSD.git OpenUSD_src
rem Then from inside the cloned OpenUSD_src directory, apply all patches sitting
rem next to this build script:
rem     git apply <build script location>/OpenUSD_*.patch
rem Note also that this path may be emitted as part of OpenUSD error messages,
rem so it is suggested that it not reveal any sensitive information.
set OPENUSD_SOURCE_LOCATION=C:\%REPOSITORY_NAME%_src

rem Set as VS2015 for backwards compatibility even though VS2022 is used
rem when building.
set COMPILER_VERSION_NAME=VS2015
set PLATFORM_NAME=Win64

set UE_ENGINE_LOCATION=%BUILD_SCRIPT_LOCATION%\..\..\..\..\..\..

rem Architecture-dependent strings and paths.
set ARCH_STRING=X64
set PYTHON_PLATFORM_NAME=%PLATFORM_NAME%
set ENGINE_BINARIES_LOCATION="%UE_ENGINE_LOCATION%\Binaries\%PLATFORM_NAME%"
if /I "%ARCH_NAME%"=="ARM64" (
    set ARCH_STRING=Arm64
    set PYTHON_PLATFORM_NAME=WinArm64
    set ENGINE_BINARIES_LOCATION="%UE_ENGINE_LOCATION%\Binaries\%PLATFORM_NAME%\arm64"
)

rem CMake runs the Python interpreter as part of its validation, so if we're
rem cross-compiling, make sure to run the native executable.
set PYTHON_NATIVE_PLATFORM_NAME=%PLATFORM_NAME%
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set PYTHON_NATIVE_PLATFORM_NAME=WinArm64
)

set UE_SOURCE_THIRD_PARTY_LOCATION=%UE_ENGINE_LOCATION%\Source\ThirdParty
set TBB_LOCATION=%UE_SOURCE_THIRD_PARTY_LOCATION%\Intel\TBB\Deploy\oneTBB-2021.13.0
set TBB_LIB_LOCATION=%TBB_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib
set TBB_CMAKE_LOCATION=%TBB_LIB_LOCATION%\cmake\TBB
set IMATH_LOCATION=%UE_SOURCE_THIRD_PARTY_LOCATION%\Imath\Deploy\Imath-3.1.12
set IMATH_LIB_LOCATION=%IMATH_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib
set IMATH_CMAKE_LOCATION=%IMATH_LIB_LOCATION%\cmake\Imath
set OPENSUBDIV_LOCATION=%UE_SOURCE_THIRD_PARTY_LOCATION%\OpenSubdiv\Deploy\OpenSubdiv-3.6.0
set OPENSUBDIV_INCLUDE_DIR=%OPENSUBDIV_LOCATION%\include
set OPENSUBDIV_LIB_LOCATION=%OPENSUBDIV_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib
set ALEMBIC_LOCATION=%UE_SOURCE_THIRD_PARTY_LOCATION%\Alembic\Deploy\alembic-1.8.7
set ALEMBIC_LIB_LOCATION=%ALEMBIC_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib
set ALEMBIC_CMAKE_LOCATION=%ALEMBIC_LIB_LOCATION%\cmake\Alembic
set MATERIALX_LOCATION=%UE_SOURCE_THIRD_PARTY_LOCATION%\MaterialX\Deploy\MaterialX-1.39.3
set MATERIALX_LIB_LOCATION=%MATERIALX_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib
set MATERIALX_CMAKE_LOCATION=%MATERIALX_LIB_LOCATION%\cmake\MaterialX

set PYTHON_EXECUTABLE_LOCATION=%UE_ENGINE_LOCATION%\Binaries\ThirdParty\Python3\%PYTHON_NATIVE_PLATFORM_NAME%\python.exe
set PYTHON_SOURCE_LOCATION=%UE_SOURCE_THIRD_PARTY_LOCATION%\Python3\%PYTHON_PLATFORM_NAME%
set PYTHON_INCLUDE_LOCATION=%PYTHON_SOURCE_LOCATION%\include
set PYTHON_LIBRARY_LOCATION=%PYTHON_SOURCE_LOCATION%\libs\python311.lib

set UE_MODULE_USD_LOCATION=%BUILD_SCRIPT_LOCATION%

set BUILD_LOCATION=%UE_MODULE_USD_LOCATION%\Intermediate

rem OpenUSD build products are written into a deployment directory and must
rem then be manually copied from there into place.
set INSTALL_LOCATION=%BUILD_LOCATION%\Deploy\%REPOSITORY_NAME%-%LIBRARY_VERSION%

if exist %BUILD_LOCATION% (
    rmdir %BUILD_LOCATION% /S /Q)

mkdir %BUILD_LOCATION%
pushd %BUILD_LOCATION%

set NUM_CPU=8
set BUILD_TYPE=Release

echo Configuring build for %LIBRARY_NAME% version %LIBRARY_VERSION%...
cmake -G "Visual Studio 17 2022" %OPENUSD_SOURCE_LOCATION%^
    -A %ARCH_NAME%^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_LOCATION%"^
    -DCMAKE_PREFIX_PATH="%TBB_CMAKE_LOCATION%;%IMATH_CMAKE_LOCATION%;%ALEMBIC_CMAKE_LOCATION%;%MATERIALX_CMAKE_LOCATION%"^
    -DPython3_EXECUTABLE="%PYTHON_EXECUTABLE_LOCATION%"^
    -DPython3_INCLUDE_DIR="%PYTHON_INCLUDE_LOCATION%"^
    -DPython3_LIBRARY="%PYTHON_LIBRARY_LOCATION%"^
    -DPXR_BUILD_ALEMBIC_PLUGIN=ON^
    -DPXR_ENABLE_HDF5_SUPPORT=OFF^
    -DOPENSUBDIV_INCLUDE_DIR="%OPENSUBDIV_INCLUDE_DIR%"^
    -DOPENSUBDIV_ROOT_DIR="%OPENSUBDIV_LIB_LOCATION%"^
    -DPXR_ENABLE_MATERIALX_SUPPORT=ON^
    -DBUILD_SHARED_LIBS=ON^
    -DPXR_BUILD_TESTS=OFF^
    -DPXR_BUILD_EXAMPLES=OFF^
    -DPXR_BUILD_TUTORIALS=OFF^
    -DPXR_BUILD_USD_TOOLS=OFF^
    -DPXR_BUILD_IMAGING=ON^
    -DPXR_BUILD_USD_IMAGING=ON^
    -DPXR_BUILD_USDVIEW=OFF^
    -DCMAKE_CXX_FLAGS="/Zm150"
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building %LIBRARY_NAME% for %BUILD_TYPE%...
cmake --build . --config %BUILD_TYPE% -j%NUM_CPU%
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing %LIBRARY_NAME% for %BUILD_TYPE%...
cmake --install . --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /B %errorlevel%

popd

set BUILD_BIN_LOCATION=%INSTALL_LOCATION%\bin
set BUILD_LIB_LOCATION=%INSTALL_LOCATION%\lib

set INSTALL_BIN_LOCATION=%INSTALL_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%\bin
set INSTALL_LIB_LOCATION=%INSTALL_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib

echo Removing command-line tools...
rmdir /S /Q "%BUILD_BIN_LOCATION%"

echo Moving shared libraries to bin directory...
mkdir %INSTALL_BIN_LOCATION%
move "%BUILD_LIB_LOCATION%\*.dll" "%INSTALL_BIN_LOCATION%"
if exist "%BUILD_LIB_LOCATION%\*.pdb" (
    move "%BUILD_LIB_LOCATION%\*.pdb" "%INSTALL_BIN_LOCATION%"
)

echo Moving import libraries to lib directory...
mkdir %INSTALL_LIB_LOCATION%
move "%BUILD_LIB_LOCATION%\*.lib" "%INSTALL_LIB_LOCATION%"

echo Moving built-in %LIBRARY_NAME% plugins to UsdResources plugins directory...
set INSTALL_RESOURCES_LOCATION=%INSTALL_LOCATION%\Resources\UsdResources\%PLATFORM_NAME%\%ARCH_STRING%
set INSTALL_RESOURCES_PLUGINS_LOCATION=%INSTALL_RESOURCES_LOCATION%\plugins
mkdir %INSTALL_RESOURCES_LOCATION%
move "%BUILD_LIB_LOCATION%\usd" "%INSTALL_RESOURCES_PLUGINS_LOCATION%"

echo Moving %LIBRARY_NAME% plugin shared libraries to bin directory...
set INSTALL_PLUGIN_LOCATION=%INSTALL_LOCATION%\plugin
set INSTALL_PLUGIN_USD_LOCATION=%INSTALL_PLUGIN_LOCATION%\usd
move "%INSTALL_PLUGIN_USD_LOCATION%\*.dll" "%INSTALL_BIN_LOCATION%"
if exist "%INSTALL_PLUGIN_USD_LOCATION%\*.pdb" (
    move "%INSTALL_PLUGIN_USD_LOCATION%\*.pdb" "%INSTALL_BIN_LOCATION%"
)

echo Moving %LIBRARY_NAME% plugin import libraries to lib directory...
move "%INSTALL_PLUGIN_USD_LOCATION%\*.lib" "%INSTALL_LIB_LOCATION%"

echo Removing top-level %LIBRARY_NAME% plugins plugInfo.json file...
del "%INSTALL_PLUGIN_USD_LOCATION%\plugInfo.json"

echo Moving %LIBRARY_NAME% plugin resource directories to UsdResources plugins directory
move "%INSTALL_PLUGIN_USD_LOCATION%\hdStorm" "%INSTALL_RESOURCES_PLUGINS_LOCATION%"
move "%INSTALL_PLUGIN_USD_LOCATION%\hioAvif" "%INSTALL_RESOURCES_PLUGINS_LOCATION%"
move "%INSTALL_PLUGIN_USD_LOCATION%\sdrGlslfx" "%INSTALL_RESOURCES_PLUGINS_LOCATION%"
move "%INSTALL_PLUGIN_USD_LOCATION%\usdAbc" "%INSTALL_RESOURCES_PLUGINS_LOCATION%"
move "%INSTALL_PLUGIN_USD_LOCATION%\usdShaders" "%INSTALL_RESOURCES_PLUGINS_LOCATION%"

rmdir "%INSTALL_PLUGIN_USD_LOCATION%"
rmdir "%INSTALL_PLUGIN_LOCATION%"

echo Removing CMake files...
rmdir /S /Q "%INSTALL_LOCATION%\cmake"
del /S /Q "%INSTALL_LOCATION%\*.cmake"

echo Removing Python .pyc files...
del /S /Q "%INSTALL_LOCATION%\*.pyc"

echo Removing pxr.Tf.testenv Python module...
rmdir /S /Q "%INSTALL_LOCATION%\lib\python\pxr\Tf\testenv"

echo Moving Python modules to Content
set INSTALL_CONTENT_LOCATION=%INSTALL_LOCATION%\Content\Python\Lib\%PLATFORM_NAME%\%ARCH_STRING%\site-packages
mkdir %INSTALL_CONTENT_LOCATION%
move "%BUILD_LIB_LOCATION%\python\pxr" "%INSTALL_CONTENT_LOCATION%"
rmdir "%BUILD_LIB_LOCATION%\python"

rmdir %BUILD_LIB_LOCATION%

rem The locations of the shared libraries where they will live when ultimately
rem deployed are used to generate relative paths for use as LibraryPaths in
rem plugInfo.json files.
rem The OpenUSD plugins all exist at the same directory level, so any of them can
rem be used to generate a relative path.
set USD_PLUGIN_LOCATION="%UE_ENGINE_LOCATION%\Plugins\Runtime\USDCore\Resources\UsdResources\%PLATFORM_NAME%\%ARCH_STRING%\plugins\usd"
set USD_LIBS_LOCATION=%ENGINE_BINARIES_LOCATION%

echo Adjusting plugInfo.json LibraryPath fields...
for /f "delims=" %%o IN ('%PYTHON_EXECUTABLE_LOCATION% -c "import os.path; print(os.path.relpath(r'%USD_LIBS_LOCATION%', r'%USD_PLUGIN_LOCATION%'))"') do set USD_PLUGIN_TO_USD_LIBS_REL_PATH=%%o

pushd %INSTALL_RESOURCES_PLUGINS_LOCATION%

for /f %%f IN ('findstr /S /M LibraryPath *') do (
    %PYTHON_EXECUTABLE_LOCATION% %BUILD_SCRIPT_LOCATION%\modify_plugInfo_file.py "%%f" "%USD_PLUGIN_TO_USD_LIBS_REL_PATH%"
)

popd

echo Done.

goto :eof

:usage
echo Usage: %BUILD_SCRIPT_NAME% ^<version^> ^<architecture: x64 or ARM64^>
exit /B 1

endlocal
