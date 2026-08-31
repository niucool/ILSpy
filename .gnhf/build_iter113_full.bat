@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests > .gnhf\build_iter113_log.txt 2>&1
if errorlevel 1 exit /b 1
echo BUILD_OK
