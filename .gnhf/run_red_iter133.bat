@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
"cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe" --gtest_filter=CSharpAmbience*.* > .gnhf\red_iter133.txt 2>&1
type .gnhf\red_iter133.txt | findstr /C:"tests ran" /C:"PASSED" /C:"FAILED  ]"
