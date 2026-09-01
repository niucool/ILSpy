@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_cli
if errorlevel 1 exit /b 1
echo CLI_BUILD_OK
cpp\build\windows-vs2026\ILSpyCmd\Debug\ilspy_cli.exe "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\mscorlib.dll" --csharp > .gnhf\mscorlib_iter139.cs 2>.gnhf\iter139_err.txt
if errorlevel 1 exit /b 1
echo CLI_RUN_OK
for %%I in (.gnhf\mscorlib_iter139.cs) do echo SIZE_NEW=%%~zI
for %%I in (.gnhf\mscorlib_new.cs) do echo SIZE_BASE=%%~zI
fc /b .gnhf\mscorlib_iter139.cs .gnhf\mscorlib_new.cs > .gnhf\fc_result_iter139.txt 2>&1
echo FC_EXIT=%ERRORLEVEL%
type .gnhf\fc_result_iter139.txt | findstr /C:"no differences" /C:"differences"
