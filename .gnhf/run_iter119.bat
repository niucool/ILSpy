@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d c:\Projects\github\ILSpy
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe --gtest_filter=IsSpecialConstantTest.*:ConvertFloatingPointTest.* > .gnhf\iter119_tests.txt 2>&1
echo EXIT=%ERRORLEVEL%
