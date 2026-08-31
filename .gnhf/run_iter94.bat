@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe --gtest_filter=UsingScopeTest.*:CSharpTypeResolveContextTest.*:CSharp_NamespaceDeclaration.BuildQualifiedName*
if errorlevel 1 exit /b 1
echo RUN_ITER94_OK
