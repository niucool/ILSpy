@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d c:\Projects\github\ILSpy
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_cli
if %ERRORLEVEL%==0 (
  cpp\build\windows-vs2026\ILSpyCmd\Debug\ilspy_cli.exe "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\mscorlib.dll" --csharp > .gnhf\mscorlib_new.cs 2>.gnhf\mscorlib_new_err.txt
  echo EXIT_CLI=%ERRORLEVEL%
)
