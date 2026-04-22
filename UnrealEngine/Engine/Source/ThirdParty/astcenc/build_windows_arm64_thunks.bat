
IF NOT "%Platform%"=="arm64" (
	ECHO please ensure you run   C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsamd64_arm64.bat  or equlivant first
	exit /b
)


cl astc_thunk.cpp /std:c++17 /Zi -DASTC_SUPPORTS_RDO -I5.0.1/Source 5.0.1/lib/Win64/arm64/Release/astcenc-neon-static.lib /Fe:Thunks/astcenc_thunk_winarm64_5.0.1.dll /MD /link /dll
cl astc_thunk.cpp /std:c++17 /Zi -I4.2.0/Source 4.2.0/lib/Win64/arm64/Release/astcenc-neon-static.lib /Fe:Thunks/astcenc_thunk_winarm64_4.2.0.dll /MD /link /dll
