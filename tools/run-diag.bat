@echo off
rem Build AltTaber and launch it with diagnostic logging enabled.
rem Build log:   %TEMP%\alttaber_build.log
rem Runtime log: <repo>\build\Release\diag.log
rem Markers written to the runtime log: #FG #SWITCH #TB_ENTER #TB_LEAVE #THUMB
setlocal

set "REPO=%~dp0.."
set "BUILD_LOG=%TEMP%\alttaber_build.log"
set "DIAG_LOG=%REPO%\build\Release\diag.log"
set "EXE=%REPO%\build\Release\AltTaber.exe"

rem The exe is locked while AltTaber runs, so stop the previous instance first.
taskkill /IM AltTaber.exe /F >nul 2>&1

call "%REPO%\..\rebuild.bat" > "%BUILD_LOG%" 2>&1
if errorlevel 1 (
    echo Build failed. See "%BUILD_LOG%"
    exit /b 1
)

set "QT_FORCE_STDERR_LOGGING=1"
set "QT_WIN_DEBUG_CONSOLE=attach"
set "ALTTABER_DIAG=1"

echo ==== AltTaber diag session %DATE% %TIME% ==== > "%DIAG_LOG%"
start "" /b "%EXE%" >> "%DIAG_LOG%" 2>&1

exit /b 0
