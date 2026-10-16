// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The gold data for the UniversalAssemblyResolver file-loading tests
// (Resolve/ResolveModule/CreatePEFileFromFileName), dumped from the real
// installed ICSharpCode.Decompiler 11.0 (the C:/temp-probe/UarResolveProbe
// gold probe) over this machine's real .NET Framework layout plus a
// crafted fixture directory (a managed main module, a garbage .dll, and a
// native .dll) -- a machine-pinned snapshot (regenerate this fixture from
// the probe when the machine layout changes).

#pragma once

#include <array>
#include <string_view>

namespace ILSpy::Tests {

// The R2 file-loading drive rows (the probe's tag|render shape).
inline constexpr std::array<std::string_view, 35> kUarResolveGold = {
    "F|dir=C:\\temp-probe\\uar_resolve",
    "F|main=C:\\temp-probe\\uar_resolve\\main.dll|418200",
    "F|garbage=C:\\temp-probe\\uar_resolve\\garbage.dll|23",
    "F|native=C:\\temp-probe\\uar_resolve\\nativelib.dll|159744",
    "A|find=C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\mscorlib.dll",
    "A|null=False",
    "A|fileName=C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\mscorlib.dll",
    "A|name=mscorlib",
    "A|fullName=mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    "A|isAssembly=True",
    "A|distinct=True",
    "A|sameName=True",
    "B|null=True",
    "C|msg=Failed to resolve assembly: 'NoSuchAssembly.XYZ, Version=1.0.0.0, Culture=neutral, PublicKeyToken=7777777777777777'\\r\\nResolve result: <not found>",
    "C|ref=NoSuchAssembly.XYZ, Version=1.0.0.0, Culture=neutral, PublicKeyToken=7777777777777777",
    "C|resolved=<null>",
    "C|module=<null>|main=<null>",
    "D|null=False",
    "D|fileName=C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\System.dll",
    "D|name=System",
    "D|distinct=True",
    "E|msg=Failed to resolve module: 'missingmodule.dll of C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\mscorlib.dll'\\r\\nResolve result: C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\missingmodule.dll",
    "E|module=missingmodule.dll|main=C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\mscorlib.dll|resolved=C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\missingmodule.dll",
    "F2|null=True",
    "G|mainName=System.Configuration",
    "G|null0=True",
    "G1|msg=Failed to resolve module: 'garbage.dll of C:\\temp-probe\\uar_resolve\\main.dll'\\r\\nResolve result: C:\\temp-probe\\uar_resolve\\garbage.dll",
    "G1|resolved=C:\\temp-probe\\uar_resolve\\garbage.dll",
    "H0|type=ICSharpCode.Decompiler.Metadata.MetadataFileNotSupportedException",
    "H0|msg=PE file does not contain any managed metadata.",
    "H1|type=ICSharpCode.Decompiler.Metadata.MetadataFileNotSupportedException",
    "H1|msg=PE file does not contain any managed metadata.",
    "I|null1=True",
    "I|null2=True",
    "I|null3=True|fileName=<null>",
};

}  // namespace ILSpy::Tests
