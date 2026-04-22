@echo off
setlocal EnableDelayedExpansion

set CONFIG=RelWithDebInfo

REM Run build script with "-debug" argument to get debuggable shader conductor
if "%1"=="-debug" set CONFIG=Debug

REM At present we can only build arm64 on an arm64 device due to the post-build unit tests #jira UE-234839
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
	set CMAKE_ARGS=
	set TARGET_ARCH=arm64
	set TARGET_PLATFORM=WinArm64
) else (
	REM Default to clang for improved runtime performance (inc PGO)
	if "%1"=="-msvc" (
		set TOOLSET=host=x64
	) else (
		set TOOLSET=ClangCL
	)

	set CMAKE_ARGS=-T !TOOLSET!
	set TARGET_ARCH=x64
	set TARGET_PLATFORM=Win64
)
echo Detected host architecture is %PROCESSOR_ARCHITECTURE% - building ShaderConductor for %HOST_ARCH%

if exist ShaderConductor\lib\%TARGET_PLATFORM% goto Continue
echo 
echo ************************************
echo *** Creating ShaderConductor\lib\%TARGET_PLATFORM%...
mkdir ShaderConductor\lib
mkdir ShaderConductor\lib\%TARGET_PLATFORM%

:Continue
set ENGINE_THIRD_PARTY_BIN=..\..\Binaries\ThirdParty\ShaderConductor\%TARGET_PLATFORM%
set ENGINE_THIRD_PARTY_SOURCE=..\..\Source\ThirdParty\ShaderConductor

set VS_ROOT_DIR=%ProgramFiles%\Microsoft Visual Studio\2022
set MSBUILD_VS_PROFESSIONAL=%VS_ROOT_DIR%\Professional\MSBuild\Current\Bin
set MSBUILD_VS_ENTERPRISE=%VS_ROOT_DIR%\Enterprise\MSBuild\Current\Bin

set VSPROJ_SHADERCONDUCTOR_LIB=ALL_BUILD.vcxproj
set VSPROJ_DXCOMPILER_APP=External\DirectXShaderCompiler\tools\clang\tools\dxc\dxc.vcxproj

echo 
echo ************************************
echo *** Checking out files...
pushd ..\%ENGINE_THIRD_PARTY_BIN%
	p4 edit ..\%THIRD_PARTY_CHANGELIST% ./...
popd

pushd ShaderConductor\lib\%TARGET_PLATFORM%
	p4 edit ..\%THIRD_PARTY_CHANGELIST% ./...
popd

mkdir ..\..\..\Intermediate\ShaderConductor
pushd ..\..\..\Intermediate\ShaderConductor
	echo 
	echo ************************************
	echo *** CMake
	cmake -G "Visual Studio 17 2022" %CMAKE_ARGS% -A %TARGET_ARCH% %ENGINE_THIRD_PARTY_SOURCE%\ShaderConductor

	echo 
	echo ************************************
	echo *** MSBuild
	
	where MSBuild.exe >nul 2>nul
	if %ERRORLEVEL% equ 0 (
		echo Run MSBuild from environment variable
		MSbuild.exe "%VSPROJ_SHADERCONDUCTOR_LIB%" -nologo -v:m -maxCpuCount -p:Platform=%TARGET_ARCH%;Configuration="%CONFIG%"
		MSbuild.exe "%VSPROJ_DXCOMPILER_APP%" -nologo -v:m -maxCpuCount -p:Platform=%TARGET_ARCH%;Configuration="%CONFIG%"
	) else (
		if exist "%MSBUILD_VS_PROFESSIONAL%\MSBuild.exe" (
			echo Run MSBuild from "%MSBUILD_VS_PROFESSIONAL%\MSBuild.exe"
			"%MSBUILD_VS_PROFESSIONAL%\MSBuild.exe" "%VSPROJ_SHADERCONDUCTOR_LIB%" -nologo -v:m -maxCpuCount -p:Platform=%TARGET_ARCH%;Configuration="%CONFIG%"
			"%MSBUILD_VS_PROFESSIONAL%\MSBuild.exe" "%VSPROJ_DXCOMPILER_APP%" -nologo -v:m -maxCpuCount -p:Platform=%TARGET_ARCH%;Configuration="%CONFIG%"
		) else (
			echo Run MSBuild from "%MSBUILD_VS_ENTERPRISE%\MSBuild.exe"
			"%MSBUILD_VS_ENTERPRISE%\MSBuild.exe" "%VSPROJ_SHADERCONDUCTOR_LIB%" -nologo -v:m -maxCpuCount -p:Platform=%TARGET_ARCH%;Configuration="%CONFIG%"
			"%MSBUILD_VS_ENTERPRISE%\MSBuild.exe" "%VSPROJ_DXCOMPILER_APP%" -nologo -v:m -maxCpuCount -p:Platform=%TARGET_ARCH%;Configuration="%CONFIG%"
		)
	)
	
	
	echo 
	echo ************************************
	echo *** Copying to final destination
 	xcopy External\DirectXShaderCompiler\%CONFIG%\bin\dxc.pdb			%ENGINE_THIRD_PARTY_BIN%\dxc.pdb  /F /Y
 	xcopy External\DirectXShaderCompiler\%CONFIG%\bin\dxc.exe			%ENGINE_THIRD_PARTY_BIN%\dxc.exe  /F /Y
 	xcopy External\DirectXShaderCompiler\%CONFIG%\bin\dxcompiler.pdb	%ENGINE_THIRD_PARTY_BIN%\dxcompiler.pdb  /F /Y
 	xcopy Bin\%CONFIG%\dxcompiler.dll									%ENGINE_THIRD_PARTY_BIN%\dxcompiler.dll  /F /Y
 	xcopy Bin\%CONFIG%\ShaderConductor.dll								%ENGINE_THIRD_PARTY_BIN%\ShaderConductor.dll  /F /Y
 	xcopy Bin\%CONFIG%\ShaderConductor.pdb								%ENGINE_THIRD_PARTY_BIN%\ShaderConductor.pdb  /F /Y
	xcopy Lib\%CONFIG%\ShaderConductor.lib								%ENGINE_THIRD_PARTY_SOURCE%\ShaderConductor\lib\%TARGET_PLATFORM%  /F /Y
	xcopy Bin\%CONFIG%\ShaderConductorCmd.pdb							%ENGINE_THIRD_PARTY_BIN%\ShaderConductorCmd.pdb  /F /Y
	xcopy Bin\%CONFIG%\ShaderConductorCmd.exe							%ENGINE_THIRD_PARTY_BIN%\ShaderConductorCmd.exe  /F /Y
popd

:Done
