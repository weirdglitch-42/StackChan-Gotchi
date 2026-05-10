@echo off
setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%"

set "ESP_IDF_PATH="
if defined IDF_PATH (
    set "ESP_IDF_PATH=%IDF_PATH%"
) else (
    if exist "C:\esp\esp-idf" set "ESP_IDF_PATH=C:\esp\esp-idf"
    if exist "D:\esp\esp-idf" set "ESP_IDF_PATH=D:\esp\esp-idf"
    if exist "C:\Users\%USERNAME%\esp\esp-idf" set "ESP_IDF_PATH=C:\Users\%USERNAME%\esp\esp-idf"
    if exist "C:\Espressif\frameworks\esp-idf" set "ESP_IDF_PATH=C:\Espressif\frameworks\esp-idf"
)

if not defined ESP_IDF_PATH (
    echo ERROR: ESP-IDF not found! Set IDF_PATH or install ESP-IDF.
    pause
    exit /b 1
)

echo Looking for available COM ports...
wmic path Win32_SerialPort get DeviceID,Name 2>nul
echo.

set /p PORT=Enter COM port (e.g., COM3) or press Enter for default COM8: 
if not defined PORT set PORT=COM8

cd /d "%ESP_IDF_PATH%"
call export.bat
cd /d "%PROJECT_DIR%"

idf.py -p %PORT% flash monitor
pause