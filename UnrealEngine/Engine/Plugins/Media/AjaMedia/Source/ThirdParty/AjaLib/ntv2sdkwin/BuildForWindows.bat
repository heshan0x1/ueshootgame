@Echo off
rem Copyright Epic Games, Inc. All Rights Reserved.

rem Setup part
setlocal
set AJANTV2_VERSION=17_5_0
set AJANTV2_LIB_NAME=ntv2_%AJANTV2_VERSION%
set AJANTV2_LIB_FOLDER=ntv2
set DEPLOY_FOLDER=..\..\..\..\ntv2lib-deploy
set AJANTV2_BUILD_FOLDER=cmake-build

if "%1"=="" (
	echo please specify architecture - x64 or arm64
	exit /b
)
set ARCH=%1

rem Download library source if not present
if not exist %AJANTV2_LIB_NAME%.zip (
	powershell -Command "Invoke-WebRequest https://github.com/aja-video/libajantv2/archive/refs/tags/%AJANTV2_LIB_NAME%.zip -OutFile %AJANTV2_LIB_NAME%.zip"
)
rem Remove previously extracted build library folder
if exist .\%AJANTV2_LIB_FOLDER% (
   rd /S /Q .\%AJANTV2_LIB_FOLDER%
)

echo Extracting %AJANTV2_LIB_NAME%.zip...
powershell -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('%AJANTV2_LIB_NAME%.zip', '%AJANTV2_LIB_FOLDER%')"
cd /d .\%AJANTV2_LIB_FOLDER%\
for /d %%D in (*) do ( 
	if exist "%%D\CMakeLists.txt" (
		echo using cmakelists.txt from "%%D"
		cd "%%D"
		goto :got_cmake
	)
)
echo couldn't find CMakeLists.txt in %cd%
exit /b
:got_cmake

mkdir %AJANTV2_BUILD_FOLDER%
cd %AJANTV2_BUILD_FOLDER%

rem Configure AjaNTV2 cmake and launch a release build
echo Configuring %ARCH% build environment...
if /i "%ARCH%"=="arm64" (
	REM this is needed to work around the fact __cpuid is not present in ntv2 until 17.5.0 beta 1
	set CMAKE_ARCH_ARGS=-DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /GR /EHsc /D__cpuid=__noop"
	call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64_arm64
) else (
	set CMAKE_ARCH_ARGS=
	call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" 
)

echo Configuring %ARCH% debug cmake config...
cmake -DCMAKE_BUILD_TYPE=Debug -DAJANTV2_DISABLE_PLUGIN_LOAD=ON -G "Visual Studio 17 2022" -A %ARCH% %CMAKE_ARCH_ARGS% ..
 
echo Building %ARCH% Debug build...
cmake --build . --config Debug

echo Configuring %ARCH% Release cmake config...
cmake -DCMAKE_BUILD_TYPE=Release -DAJANTV2_DISABLE_PLUGIN_LOAD=ON -G "Visual Studio 17 2022" -A %ARCH% %CMAKE_ARCH_ARGS% ..

echo Building %ARCH% Release build...
cmake --build . --config Release

rem Remove previous deployment file
if exist %DEPLOY_FOLDER% (
    rd /S /Q %DEPLOY_FOLDER%\lib\%ARCH%\
    rd /S /Q %DEPLOY_FOLDER%\includes\
)

echo Copying library for deployment ...
xcopy .\ajantv2\Debug\ajantv2d.lib %DEPLOY_FOLDER%\lib\%ARCH%\libajantv2d.lib* /Y
xcopy .\ajantv2\Release\ajantv2.lib %DEPLOY_FOLDER%\lib\%ARCH%\libajantv2.lib* /Y

echo Copying all headers for deployment ...
xcopy "..\*.h" "%DEPLOY_FOLDER%\includes\" /S /C /Y
xcopy "..\*.hh" "%DEPLOY_FOLDER%\includes\" /S /C /Y

endlocal
pause