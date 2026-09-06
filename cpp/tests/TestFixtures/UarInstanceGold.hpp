// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE. OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The gold data for the UniversalAssemblyResolver instance-surface tests,
// dumped from the real installed ICSharpCode.Decompiler 11.0 (the
// C:/temp-probe/UarProbe gold probe) over THIS machine's framework
// directories, GAC, Windows Kits references, and .NET 10 shared
// framework install -- a machine-pinned snapshot (regenerate this
// fixture from the probe when the machine layout changes). The
// cwd-dependent I2 lines (rel-main/root-main/ws-main) are excluded:
// the test derives them from its own current directory (the same
// Path.GetDirectoryName null/whitespace -> CurrentDirectory relation
// the gold pins over the probe's cwd).

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace ILSpy::Tests {

// The I2 ctor/search-directory sequences (the fixed drives).
inline constexpr std::array<std::string_view, 5> kUarDirsGold = {
    "I2|null-main|[]",
    "I2|abs-main|[C:\\some]",
    "I2|unc-main|[\\\\server\\share]",
    "I2|mutated|[D1, <null>]",
    "I2|removed-null|[D1]",
};

// The resolver's search directories after the lazy path finder was forced.
inline constexpr std::array<std::string_view, 1> kUarAfterLazyDirsGold = {
    "I2|after-lazy|[C:\\temp-probe\\uar_instance]",
};

// The I3 IsSharedAssembly drives and the lazy-add plumbing.
inline constexpr std::array<std::string_view, 4> kUarSharedGold = {
    "I3|sysruntime|shared=True|pack=Microsoft.NETCore.App",
    "I3|mscorlib|shared=True|pack=Microsoft.NETCore.App",
    "I3|bogus|shared=False|pack=<null>",
    "I3|lazy-add|C:\\temp-probe\\uar_instance\\added\\LATER.dll",
};

// The I4 FindAssemblyFile drives over the .NET Framework target (the ResolveInternal chain).
inline constexpr std::array<std::string_view, 13> kUarFrameworkChainGold = {
    "I4|inDir|C:\\temp-probe\\uar_instance\\Bar.dll",
    "I4|inDirExe|C:\\temp-probe\\uar_instance\\Baz.exe",
    "I4|msc4|C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\mscorlib.dll",
    "I4|msc2|C:\\WINDOWS\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll",
    "I4|msc1-rev3300|C:\\WINDOWS\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll",
    "I4|msc1|C:\\WINDOWS\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll",
    "I4|mscNoVer|C:\\WINDOWS\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll",
    "I4|gac|C:\\WINDOWS\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Configuration\\v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Configuration.dll",
    "I4|sysruntime8|C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\System.Runtime.dll",
    "I4|sys4|C:\\WINDOWS\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\v4.0_4.0.0.0__b77a5c561934e089\\System.dll",
    "I4|mscCF|C:\\WINDOWS\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll",
    "I4|msc99|C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\mscorlib.dll",
    "I4|missing|<null>",
};

// The I4 throwOnError arms (the ResolutionException and the two NotSupportedException renders).
inline constexpr std::array<std::string_view, 3> kUarThrowGold = {
    "I4|throw-missing|ResolutionException|Failed to resolve assembly: 'NoSuchAssemblyAnywhere, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'\\r\\nResolve result: <not found>",
    "I4|throw-99|NotSupportedException|Version not supported: 99.0.0.0",
    "I4|throw-cf|NotSupportedException|Version not supported: 2.0.0.0",
};

// The I5 target-framework dispatch arms.
inline constexpr std::array<std::string_view, 4> kUarDispatchGold = {
    "I5|core-sysruntime|C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\System.Runtime.dll",
    "I5|core-msc2|C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\mscorlib.dll",
    "I5|gate-sysruntime|C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\System.Runtime.dll",
    "I5|sl-msc4|C:\\WINDOWS\\Microsoft.NET\\Framework\\v4.0.30319\\mscorlib.dll",
};

// The I6 winmd resolution arms.
inline constexpr std::array<std::string_view, 5> kUarWinmdGold = {
    "I6|contract|C:\\Program Files (x86)\\Windows Kits\\10\\References\\10.0.26100.0\\Windows.Foundation.UniversalApiContract\\19.0.0.0\\Windows.Foundation.UniversalApiContract.winmd",
    "I6|contract99|C:\\Program Files (x86)\\Windows Kits\\10\\References\\10.0.26100.0\\Windows.Foundation.UniversalApiContract\\19.0.0.0\\Windows.Foundation.UniversalApiContract.winmd",
    "I6|contractNoVer|C:\\Program Files (x86)\\Windows Kits\\10\\References\\10.0.26100.0\\Windows.Foundation.UniversalApiContract\\19.0.0.0\\Windows.Foundation.UniversalApiContract.winmd",
    "I6|sysdir|C:\\WINDOWS\\system32\\WinMetadata\\Windows.Foundation.winmd",
    "I6|bogusWinmd|<null>",
};

// The I7 FindClosestVersionDirectory crafted matrix.
inline constexpr std::array<std::string_view, 8> kUarClosestVersionGold = {
    "I7|2.5|3.0.0.0",
    "I7|1.5|1.5-beta",
    "I7|<null>|1.0.0.0",
    "I7|0.5|1.0.0.0",
    "I7|9.9|3.0.0.0",
    "I7|3.0|3.0.0.0",
    "I7|2.5|2.5",
    "I7|<null>|.",
};

// The I1 ctor option-enum member tables (probed from the .NET 10 runtime).
inline constexpr std::array<std::string_view, 8> kUarOptionEnumGold = {
    "I1|PEStreamOptions|Default=0",
    "I1|PEStreamOptions|LeaveOpen=1",
    "I1|PEStreamOptions|PrefetchMetadata=2",
    "I1|PEStreamOptions|PrefetchEntireImage=4",
    "I1|PEStreamOptions|IsLoadedImage=8",
    "I1|MetadataReaderOptions|None=0",
    "I1|MetadataReaderOptions|Default=1",
    "I1|MetadataReaderOptions|ApplyWindowsRuntimeProjections=1",
};

}  // namespace ILSpy::Tests
