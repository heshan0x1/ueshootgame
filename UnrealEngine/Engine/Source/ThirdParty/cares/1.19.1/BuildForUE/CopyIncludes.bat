@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

SET ROOT=%~dp0..

rmdir /s /q %ROOT%\include
robocopy %ROOT%\cmake\include %ROOT%\include *.h *.inc /E /IT /XD testdata testing /XF *unittest* test_*
