@echo off
REM Clean and build script for StackChan firmware
REM Run this in CMD (not PowerShell or Git Bash)

setlocal EnableDelayedExpansion

REM Get script directory
set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%"

REM Find ESP-IDF - check IDF_PATH env var first, then common locations
set "ESP_IDF_PATH="
if defined IDF_PATH (
    set "ESP_IDF_PATH=%IDF_PATH%"
) else (
    REM Check common installation paths
    if exist "C:\esp\esp-idf" set "ESP_IDF_PATH=C:\esp\esp-idf"
    if exist "D:\esp\esp-idf" set "ESP_IDF_PATH=D:\esp\esp-idf"
    if exist "C:\Users\%USERNAME%\esp\esp-idf" set "ESP_IDF_PATH=C:\Users\%USERNAME%\esp\esp-idf"
    if exist "C:\Espressif\frameworks\esp-idf" set "ESP_IDF_PATH=C:\Espressif\frameworks\esp-idf"
)

if not defined ESP_IDF_PATH (
    echo ERROR: ESP-IDF not found!
    echo Please set IDF_PATH environment variable or install ESP-IDF.
    echo.
    echo Common installation paths:
    echo   C:\esp\esp-idf
    echo   D:\esp\esp-idf
    echo   C:\Espressif\frameworks\esp-idf
    echo.
    pause
    exit /b 1
)

echo ========================================
echo Cleaning and Building StackChan firmware
echo ESP-IDF: %ESP_IDF_PATH%
echo Project: %PROJECT_DIR%
echo ========================================
echo.

cd /d "%ESP_IDF_PATH%"
call export.bat

cd /d "%PROJECT_DIR%"

echo.
echo Cleaning build directory...
echo.

if exist "build" (
    rmdir /s /q "build"
    echo Build directory cleaned.
) else (
    echo No build directory found.
)

echo.
echo Starting fresh build...
echo.

idf.py build

echo.
echo Build complete!
pause