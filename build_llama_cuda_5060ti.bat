@echo off
setlocal enableextensions

rem =================================================
rem  llama.cpp CUDA incremental build script
rem  NO cmake configure, NO clean-first — fast rebuild
rem =================================================

cd /d "%~dp0"

set "VSCODEV=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "%VSCODEV%" (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" (
        for /f "usebackq delims=" %%I in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VSCODEV=%%I\Common7\Tools\VsDevCmd.bat"
        )
    )
)

where cl.exe >nul 2>&1
if errorlevel 1 (
    if exist "%VSCODEV%" (
        call "%VSCODEV%" -arch=amd64 -no_logo
    )
)

where cmake.exe >nul 2>&1 || (
    echo ERROR: cmake.exe not found in PATH.
    exit /b 1
)

where cl.exe >nul 2>&1 || (
    echo ERROR: cl.exe not found in PATH.
    exit /b 1
)

echo =================================================
echo Incremental build — llama-server + llama-cli
echo =================================================

cmake --build build --config Release -j --target llama-server --target llama-cli

if errorlevel 1 (
    echo.
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo =================================================
echo Build completed successfully!
echo Output: %cd%\build\bin\Release\
echo =================================================

endlocal
