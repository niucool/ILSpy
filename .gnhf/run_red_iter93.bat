@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests
if errorlevel 1 exit /b 1
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe --gtest_filter=CSharpOperatorsUserDefinedTest.*:CSharp_OperatorDeclaration.GetOperatorType*:CSharp_OperatorDeclaration.IsComparisonOperator* > .gnhf\red_iter93.txt 2>&1
echo RUN_DONE
