@echo off
cd /d c:\Projects\github\ILSpy
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe --gtest_filter=CSharpResolverConditionPrimitiveDefaultAssignmentTest.*
echo EXIT_CODE=%ERRORLEVEL%
