@echo off
rem Runs the original DOS game in DOSBox Staging for side-by-side comparison.
"%LOCALAPPDATA%\Programs\DOSBox Staging\dosbox.exe" -conf "%~dp0tools\original.conf"
