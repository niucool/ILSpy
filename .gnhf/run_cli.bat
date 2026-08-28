@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d c:\Projects\github\ILSpy
copy /Y "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\mscorlib.dll" c:\Projects\github\ILSpy\.gnhf\mscorlib.dll >nul
cpp\build\windows-vs2026\ILSpyCmd\Debug\ilspy_cli.exe c:\Projects\github\ILSpy\.gnhf\mscorlib.dll --csharp > c:\Projects\github\ILSpy\.gnhf\mscorlib_out.cs 2>&1
echo CLI exit: %ERRORLEVEL%
for %%A in (c:\Projects\github\ILSpy\.gnhf\mscorlib_out.cs) do echo Size: %%~zA bytes
