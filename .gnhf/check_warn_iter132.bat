@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d c:\Projects\github\ILSpy
copy /b cpp\Decompiler\CSharp\Syntax\AstNode.cpp+,, cpp\Decompiler\CSharp\Syntax\AstNode.cpp >nul
copy /b cpp\Decompiler\CSharp\Syntax\AstType.cpp+,, cpp\Decompiler\CSharp\Syntax\AstType.cpp >nul
copy /b cpp\Decompiler\CSharp\Syntax\ComposedType.hpp+,, cpp\Decompiler\CSharp\Syntax\ComposedType.hpp >nul
copy /b cpp\Decompiler\CSharp\Syntax\ArraySpecifier.hpp+,, cpp\Decompiler\CSharp\Syntax\ArraySpecifier.hpp >nul
copy /b cpp\Decompiler\CSharp\Syntax\PrimitiveType.hpp+,, cpp\Decompiler\CSharp\Syntax\PrimitiveType.hpp >nul
copy /b cpp\Decompiler\CSharp\Syntax\AstNode.hpp+,, cpp\Decompiler\CSharp\Syntax\AstNode.hpp >nul
copy /b cpp\Decompiler\CSharp\Syntax\AstType.hpp+,, cpp\Decompiler\CSharp\Syntax\AstType.hpp >nul
copy /b cpp\tests\Decompiler\CSharp\Syntax\AstNodeToString_Test.cpp+,, cpp\tests\Decompiler\CSharp\Syntax\AstNodeToString_Test.cpp >nul
cmake --build cpp/build/windows-vs2026 --config Debug --target ilspy_tests > .gnhf\warncheck_iter132.txt 2>&1
if errorlevel 1 exit /b 1
echo BUILD_OK
findstr /C:"warning" .gnhf\warncheck_iter132.txt
echo WARN_GREP_EXIT=%ERRORLEVEL%
