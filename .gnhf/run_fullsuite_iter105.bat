@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests 2> .gnhf\buildwarn_iter105.txt
if errorlevel 1 exit /b 1
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe > .gnhf\fullsuite_iter105.txt 2>&1
echo FULLSUITE_EXIT=%ERRORLEVEL%
