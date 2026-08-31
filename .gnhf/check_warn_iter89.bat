@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy 2>&1 | findstr /I "warning error CSharpOperators"
echo BUILD_DONE=%ERRORLEVEL%
