@echo off
rem Writes build\output\BuildAMelee.iso from the prebuilt dist\main.dol and the
rem clean ISO named in config\local.toml. No compiler needed.
rem Also empties Slippi Dolphin's dolphin.log so the next log holds one test run.
cd /d "%~dp0.."
set "BAM_LOG=%APPDATA%\Slippi Launcher\netplay\User\Logs\dolphin.log"
if exist "%BAM_LOG%" (
    type nul > "%BAM_LOG%" 2>nul && echo Cleared dolphin.log || echo Could not clear dolphin.log - close Dolphin first.
)
py -3 tools\bam.py iso --quick
pause
