@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"

if exist "%VS_DEV_CMD%" (
    call "%VS_DEV_CMD%" -arch=x64 -host_arch=x64 >nul
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%dev.ps1" %*
exit /b %ERRORLEVEL%
