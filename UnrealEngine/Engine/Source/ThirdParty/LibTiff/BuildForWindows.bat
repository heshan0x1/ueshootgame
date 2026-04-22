@echo off

set ZLIB_ROOT=%~dp0..\zlib\1.3
set JPEG_LIBRARY_ROOT=%~dp0..\libjpeg-turbo\3.0.0\lib\Win64\
set JPEG_INCLUDE_DIR=%~dp0..\FreeImage\FreeImage-3.18.0\Source\LibJPEG
REM... not ideal to pull in the header from FreeImage but that's the only checked-in place with jpeglib.h now

REM *** x64 ***


if exist Build (rmdir Build /s/q)
mkdir Build
pushd Build

cmake -G "Visual Studio 16 2019" -DBUILD_SHARED_LIBS=OFF
	-DZLIB_FOUND=TRUE ^
	-DZLIB_INCLUDE_DIR=%ZLIB_ROOT%\include ^
	-DZLIB_LIBRARY=%ZLIB_ROOT%\lib\Win64\Release\zlibstatic.lib ^
	-DZLIB_SUPPORT=1 ^
	-DJPEG_FOUND=TRUE ^
	-DJPEG_INCLUDE_DIR=%JPEG_INCLUDE_DIR%\ ^
	-DJPEG_LIBRARY=%JPEG_LIBRARY_ROOT%\Release\turbojpeg-static.lib ^
	-DJPEG_SUPPORT=TRUE ^
	../libtiff-v4.2.0/
"%_msbuild%msbuild.exe" tiff.sln /t:build /p:Configuration=Release

md ..\Lib\Win64\
copy /y libtiff\Release\tiff.lib ..\Lib\Win64\tiff.lib

popd






REM *** ARM64 ***

if exist Build (rmdir Build /s/q)
mkdir Build
pushd Build

cmake -G "Visual Studio 16 2019" -A arm64  -DBUILD_SHARED_LIBS=OFF
	-DZLIB_FOUND=TRUE ^
	-DZLIB_INCLUDE_DIR=%ZLIB_ROOT%\include ^
	-DZLIB_LIBRARY=%ZLIB_ROOT%\lib\Win64\arm64\Release\zlibstatic.lib ^
	-DZLIB_SUPPORT=1 ^
	-DJPEG_FOUND=TRUE ^
	-DJPEG_INCLUDE_DIR=%JPEG_INCLUDE_DIR%\ ^
	-DJPEG_LIBRARY=%JPEG_LIBRARY_ROOT%\arm64\Release\turbojpeg-static.lib ^
	-DJPEG_SUPPORT=TRUE ^
	../libtiff-v4.2.0/

"%_msbuild%msbuild.exe" tiff.sln /t:build /p:Configuration=Release

md ..\Lib\Win64\arm64\
copy /y libtiff\Release\tiff.lib ..\Lib\Win64\arm64\tiff.lib


popd
