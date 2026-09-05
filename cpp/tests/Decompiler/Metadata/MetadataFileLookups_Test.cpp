// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the MetadataFile name reverse lookups (cpp/Decompiler/Metadata/
// MetadataFile.{hpp,cpp} -- the C# MetadataFile.cs GetTypeDefinition/
// GetTypeForwarder lazy dictionaries, the leaf the future MetadataModule
// type-system back end's own GetTypeDefinition(TopLevelTypeName) resolves
// through) and the ExportedType full-name reader they compose
// (SRMExtensions::GetFullTypeNameFromExportedType -- the C# SRMExtensions.cs
// `GetFullTypeName(this ExportedType, MetadataReader)` nested-forwarder-chain
// walk). Every expectation is pinned against the REAL installed
// ICSharpCode.Decompiler 11.0 PEFile driven over the identical fixtures
// (C:/temp-probe/NameLookupProbe -- the established gold-probe technique):
//   - mscorlib 4.8: String is TypeDef 0x02000073, 2696 top-level TypeDefs
//     all round-trip their (ns, arity-split name, count) key back to their
//     own token, no duplicate keys, and ZERO ExportedType rows;
//   - System.dll 4.8: System.Uri is a real TypeDef (0x0200003d), 1711
//     top-level rows, exactly ONE forwarder (System.Threading.
//     SemaphoreFullException -> 0x27000001);
//   - .NET 10 CoreLib: 1882 top-level rows round-trip;
//   - the GAC System.Runtime facade: the one local fixture with a real
//     forwarder table -- 279 rows including six NESTED forwarders whose
//     Implementation column targets the outer ExportedType row (the
//     DebuggableAttribute+DebuggingModes chain), all 279 names and tokens
//     gold-pinned row for row;
//   - tiny.netmodule (the TestFixtures/TinyNetModule.hpp bytes): both
//     lookups over a netmodule; and the invalid-file / throw arms.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using ILSpy::Decompiler::Metadata::GetFullTypeNameFromExportedType;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;

namespace {

// A top-level name from its parts (the ctor is explicit, so the braced form
// reads the three fields in declaration order: namespace, name, arity).
TopLevelTypeName TL(const char* ns, const char* name, int tpc = 0) {
    return TopLevelTypeName(ns, name, tpc);
}

FullTypeName FT(const TopLevelTypeName& topLevel) {
    return FullTypeName(topLevel);
}

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

const char* SystemDllPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\"
           "v4.0_4.0.0.0__b77a5c561934e089\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

// The GAC .NET Framework System.Runtime facade -- the one local fixture with
// a real forwarder table (279 ExportedType rows, six of them nested).
const char* FacadePath() {
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
           "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
}

#if defined(_WIN32)
// The .NET 10 shared-runtime CoreLib (the ReflectionDisassembler_Test glob
// pattern: the newest installed Microsoft.NETCore.App).
std::string CoreLibPath() {
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App";
    std::error_code ec;
    std::string best;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string candidate = it->path().string() + "\\System.Private.CoreLib.dll";
        if (fs::exists(candidate, ec)) best = candidate;
    }
    return best;
}
#endif

// The GAC facade's 279 exported-type rows, gold-dumped from the REAL
// ICSharpCode.Decompiler 11.0 over the identical file (the probe's
// `td.GetFullTypeName(metadata)` render): the reflection name (the nested
// forwarders key on their declaring-chain names) and the raw token.
struct FacadeRow {
    const char* Name;
    std::uint32_t Token;
};
const FacadeRow kFacadeGoldRows[] = {
    { "System.Action", 0x27000001 },
    { "System.Action`1", 0x27000002 },
    { "System.Action`10", 0x27000003 },
    { "System.Action`11", 0x27000004 },
    { "System.Action`12", 0x27000005 },
    { "System.Action`13", 0x27000006 },
    { "System.Action`14", 0x27000007 },
    { "System.Action`15", 0x27000008 },
    { "System.Action`16", 0x27000009 },
    { "System.Action`2", 0x2700000a },
    { "System.Action`3", 0x2700000b },
    { "System.Action`4", 0x2700000c },
    { "System.Action`5", 0x2700000d },
    { "System.Action`6", 0x2700000e },
    { "System.Action`7", 0x2700000f },
    { "System.Action`8", 0x27000010 },
    { "System.Action`9", 0x27000011 },
    { "System.Activator", 0x27000012 },
    { "System.ArgumentException", 0x27000013 },
    { "System.ArgumentNullException", 0x27000014 },
    { "System.ArgumentOutOfRangeException", 0x27000015 },
    { "System.ArithmeticException", 0x27000016 },
    { "System.Array", 0x27000017 },
    { "System.ArraySegment`1", 0x27000018 },
    { "System.ArrayTypeMismatchException", 0x27000019 },
    { "System.AsyncCallback", 0x2700001a },
    { "System.Attribute", 0x2700001b },
    { "System.AttributeTargets", 0x2700001c },
    { "System.AttributeUsageAttribute", 0x2700001d },
    { "System.BadImageFormatException", 0x2700001e },
    { "System.Boolean", 0x2700001f },
    { "System.Buffer", 0x27000020 },
    { "System.Byte", 0x27000021 },
    { "System.Char", 0x27000022 },
    { "System.CLSCompliantAttribute", 0x27000023 },
    { "System.Collections.DictionaryEntry", 0x27000024 },
    { "System.Collections.Generic.ICollection`1", 0x27000025 },
    { "System.Collections.Generic.IComparer`1", 0x27000026 },
    { "System.Collections.Generic.IDictionary`2", 0x27000027 },
    { "System.Collections.Generic.IEnumerable`1", 0x27000028 },
    { "System.Collections.Generic.IEnumerator`1", 0x27000029 },
    { "System.Collections.Generic.IEqualityComparer`1", 0x2700002a },
    { "System.Collections.Generic.IList`1", 0x2700002b },
    { "System.Collections.Generic.IReadOnlyCollection`1", 0x2700002c },
    { "System.Collections.Generic.IReadOnlyDictionary`2", 0x2700002d },
    { "System.Collections.Generic.IReadOnlyList`1", 0x2700002e },
    { "System.Collections.Generic.ISet`1", 0x2700002f },
    { "System.Collections.Generic.KeyNotFoundException", 0x27000030 },
    { "System.Collections.Generic.KeyValuePair`2", 0x27000031 },
    { "System.Collections.ICollection", 0x27000032 },
    { "System.Collections.IComparer", 0x27000033 },
    { "System.Collections.IDictionary", 0x27000034 },
    { "System.Collections.IDictionaryEnumerator", 0x27000035 },
    { "System.Collections.IEnumerable", 0x27000036 },
    { "System.Collections.IEnumerator", 0x27000037 },
    { "System.Collections.IEqualityComparer", 0x27000038 },
    { "System.Collections.IList", 0x27000039 },
    { "System.Collections.IStructuralComparable", 0x2700003a },
    { "System.Collections.IStructuralEquatable", 0x2700003b },
    { "System.Collections.ObjectModel.Collection`1", 0x2700003c },
    { "System.Collections.ObjectModel.ReadOnlyCollection`1", 0x2700003d },
    { "System.Comparison`1", 0x2700003e },
    { "System.ComponentModel.DefaultValueAttribute", 0x2700003f },
    { "System.ComponentModel.EditorBrowsableAttribute", 0x27000040 },
    { "System.ComponentModel.EditorBrowsableState", 0x27000041 },
    { "System.DateTime", 0x27000042 },
    { "System.DateTimeKind", 0x27000043 },
    { "System.DateTimeOffset", 0x27000044 },
    { "System.DayOfWeek", 0x27000045 },
    { "System.Decimal", 0x27000046 },
    { "System.Delegate", 0x27000047 },
    { "System.Diagnostics.ConditionalAttribute", 0x27000048 },
    { "System.Diagnostics.DebuggableAttribute", 0x27000049 },
    { "System.Diagnostics.DebuggableAttribute+DebuggingModes", 0x2700004a },
    { "System.DivideByZeroException", 0x2700004b },
    { "System.Double", 0x2700004c },
    { "System.Enum", 0x2700004d },
    { "System.EventArgs", 0x2700004e },
    { "System.EventHandler", 0x2700004f },
    { "System.EventHandler`1", 0x27000050 },
    { "System.Exception", 0x27000051 },
    { "System.FieldAccessException", 0x27000052 },
    { "System.FlagsAttribute", 0x27000053 },
    { "System.FormatException", 0x27000054 },
    { "System.FormattableString", 0x27000055 },
    { "System.Func`1", 0x27000056 },
    { "System.Func`10", 0x27000057 },
    { "System.Func`11", 0x27000058 },
    { "System.Func`12", 0x27000059 },
    { "System.Func`13", 0x2700005a },
    { "System.Func`14", 0x2700005b },
    { "System.Func`15", 0x2700005c },
    { "System.Func`16", 0x2700005d },
    { "System.Func`17", 0x2700005e },
    { "System.Func`2", 0x2700005f },
    { "System.Func`3", 0x27000060 },
    { "System.Func`4", 0x27000061 },
    { "System.Func`5", 0x27000062 },
    { "System.Func`6", 0x27000063 },
    { "System.Func`7", 0x27000064 },
    { "System.Func`8", 0x27000065 },
    { "System.Func`9", 0x27000066 },
    { "System.GC", 0x27000067 },
    { "System.GCCollectionMode", 0x27000068 },
    { "System.Globalization.DateTimeStyles", 0x27000069 },
    { "System.Globalization.NumberStyles", 0x2700006a },
    { "System.Globalization.TimeSpanStyles", 0x2700006b },
    { "System.Guid", 0x2700006c },
    { "System.IAsyncResult", 0x2700006d },
    { "System.IComparable", 0x2700006e },
    { "System.IComparable`1", 0x2700006f },
    { "System.IConvertible", 0x27000070 },
    { "System.ICustomFormatter", 0x27000071 },
    { "System.IDisposable", 0x27000072 },
    { "System.IEquatable`1", 0x27000073 },
    { "System.IFormatProvider", 0x27000074 },
    { "System.IFormattable", 0x27000075 },
    { "System.IndexOutOfRangeException", 0x27000076 },
    { "System.InsufficientExecutionStackException", 0x27000077 },
    { "System.Int16", 0x27000078 },
    { "System.Int32", 0x27000079 },
    { "System.Int64", 0x2700007a },
    { "System.IntPtr", 0x2700007b },
    { "System.InvalidCastException", 0x2700007c },
    { "System.InvalidOperationException", 0x2700007d },
    { "System.InvalidProgramException", 0x2700007e },
    { "System.InvalidTimeZoneException", 0x2700007f },
    { "System.IO.DirectoryNotFoundException", 0x27000080 },
    { "System.IO.FileLoadException", 0x27000081 },
    { "System.IO.FileNotFoundException", 0x27000082 },
    { "System.IO.IOException", 0x27000083 },
    { "System.IO.PathTooLongException", 0x27000084 },
    { "System.IObservable`1", 0x27000085 },
    { "System.IObserver`1", 0x27000086 },
    { "System.IProgress`1", 0x27000087 },
    { "System.Lazy`1", 0x27000088 },
    { "System.Lazy`2", 0x27000089 },
    { "System.MemberAccessException", 0x2700008a },
    { "System.MethodAccessException", 0x2700008b },
    { "System.MissingFieldException", 0x2700008c },
    { "System.MissingMemberException", 0x2700008d },
    { "System.MissingMethodException", 0x2700008e },
    { "System.MTAThreadAttribute", 0x2700008f },
    { "System.MulticastDelegate", 0x27000090 },
    { "System.NotImplementedException", 0x27000091 },
    { "System.NotSupportedException", 0x27000092 },
    { "System.Nullable", 0x27000093 },
    { "System.Nullable`1", 0x27000094 },
    { "System.NullReferenceException", 0x27000095 },
    { "System.Object", 0x27000096 },
    { "System.ObjectDisposedException", 0x27000097 },
    { "System.ObsoleteAttribute", 0x27000098 },
    { "System.OutOfMemoryException", 0x27000099 },
    { "System.OverflowException", 0x2700009a },
    { "System.ParamArrayAttribute", 0x2700009b },
    { "System.PlatformNotSupportedException", 0x2700009c },
    { "System.Predicate`1", 0x2700009d },
    { "System.RankException", 0x2700009e },
    { "System.Reflection.AssemblyCompanyAttribute", 0x2700009f },
    { "System.Reflection.AssemblyConfigurationAttribute", 0x270000a0 },
    { "System.Reflection.AssemblyCopyrightAttribute", 0x270000a1 },
    { "System.Reflection.AssemblyCultureAttribute", 0x270000a2 },
    { "System.Reflection.AssemblyDefaultAliasAttribute", 0x270000a3 },
    { "System.Reflection.AssemblyDelaySignAttribute", 0x270000a4 },
    { "System.Reflection.AssemblyDescriptionAttribute", 0x270000a5 },
    { "System.Reflection.AssemblyFileVersionAttribute", 0x270000a6 },
    { "System.Reflection.AssemblyFlagsAttribute", 0x270000a7 },
    { "System.Reflection.AssemblyInformationalVersionAttribute", 0x270000a8 },
    { "System.Reflection.AssemblyKeyFileAttribute", 0x270000a9 },
    { "System.Reflection.AssemblyKeyNameAttribute", 0x270000aa },
    { "System.Reflection.AssemblyMetadataAttribute", 0x270000ab },
    { "System.Reflection.AssemblyNameFlags", 0x270000ac },
    { "System.Reflection.AssemblyProductAttribute", 0x270000ad },
    { "System.Reflection.AssemblySignatureKeyAttribute", 0x270000ae },
    { "System.Reflection.AssemblyTitleAttribute", 0x270000af },
    { "System.Reflection.AssemblyTrademarkAttribute", 0x270000b0 },
    { "System.Reflection.AssemblyVersionAttribute", 0x270000b1 },
    { "System.Reflection.DefaultMemberAttribute", 0x270000b2 },
    { "System.Reflection.ProcessorArchitecture", 0x270000b3 },
    { "System.Runtime.CompilerServices.AccessedThroughPropertyAttribute", 0x270000b4 },
    { "System.Runtime.CompilerServices.AsyncStateMachineAttribute", 0x270000b5 },
    { "System.Runtime.CompilerServices.CallerFilePathAttribute", 0x270000b6 },
    { "System.Runtime.CompilerServices.CallerLineNumberAttribute", 0x270000b7 },
    { "System.Runtime.CompilerServices.CallerMemberNameAttribute", 0x270000b8 },
    { "System.Runtime.CompilerServices.CompilationRelaxationsAttribute", 0x270000b9 },
    { "System.Runtime.CompilerServices.CompilerGeneratedAttribute", 0x270000ba },
    { "System.Runtime.CompilerServices.ConditionalWeakTable`2", 0x270000bb },
    { "System.Runtime.CompilerServices.ConditionalWeakTable`2+CreateValueCallback", 0x270000bc },
    { "System.Runtime.CompilerServices.CustomConstantAttribute", 0x270000bd },
    { "System.Runtime.CompilerServices.DateTimeConstantAttribute", 0x270000be },
    { "System.Runtime.CompilerServices.DecimalConstantAttribute", 0x270000bf },
    { "System.Runtime.CompilerServices.DisablePrivateReflectionAttribute", 0x270000c0 },
    { "System.Runtime.CompilerServices.ExtensionAttribute", 0x270000c1 },
    { "System.Runtime.CompilerServices.FixedBufferAttribute", 0x270000c2 },
    { "System.Runtime.CompilerServices.FormattableStringFactory", 0x270000c3 },
    { "System.Runtime.CompilerServices.IndexerNameAttribute", 0x270000c4 },
    { "System.Runtime.CompilerServices.InternalsVisibleToAttribute", 0x270000c5 },
    { "System.Runtime.CompilerServices.IsConst", 0x270000c6 },
    { "System.Runtime.CompilerServices.IStrongBox", 0x270000c7 },
    { "System.Runtime.CompilerServices.IsVolatile", 0x270000c8 },
    { "System.Runtime.CompilerServices.IteratorStateMachineAttribute", 0x270000c9 },
    { "System.Runtime.CompilerServices.MethodImplAttribute", 0x270000ca },
    { "System.Runtime.CompilerServices.MethodImplOptions", 0x270000cb },
    { "System.Runtime.CompilerServices.ReferenceAssemblyAttribute", 0x270000cc },
    { "System.Runtime.CompilerServices.RuntimeCompatibilityAttribute", 0x270000cd },
    { "System.Runtime.CompilerServices.RuntimeHelpers", 0x270000ce },
    { "System.Runtime.CompilerServices.RuntimeHelpers+CleanupCode", 0x270000cf },
    { "System.Runtime.CompilerServices.RuntimeHelpers+TryCode", 0x270000d0 },
    { "System.Runtime.CompilerServices.StateMachineAttribute", 0x270000d1 },
    { "System.Runtime.CompilerServices.StrongBox`1", 0x270000d2 },
    { "System.Runtime.CompilerServices.TypeForwardedFromAttribute", 0x270000d3 },
    { "System.Runtime.CompilerServices.TypeForwardedToAttribute", 0x270000d4 },
    { "System.Runtime.CompilerServices.UnsafeValueTypeAttribute", 0x270000d5 },
    { "System.Runtime.ExceptionServices.ExceptionDispatchInfo", 0x270000d6 },
    { "System.Runtime.GCLargeObjectHeapCompactionMode", 0x270000d7 },
    { "System.Runtime.GCLatencyMode", 0x270000d8 },
    { "System.Runtime.GCSettings", 0x270000d9 },
    { "System.Runtime.InteropServices.CharSet", 0x270000da },
    { "System.Runtime.InteropServices.ComVisibleAttribute", 0x270000db },
    { "System.Runtime.InteropServices.FieldOffsetAttribute", 0x270000dc },
    { "System.Runtime.InteropServices.LayoutKind", 0x270000dd },
    { "System.Runtime.InteropServices.OutAttribute", 0x270000de },
    { "System.Runtime.InteropServices.StructLayoutAttribute", 0x270000df },
    { "System.Runtime.Versioning.TargetFrameworkAttribute", 0x270000e0 },
    { "System.RuntimeFieldHandle", 0x270000e1 },
    { "System.RuntimeMethodHandle", 0x270000e2 },
    { "System.RuntimeTypeHandle", 0x270000e3 },
    { "System.SByte", 0x270000e4 },
    { "System.Security.AllowPartiallyTrustedCallersAttribute", 0x270000e5 },
    { "System.Security.SecurityCriticalAttribute", 0x270000e6 },
    { "System.Security.SecurityException", 0x270000e7 },
    { "System.Security.SecuritySafeCriticalAttribute", 0x270000e8 },
    { "System.Security.SecurityTransparentAttribute", 0x270000e9 },
    { "System.Security.VerificationException", 0x270000ea },
    { "System.Single", 0x270000eb },
    { "System.STAThreadAttribute", 0x270000ec },
    { "System.String", 0x270000ed },
    { "System.StringComparison", 0x270000ee },
    { "System.StringSplitOptions", 0x270000ef },
    { "System.Text.StringBuilder", 0x270000f0 },
    { "System.Threading.LazyThreadSafetyMode", 0x270000f1 },
    { "System.Threading.Timeout", 0x270000f2 },
    { "System.Threading.WaitHandle", 0x270000f3 },
    { "System.ThreadStaticAttribute", 0x270000f4 },
    { "System.TimeoutException", 0x270000f5 },
    { "System.TimeSpan", 0x270000f6 },
    { "System.TimeZoneInfo", 0x270000f7 },
    { "System.TimeZoneInfo+AdjustmentRule", 0x270000f8 },
    { "System.TimeZoneInfo+TransitionTime", 0x270000f9 },
    { "System.Tuple", 0x270000fa },
    { "System.Tuple`1", 0x270000fb },
    { "System.Tuple`2", 0x270000fc },
    { "System.Tuple`3", 0x270000fd },
    { "System.Tuple`4", 0x270000fe },
    { "System.Tuple`5", 0x270000ff },
    { "System.Tuple`6", 0x27000100 },
    { "System.Tuple`7", 0x27000101 },
    { "System.Tuple`8", 0x27000102 },
    { "System.Type", 0x27000103 },
    { "System.TypeAccessException", 0x27000104 },
    { "System.TypeCode", 0x27000105 },
    { "System.TypeInitializationException", 0x27000106 },
    { "System.TypeLoadException", 0x27000107 },
    { "System.UInt16", 0x27000108 },
    { "System.UInt32", 0x27000109 },
    { "System.UInt64", 0x2700010a },
    { "System.UIntPtr", 0x2700010b },
    { "System.UnauthorizedAccessException", 0x2700010c },
    { "System.Uri", 0x2700010d },
    { "System.UriComponents", 0x2700010e },
    { "System.UriFormat", 0x2700010f },
    { "System.UriFormatException", 0x27000110 },
    { "System.UriHostNameType", 0x27000111 },
    { "System.UriKind", 0x27000112 },
    { "System.ValueType", 0x27000113 },
    { "System.Version", 0x27000114 },
    { "System.Void", 0x27000115 },
    { "System.WeakReference", 0x27000116 },
    { "System.WeakReference`1", 0x27000117 },
};

// The top-level TypeDef sweep over one file: every non-nested row's
// (ns, arity-split name, count) key must resolve back to its own token
// through the lookup (the gold probe's section B: roundtrip-mismatches=0),
// returning the top-level count for the caller's total assertion.
std::size_t RoundTripTopLevelTypeDefs(const MetadataFile& file) {
    std::size_t count = 0;
    // The gtest ASSERT_ macros return void and cannot live in this
    // size_t-returning helper, so the sweep uses EXPECT_ (a failed
    // expectation still fails the calling test).
    for (const auto& t : file.TypeDefs()) {
        auto info = file.GetTypeDefNameInfo(t.Token);
        EXPECT_TRUE(info.has_value());
        if (!info.has_value()) continue;
        if (info->DeclaringTypeToken != 0) continue;  // the C# nested skip
        int tpc = 0;
        std::string name = ILSpy::Decompiler::TypeSystem::
            SplitTypeParameterCountFromReflectionName(info->Name, tpc);
        EXPECT_EQ(file.GetTypeDefinition(
                      TL(info->Namespace.c_str(), name.c_str(), tpc)),
                  t.Token)
            << "round-trip miss for " << info->Namespace << "." << info->Name;
        count++;
    }
    return count;
}

}  // namespace

// The gold spot pins over mscorlib 4.8: the six hit shapes (including the
// <Module> pseudo-type the C# lookup keeps, the generic arity split, and a
// Microsoft.Win32 type) and the five miss shapes (the wrong-arity query,
// the nested type spelled as a top-level name, and two absent names --
// System.Uri is NOT in mscorlib).
TEST(MetadataFileLookupsTest, GetTypeDefinitionFindsMscorlibTypes) {
    MetadataFile file(MscorlibPath());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(file.GetTypeDefinition(TL("", "<Module>")), 0x02000001u);
    EXPECT_EQ(file.GetTypeDefinition(TL("System", "String")), 0x02000073u);
    EXPECT_EQ(file.GetTypeDefinition(TL("System", "Int32")), 0x020000fbu);
    EXPECT_EQ(file.GetTypeDefinition(TL("System", "Math")), 0x0200010bu);
    EXPECT_EQ(file.GetTypeDefinition(
                  TL("System.Collections.Generic", "List", 1)),
              0x020004dcu);
    EXPECT_EQ(file.GetTypeDefinition(TL("Microsoft.Win32", "Win32Native")),
              0x0200000eu);
    // The misses (the nil handle is the port's 0).
    EXPECT_EQ(file.GetTypeDefinition(
                  TL("System.Collections.Generic", "List", 0)), 0u);
    EXPECT_EQ(file.GetTypeDefinition(TL("System", "String", 1)), 0u);
    EXPECT_EQ(file.GetTypeDefinition(
                  TL("Microsoft.Win32", "WIN32_FIND_DATA")), 0u);
    EXPECT_EQ(file.GetTypeDefinition(TL("System", "NoSuchTypeAtAll")), 0u);
    EXPECT_EQ(file.GetTypeDefinition(TL("System", "Uri")), 0u);
}

// The full-table sweep: every one of mscorlib's 2696 top-level TypeDefs
// round-trips its lookup key back to its own token (the gold: 2696 rows,
// 0 mismatches, 0 duplicate keys -- the last-wins arm is unreachable on
// this fixture, so every key is distinct and the sweep is exact).
TEST(MetadataFileLookupsTest, GetTypeDefinitionRoundTripsMscorlibTypeDefs) {
    MetadataFile file(MscorlibPath());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(RoundTripTopLevelTypeDefs(file), 2696u);
}

// The sweep over the two sibling framework files (the gold: System.dll's
// 1711 and CoreLib's 1882 top-level rows all round-trip), plus System.dll's
// System.Uri spot pin (a REAL TypeDef there, not a forwarder -- mscorlib
// carries none, which is why the iteration-49 reflection-name walk finds
// Uri through the System module) and CoreLib's shifted tokens (String is
// 0x020000c9 there, not mscorlib's 0x02000073).
TEST(MetadataFileLookupsTest, GetTypeDefinitionRoundTripsSystemAndCoreLib) {
    MetadataFile system(SystemDllPath());
    ASSERT_TRUE(system.IsValid());
    EXPECT_EQ(system.GetTypeDefinition(TL("System", "Uri")), 0x0200003du);
    EXPECT_EQ(RoundTripTopLevelTypeDefs(system), 1711u);
#if defined(_WIN32)
    MetadataFile corelib(CoreLibPath());
    ASSERT_TRUE(corelib.IsValid());
    EXPECT_EQ(corelib.GetTypeDefinition(TL("System", "String")), 0x020000c9u);
    EXPECT_EQ(corelib.GetTypeDefinition(TL("System", "Math")), 0x020000a2u);
    EXPECT_EQ(RoundTripTopLevelTypeDefs(corelib), 1882u);
#endif
}

// The ExportedType full-name reader over the facade's whole 279-row
// forwarder table, gold-pinned row for row: the top-level names
// (arity split included -- "System.Action`1" splits to name Action, count
// 1) and the six NESTED forwarders whose Implementation column targets the
// outer ExportedType row (the DebuggableAttribute+DebuggingModes chain).
TEST(MetadataFileLookupsTest, GetFullTypeNameFromExportedTypeMatchesFacadeGold) {
#if defined(_WIN32)
    MetadataFile facade(FacadePath());
    ASSERT_TRUE(facade.IsValid());
    const auto rows = facade.GetExportedTypes();
    ASSERT_EQ(rows.size(), 279u);
    for (const auto& gold : kFacadeGoldRows) {
        FullTypeName name = GetFullTypeNameFromExportedType(facade, gold.Token);
        EXPECT_EQ(name.ReflectionName(), gold.Name)
            << "row token " << gold.Token;
    }
#else
    GTEST_SKIP() << "the GAC System.Runtime facade is Windows-only";
#endif
}

// GetTypeForwarder over the facade (the gold spot pins): the top-level
// forwarders resolve by their full names (the arity-carrying WeakReference`1
// keys on the SPLIT name + count, never the authored backtick spelling),
// the six nested forwarders resolve through their declaring-chain
// FullTypeNames, and the arity-mismatched / absent / nested-in-the-wrong-
// chain queries all miss.
TEST(MetadataFileLookupsTest, GetTypeForwarderResolvesFacadeForwarders) {
#if defined(_WIN32)
    MetadataFile facade(FacadePath());
    ASSERT_TRUE(facade.IsValid());
    // Top-level forwarders (the gold: System.Uri -> 0x2700010d).
    EXPECT_EQ(facade.GetTypeForwarder(FT(TL("System", "Uri"))), 0x2700010du);
    EXPECT_EQ(facade.GetTypeForwarder(FT(TL("System", "Void"))), 0x27000115u);
    // The arity split: WeakReference`1 keys on ("System", "WeakReference", 1);
    // the facade also forwards the NON-generic System.WeakReference (its own
    // row 0x27000116), so the arity-0 query hits that one. The arity-1 query
    // over System.Void (only the arity-0 row exists) is the true arity miss.
    EXPECT_EQ(facade.GetTypeForwarder(FT(TL("System", "WeakReference", 1))),
              0x27000117u);
    EXPECT_EQ(facade.GetTypeForwarder(FT(TL("System", "WeakReference", 0))),
              0x27000116u);
    EXPECT_EQ(facade.GetTypeForwarder(FT(TL("System", "Void", 1))), 0u);
    // The nested forwarders (the gold's NESTED section).
    EXPECT_EQ(facade.GetTypeForwarder(
                  FT(TL("System.Diagnostics", "DebuggableAttribute"))
                      .NestedType("DebuggingModes", 0)),
              0x2700004au);
    EXPECT_EQ(facade.GetTypeForwarder(
                  FT(TL("System.Runtime.CompilerServices",
                        "ConditionalWeakTable", 2))
                      .NestedType("CreateValueCallback", 0)),
              0x270000bcu);
    EXPECT_EQ(facade.GetTypeForwarder(
                  FT(TL("System.Runtime.CompilerServices", "RuntimeHelpers"))
                      .NestedType("CleanupCode", 0)),
              0x270000cfu);
    EXPECT_EQ(facade.GetTypeForwarder(
                  FT(TL("System.Runtime.CompilerServices", "RuntimeHelpers"))
                      .NestedType("TryCode", 0)),
              0x270000d0u);
    EXPECT_EQ(facade.GetTypeForwarder(
                  FT(TL("System", "TimeZoneInfo")).NestedType("AdjustmentRule", 0)),
              0x270000f8u);
    EXPECT_EQ(facade.GetTypeForwarder(
                  FT(TL("System", "TimeZoneInfo")).NestedType("TransitionTime", 0)),
              0x270000f9u);
    // The misses: an absent top-level name, an absent nesting, and the
    // nested name queried through the WRONG declaring type.
    EXPECT_EQ(facade.GetTypeForwarder(FT(TL("System", "NoSuchTypeAtAll"))), 0u);
    EXPECT_EQ(facade.GetTypeForwarder(
                  FT(TL("System", "String")).NestedType("NoSuchNested", 0)), 0u);
    EXPECT_EQ(facade.GetTypeForwarder(
                  FT(TL("System", "TimeZoneInfo")).NestedType("DebuggingModes", 0)),
              0u);
#else
    GTEST_SKIP() << "the GAC System.Runtime facade is Windows-only";
#endif
}

// The forwarder round-trip sweep: every one of the facade's 279 rows
// resolves back to its own token through its gold-derived full name (the
// probe's section C: forwarder-mismatches=0, dup-names=0), and System.dll's
// single forwarder resolves too (mscorlib and CoreLib carry none -- their
// lookups miss everything).
TEST(MetadataFileLookupsTest, GetTypeForwarderRoundTripsEveryFacadeRow) {
    MetadataFile system(SystemDllPath());
    ASSERT_TRUE(system.IsValid());
    EXPECT_EQ(system.GetTypeForwarder(
                  FT(TL("System.Threading", "SemaphoreFullException"))),
              0x27000001u);
    EXPECT_EQ(system.GetTypeForwarder(FT(TL("System", "String"))), 0u);
#if defined(_WIN32)
    MetadataFile facade(FacadePath());
    ASSERT_TRUE(facade.IsValid());
    for (const auto& gold : kFacadeGoldRows) {
        EXPECT_EQ(facade.GetTypeForwarder(
                      FullTypeName(gold.Name)),
                  gold.Token)
            << "forwarder miss for " << gold.Name;
    }
    MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    EXPECT_EQ(mscorlib.GetTypeForwarder(FT(TL("System", "String"))), 0u);
    EXPECT_EQ(mscorlib.GetTypeForwarder(
                  FT(TL("System", "String")).NestedType("NoSuchNested", 0)), 0u);
#else
    GTEST_SKIP() << "the GAC System.Runtime facade is Windows-only";
#endif
}

// Both lookups over the netmodule fixture and the invalid-file arm:
// tiny.netmodule's <Module> and Tiny resolve, everything else misses, and
// the forwarder table is empty; a file that never opened reports the
// miss arm for both lookups (the never-throw invalid-file convention).
TEST(MetadataFileLookupsTest, LookupsOverNetModuleAndInvalidFile) {
    std::string tinyPath = WriteTinyNetModule();
    MetadataFile tiny(tinyPath);
    ASSERT_TRUE(tiny.IsValid());
    EXPECT_EQ(tiny.GetTypeDefinition(TL("", "<Module>")), 0x02000001u);
    EXPECT_EQ(tiny.GetTypeDefinition(TL("", "Tiny")), 0x02000002u);
    EXPECT_EQ(tiny.GetTypeDefinition(TL("System", "String")), 0u);
    EXPECT_EQ(tiny.GetTypeForwarder(FT(TL("System", "String"))), 0u);
    std::remove(tinyPath.c_str());

    MetadataFile invalid("Z:\\no\\such\\file.dll");
    ASSERT_FALSE(invalid.IsValid());
    EXPECT_EQ(invalid.GetTypeDefinition(TL("System", "String")), 0u);
    EXPECT_EQ(invalid.GetTypeForwarder(FT(TL("System", "String"))), 0u);
}

// The ExportedType reader's throw arms: the nil token (the reader-family
// invalid_argument contract the C#'s row-shaped overload never reaches),
// a non-ExportedType token, and an out-of-range row.
TEST(MetadataFileLookupsTest, GetFullTypeNameFromExportedTypeThrowArms) {
    MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    EXPECT_THROW(GetFullTypeNameFromExportedType(mscorlib, 0),
                 std::invalid_argument);
    EXPECT_THROW(GetFullTypeNameFromExportedType(mscorlib, 0x02000001u),
                 std::out_of_range);
    EXPECT_THROW(
        GetFullTypeNameFromExportedType(mscorlib, 0x2700ffffu),
        std::out_of_range);
}
