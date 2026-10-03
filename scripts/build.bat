@echo off
rem Full build: compiles the overlay with the decomp toolchain, patches the DOL
rem and writes build\output\BuildAMelee.iso. Needs the decomp checkout from
rem config\local.toml (bam.py fetch, or an existing rogueMelee-v2 cache).
cd /d "%~dp0.."
py -3 tools\bam.py build --auto-iso
pause
