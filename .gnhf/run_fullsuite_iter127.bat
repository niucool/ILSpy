@echo off
cd /d c:\Projects\github\ILSpy
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe > .gnhf\fullsuite_iter127_log.txt 2>&1
echo EXIT=%ERRORLEVEL%
