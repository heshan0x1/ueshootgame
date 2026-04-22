@ECHO OFF

REM build Release x64
md Intermediate
pushd Intermediate
cmake -G "Visual Studio 17 2022" -A x64 ..\snappy-main
devenv Snappy.sln /Build Release
xcopy Release\snappy.lib ..\lib\Win64\
popd
rd /s /q Intermediate


REM build Release arm64
md Intermediate
pushd Intermediate
cmake -G "Visual Studio 17 2022" -A arm64 ..\snappy-main
devenv Snappy.sln /Build Release
xcopy Release\snappy.lib ..\lib\WinArm64\
popd
rd /s /q Intermediate

