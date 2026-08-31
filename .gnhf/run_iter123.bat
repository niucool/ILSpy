@echo off
cd /d c:\Projects\github\ILSpy
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe --gtest_filter=ConvertTypeParameterTest.*:ConvertTypeParameterConstraintTest.*:IsObjectOrValueTypeTest.*:ConvertVariableTest.* > .gnhf\iter123_tests.txt 2>&1
echo EXIT=%ERRORLEVEL%
