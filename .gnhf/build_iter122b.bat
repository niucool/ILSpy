@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests > .gnhf\build_iter122b.txt 2>&1
if errorlevel 1 exit /b 1
findstr /i /c:"warning" /c:"error" .gnhf\build_iter122b.txt
echo BUILD_DONE
