@echo off
REM StackChan Firmware Flash Erase Script
REM Usage: Run from ESP-IDF environment or adjust paths below

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%"

echo ========================================
echo StackChan Firmware Flash Eraser
echo ========================================
echo.

echo Looking for available COM ports...
wmic path Win32_SerialPort get DeviceID,Name 2>nul
echo.

set /p PORT=Enter COM port (e.g., COM3): 

if not defined PORT (
    echo ERROR: No port entered
    pause
    exit /b 1
)

echo.
echo Choose erase option:
echo   1) Erase NVS only (reset stats only)
echo   2) Erase entire flash (full reset)
echo   3) Exit
echo.

set /p choice=Enter choice (1-3): 

if "%choice%"=="1" goto erase_nvs
if "%choice%"=="2" goto erase_full
if "%choice%"=="3" exit

echo Invalid choice.
pause
exit /b 1

:erase_nvs
echo.
echo Erasing NVS partition (stats only)...
echo This will reset: XP, Level, Networks found
echo.
esptool.py --chip esp32s3 --port %PORT% erase_region 0x9000 0x6000
if errorlevel 1 (
    echo FAILED!
    pause
    exit /b 1
)
echo.
echo SUCCESS! NVS erased.
echo Stats will reset on next boot.
pause
exit /b 0

:erase_full
echo.
echo WARNING: This will erase EVERYTHING!
echo - All stats
echo - WiFi configs
echo - BLE pairings
echo.
set /p confirm=Type YES to confirm: 
if not "%confirm%"=="YES" (
    echo Cancelled.
    pause
    exit /b 0
)
echo.
echo Erasing entire flash...
esptool.py --chip esp32s3 --port %PORT% erase_flash
if errorlevel 1 (
    echo FAILED!
    pause
    exit /b 1
)
echo.
echo SUCCESS! Full flash erased.
echo You will need to flash the firmware again.
pause
exit /b 0