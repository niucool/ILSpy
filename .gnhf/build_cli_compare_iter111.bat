@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_cli
if errorlevel 1 exit /b 1
cpp\build\windows-vs2026\ILSpyCmd\Debug\ilspy_cli.exe "c:\Projects\github\ILSpy\.gnhf\mscorlib.dll" --csharp > .gnhf\mscorlib_iter111.cs 2>.gnhf\iter111_err.txt
if errorlevel 1 exit /b 1
echo CLI_RUN_OK
for %%I in (.gnhf\mscorlib_iter111.cs) do echo SIZE_NEW=%%~zI
for %%I in (.gnhf\mscorlib_out.cs) do echo SIZE_BASE=%%~zI
fc /b .gnhf\mscorlib_iter111.cs .gnhf\mscorlib_out.cs > .gnhf\fc_result_iter111.txt 2>&1
echo FC_EXIT=%ERRORLEVEL%
type .gnhf\fc_result_iter111.txt | findstr /C:"no differences" /C:"differences"
