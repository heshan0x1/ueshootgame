@echo off
pushd "ISPC Texture Compressor"

	p4 edit %THIRD_PARTY_CHANGELIST% ..\lib\...

	msbuild "ISPC Texture Compressor.sln" /target:Clean,ispc_texcomp /p:Platform=x64;Configuration="Debug"
	msbuild "ISPC Texture Compressor.sln" /target:Clean,ispc_texcomp /p:Platform=x64;Configuration="Release"
	msbuild "ISPC Texture Compressor.sln" /target:Clean,ispc_texcomp /p:Platform=arm64;Configuration="Debug"
	msbuild "ISPC Texture Compressor.sln" /target:Clean,ispc_texcomp /p:Platform=arm64;Configuration="Release"

	xcopy x64\Debug\ispc_texcomp.dll ..\..\..\..\..\..\Binaries\ThirdParty\Intel\ISPCTexComp\Win64-Debug\
	xcopy x64\Release\ispc_texcomp.dll ..\..\..\..\..\..\Binaries\ThirdParty\Intel\ISPCTexComp\Win64-Release\
	xcopy ARM64\Debug\ispc_texcomp.dll ..\..\..\..\..\..\Binaries\ThirdParty\Intel\ISPCTexComp\WinArm64-Debug\
	xcopy ARM64\Release\ispc_texcomp.dll ..\..\..\..\..\..\Binaries\ThirdParty\Intel\ISPCTexComp\WinArm64-Release\

popd

