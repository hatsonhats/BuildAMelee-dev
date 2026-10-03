@echo off
setlocal

REM Run from this folder, wherever the ISO was dragged from.
cd /d "%~dp0"

if "%~1"=="" (
    echo Drag a clean NTSC v1.02 Melee ISO onto "DRAG VANILLA MELEE HERE.bat".
    goto end
)

echo ISO: "%~1"
echo Patching, this takes a few seconds...
xdelta3.exe -f -d -s "%~1" patch.xdelta BuildAMelee.iso || (
    echo.
    echo ERROR: That ISO is not a clean NTSC v1.02 Melee ISO.
    if exist BuildAMelee.iso del BuildAMelee.iso
    goto end
)
echo.
echo Done! BuildAMelee.iso is in this folder.
echo Set it as your Melee ISO in the Slippi Launcher settings.

:end
echo.
pause
