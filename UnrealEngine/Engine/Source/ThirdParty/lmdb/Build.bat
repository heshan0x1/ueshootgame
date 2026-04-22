set ANDROID_CMAKE=%ANDROID_HOME%\cmake\3.22.1\bin
set VS_CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake"

call :build_android_arch arm64-v8a
call :build_android_arch x86_64

call :build_win64

exit /b

:build_android_arch

mkdir .build_android_%1
pushd .build_android_%1

%ANDROID_CMAKE%\cmake.exe -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_SYSTEM_NAME=Android -DCMAKE_SYSTEM_VERSION=28 -DCMAKE_ANDROID_ARCH_ABI=%1 -DCMAKE_ANDROID_STL_TYPE=c++_static -DCMAKE_MAKE_PROGRAM=%ANDROID_CMAKE%\ninja.exe ..
%ANDROID_CMAKE%\ninja.exe

popd

mkdir android\%1
move /y .build_android_%1\liblmdb.a android\%1
rmdir .build_android_%1 /s /q

exit /b

:build_win64

mkdir .build_win64
pushd .build_win64

%VS_CMAKE%\CMake\bin\cmake.exe -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_MAKE_PROGRAM=%VS_CMAKE%\Ninja\ninja.exe ..
%VS_CMAKE%\Ninja\ninja.exe

popd

mkdir win64
move /y .build_win64\lmdb.lib win64
rmdir .build_win64 /s /q

exit /b
