@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
touch cpp/Decompiler/Metadata/MetadataExtensions.cpp cpp/tests/Decompiler/Metadata/MetadataExtensions_Test.cpp
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests 2>&1 | findstr /I "warning" > .gnhf\warn141.txt
echo WARN_EXIT=%ERRORLEVEL%
type .gnhf\warn141.txt
