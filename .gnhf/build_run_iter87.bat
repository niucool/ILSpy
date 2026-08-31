@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests
if errorlevel 1 exit /b 1
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe --gtest_filter=CSharpOperatorsTest.*:ReflectionHelperGetTypeCodeTest.*
exit /b %errorlevel%
