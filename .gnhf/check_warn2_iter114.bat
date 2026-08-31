@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
copy /Y cpp\Decompiler\CSharp\Resolver\CSharpResolver.cpp cpp\Decompiler\CSharp\Resolver\CSharpResolver.cpp.bak > nul
touch cpp\Decompiler\CSharp\Resolver\CSharpResolver.cpp
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests > .gnhf\warncheck_iter114_log.txt 2>&1
if errorlevel 1 exit /b 1
del cpp\Decompiler\CSharp\Resolver\CSharpResolver.cpp.bak
findstr /I /C:"warning" .gnhf\warncheck_iter114_log.txt
echo WARN_GREP_EXIT=%ERRORLEVEL%
echo REBUILD_DONE
