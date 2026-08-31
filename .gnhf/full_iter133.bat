@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests
if errorlevel 1 exit /b 1
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_cli
if errorlevel 1 exit /b 1
cpp\build\windows-vs2026\tests\Debug\ilspy_tests.exe > .gnhf\fullsuite_iter133_log.txt 2>&1
echo TESTS_EXIT=%ERRORLEVEL%
findstr /C:"tests ran" /C:"PASSED" .gnhf\fullsuite_iter133_log.txt
cpp\build\windows-vs2026\ILSpyCmd\Debug\ilspy_cli.exe "c:\Projects\github\ILSpy\.gnhf\mscorlib.dll" --csharp > .gnhf\mscorlib_iter133.cs 2>.gnhf\iter133_err.txt
if errorlevel 1 exit /b 1
echo CLI_RUN_OK
for %%I in (.gnhf\mscorlib_iter133.cs) do echo SIZE_NEW=%%~zI
for %%I in (.gnhf\mscorlib_out.cs) do echo SIZE_BASE=%%~zI
fc /b .gnhf\mscorlib_iter133.cs .gnhf\mscorlib_out.cs > .gnhf\fc_result_iter133.txt 2>&1
echo FC_EXIT=%ERRORLEVEL%
type .gnhf\fc_result_iter133.txt | findstr /C:"no differences" /C:"differences"
