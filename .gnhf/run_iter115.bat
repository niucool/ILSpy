@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d c:\Projects\github\ILSpy
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe --gtest_filter=CSharpResolverCanTransformTest.* > .gnhf\run_iter115_log.txt 2>&1
echo EXIT=%ERRORLEVEL%
