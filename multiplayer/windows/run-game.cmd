@echo off
setlocal
cd /d "%~dp0"
if not "%~1"=="" goto arguments
if exist "output\godollo.glb" (
    "%~dp0godollo_client.exe" output/godollo.glb
) else (
    "%~dp0godollo_client.exe" assets/test.glb --no-trees --zombie-count 0
)
exit /b %errorlevel%

:arguments
"%~dp0godollo_client.exe" %*
exit /b %errorlevel%
