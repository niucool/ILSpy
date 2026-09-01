@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
copy /b cpp\Decompiler\Disassembler\DisassemblerHelpers.cpp+,, cpp\Decompiler\Disassembler\DisassemblerHelpers.cpp >nul
copy /b cpp\Decompiler\Disassembler\DisassemblerHelpers.hpp+,, cpp\Decompiler\Disassembler\DisassemblerHelpers.hpp >nul
copy /b cpp\Decompiler\Metadata\ILOpCodes.cpp+,, cpp\Decompiler\Metadata\ILOpCodes.cpp >nul
copy /b cpp\tests\Decompiler\Disassembler\DisassemblerHelpers_Test.cpp+,, cpp\tests\Decompiler\Disassembler\DisassemblerHelpers_Test.cpp >nul
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests > .gnhf\warn139.txt 2>&1
if errorlevel 1 exit /b 1
echo BUILD_OK
findstr /i /C:"warning" .gnhf\warn139.txt
echo WARNCHECK_DONE
