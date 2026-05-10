@echo off
REM StackChan-Gotchi Build Menu
REM Run this in CMD (not PowerShell or Git Bash)

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

:menu
cls
echo ========================================
echo   StackChan-Gotchi Build Menu
echo ========================================
echo.
echo   1) Clean Build (recommended)
echo   2) Quick Build (incremental)
echo   3) Flash to Device
echo   4) Flash + Monitor
echo   5) Erase Flash
echo   6) Open build folder in Explorer
echo   7) Exit
echo.
echo ========================================

set /p choice=Enter choice (1-7): 

if "%choice%"=="1" goto clean
if "%choice%"=="2" goto build
if "%choice%"=="3" goto flash
if "%choice%"=="4" goto flash_monitor
if "%choice%"=="5" goto erase
if "%choice%"=="6" goto explorer
if "%choice%"=="7" exit

echo Invalid choice.
timeout /t 2 >nul
goto menu

:clean
call clean_build.bat
goto end

:build
call build.bat
goto end

:flash
echo.
echo Please select COM port:
call flash.bat
goto end

:flash_monitor
echo.
echo Please select COM port:
call flash.bat
goto end

:erase
call erase_flash.bat
goto end

:explorer
if exist "build" (
    explorer "build"
) else (
    echo Build folder does not exist yet. Run a build first.
    timeout /t 3 >nul
)
goto menu

:end
echo.
echo Done!
pause