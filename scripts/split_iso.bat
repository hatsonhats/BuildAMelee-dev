@echo off
rem Splits the clean ISO from config\local.toml into build\iso_parts\ (4 parts,
rem 360 MB each) so it can be copied to the build container for testing.
cd /d "%~dp0.."
py -3 tools\split_iso.py
pause
