@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
"cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe" --gtest_filter=EnumNameCollectionTest.*:WriteFlagsTest.*:WriteEnumTest.*:FormatFlagsPlaceholderTest.*:HeaderFlagCompositionTest.*:AttributeTableContentsTest.* > .gnhf\green_iter136.txt 2>&1
echo EXIT=%ERRORLEVEL%
type .gnhf\green_iter136.txt | findstr /C:"tests ran" /C:"PASSED" /C:"FAILED  ]"
