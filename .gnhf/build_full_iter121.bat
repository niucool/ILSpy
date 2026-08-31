@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests > .gnhf\full_build_iter121.txt 2>&1
if errorlevel 1 exit /b 1
findstr /i /c:"warning" /c:"error" .gnhf\full_build_iter121.txt
echo WARN_CHECK_DONE
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe > .gnhf\full_run_iter121.txt 2>&1
if errorlevel 1 exit /b 1
echo FULLSUITE_DONE
