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
// DEALINGS IN THE SOFTWARE.

// The AttributeListBuilder slice's gold fixtures (the TinyNetModule.hpp
// precedent): every expectation pinned against the REAL installed
// ICSharpCode.Decompiler 11.0 (the C:/temp-probe/AlProbe public-API probe
// driving SimpleCompilation(new PEFile(...)) over the identical local
// fixtures).
//
//   * kGoldAsmX: the module-level attribute renders -- the assembly's own
//     custom attributes + security declarations + the synthetic
//     [AssemblyVersion] + the [TypeForwardedTo] rows (the GAC facade's 279
//     forwarders), the module row's attributes, and the InternalsVisibleTo
//     short-name list + the InternalsVisibleTo(name) queries (mscorlib's 9
//     friend assemblies).
//   * kGoldCurated: the curated entity matrices -- the full GetAttributes
//     renders and the HasAttribute/GetAttribute drives over a fixed
//     KnownAttribute subset x curated mscorlib/System entities (the
//     [Serializable]/[StructLayout]/[FieldOffset]/[MarshalAs]/
//     [PermissionSet] constructions; the KnownAttribute.None drives pin the
//     C# ArgumentNullException the null-name classification throws).
//   * kGoldOptions: the same drives over a TypeSystemOptions.None module on
//     .NET 10 CoreLib -- the IgnoreAttribute gates in the KEEP direction
//     ([Nullable]/[NullableContext]/[IsReadOnly]/[IsByRefLike] appear).
//
// Regenerate with: dotnet run (in C:/temp-probe/AlProbe) -- all print >
// gold_all.txt, then python gen_fixture.py (this script).

#pragma once

namespace ILSpy::Tests::AttributeGold {

inline constexpr const char* kGoldAsmMscorlib =
R"gold(
ASM msc n=37
AA0:type=<System.Runtime.CompilerServices.CompilationRelaxationsAttribute>:ctor=<System.Runtime.CompilerServices.CompilationRelaxationsAttribute..ctor>:err=False:F=1:N=0
AA0:F0:type=<System.Int32>:val=num:Int32:8
AA1:type=<System.Diagnostics.DebuggableAttribute>:ctor=<System.Diagnostics.DebuggableAttribute..ctor>:err=False:F=1:N=0
AA1:F0:type=<System.Diagnostics.DebuggableAttribute+DebuggingModes>:val=num:Int32:2
AA2:type=<System.Runtime.InteropServices.GuidAttribute>:ctor=<System.Runtime.InteropServices.GuidAttribute..ctor>:err=False:F=1:N=0
AA2:F0:type=<System.String>:val=str:"BED7F4EA-1A96-11d2-8F08-00A0C9A6186D"
AA3:type=<System.Runtime.InteropServices.ComCompatibleVersionAttribute>:ctor=<System.Runtime.InteropServices.ComCompatibleVersionAttribute..ctor>:err=False:F=4:N=0
AA3:F0:type=<System.Int32>:val=num:Int32:1
AA3:F1:type=<System.Int32>:val=num:Int32:0
AA3:F2:type=<System.Int32>:val=num:Int32:3300
AA3:F3:type=<System.Int32>:val=num:Int32:0
AA4:type=<System.Runtime.InteropServices.TypeLibVersionAttribute>:ctor=<System.Runtime.InteropServices.TypeLibVersionAttribute..ctor>:err=False:F=2:N=0
AA4:F0:type=<System.Int32>:val=num:Int32:2
AA4:F1:type=<System.Int32>:val=num:Int32:4
AA5:type=<System.Runtime.CompilerServices.DefaultDependencyAttribute>:ctor=<System.Runtime.CompilerServices.DefaultDependencyAttribute..ctor>:err=False:F=1:N=0
AA5:F0:type=<System.Runtime.CompilerServices.LoadHint>:val=num:Int32:1
AA6:type=<System.Runtime.CompilerServices.StringFreezingAttribute>:ctor=<System.Runtime.CompilerServices.StringFreezingAttribute..ctor>:err=False:F=0:N=0
AA7:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
AA7:F0:type=<System.Boolean>:val=bool:False
AA8:type=<System.CLSCompliantAttribute>:ctor=<System.CLSCompliantAttribute..ctor>:err=False:F=1:N=0
AA8:F0:type=<System.Boolean>:val=bool:True
AA9:type=<System.Security.AllowPartiallyTrustedCallersAttribute>:ctor=<System.Security.AllowPartiallyTrustedCallersAttribute..ctor>:err=False:F=0:N=0
AA10:type=<System.Security.SecurityRulesAttribute>:ctor=<System.Security.SecurityRulesAttribute..ctor>:err=False:F=1:N=1
AA10:F0:type=<System.Security.SecurityRuleSet>:val=num:Byte:2
AA10:N0:name=SkipVerificationInFullTrust:kind=Property:type=<System.Boolean>:val=bool:True
AA11:type=<System.Reflection.AssemblyTitleAttribute>:ctor=<System.Reflection.AssemblyTitleAttribute..ctor>:err=False:F=1:N=0
AA11:F0:type=<System.String>:val=str:"mscorlib.dll"
AA12:type=<System.Reflection.AssemblyDescriptionAttribute>:ctor=<System.Reflection.AssemblyDescriptionAttribute..ctor>:err=False:F=1:N=0
AA12:F0:type=<System.String>:val=str:"mscorlib.dll"
AA13:type=<System.Reflection.AssemblyDefaultAliasAttribute>:ctor=<System.Reflection.AssemblyDefaultAliasAttribute..ctor>:err=False:F=1:N=0
AA13:F0:type=<System.String>:val=str:"mscorlib.dll"
AA14:type=<System.Reflection.AssemblyCompanyAttribute>:ctor=<System.Reflection.AssemblyCompanyAttribute..ctor>:err=False:F=1:N=0
AA14:F0:type=<System.String>:val=str:"Microsoft Corporation"
AA15:type=<System.Reflection.AssemblyProductAttribute>:ctor=<System.Reflection.AssemblyProductAttribute..ctor>:err=False:F=1:N=0
AA15:F0:type=<System.String>:val=str:"Microsoft\u00ae .NET Framework"
AA16:type=<System.Reflection.AssemblyCopyrightAttribute>:ctor=<System.Reflection.AssemblyCopyrightAttribute..ctor>:err=False:F=1:N=0
AA16:F0:type=<System.String>:val=str:"\u00a9 Microsoft Corporation.  All rights reserved."
AA17:type=<System.Reflection.AssemblyFileVersionAttribute>:ctor=<System.Reflection.AssemblyFileVersionAttribute..ctor>:err=False:F=1:N=0
AA17:F0:type=<System.String>:val=str:"4.8.9337.0"
AA18:type=<System.Reflection.AssemblyInformationalVersionAttribute>:ctor=<System.Reflection.AssemblyInformationalVersionAttribute..ctor>:err=False:F=1:N=0
AA18:F0:type=<System.String>:val=str:"4.8.9337.0"
AA19:type=<System.Resources.SatelliteContractVersionAttribute>:ctor=<System.Resources.SatelliteContractVersionAttribute..ctor>:err=False:F=1:N=0
AA19:F0:type=<System.String>:val=str:"4.0.0.0"
AA20:type=<System.Resources.NeutralResourcesLanguageAttribute>:ctor=<System.Resources.NeutralResourcesLanguageAttribute..ctor>:err=False:F=1:N=0
AA20:F0:type=<System.String>:val=str:"en-US"
AA21:type=<System.Reflection.AssemblyDelaySignAttribute>:ctor=<System.Reflection.AssemblyDelaySignAttribute..ctor>:err=False:F=1:N=0
AA21:F0:type=<System.Boolean>:val=bool:True
AA22:type=<System.Reflection.AssemblyKeyFileAttribute>:ctor=<System.Reflection.AssemblyKeyFileAttribute..ctor>:err=False:F=1:N=0
AA22:F0:type=<System.String>:val=str:"f:\\dd\\tools\\devdiv\\EcmaPublicKey.snk"
AA23:type=<System.Reflection.AssemblySignatureKeyAttribute>:ctor=<System.Reflection.AssemblySignatureKeyAttribute..ctor>:err=False:F=2:N=0
AA23:F0:type=<System.String>:val=str:"002400000c800000140100000602000000240000525341310008000001000100613399aff18ef1a2c2514a273a42d9042b72321f1757102df9ebada69923e2738406c21e5b801552ab8d200a65a235e001ac9adc25f2d811eb09496a4c6a59d4619589c69f5baf0c4179a47311d92555cd006acc8b5959f2bd6e10e360c34537a1d266da8085856583c85d81da7f3ec01ed9564c58d93d713cd0172c8e23a10f0239b80c96b07736f5d8b022542a4e74251a5f432824318b3539a5a087f8e53d2f135f9ca47f3bb2e10aff0af0849504fb7cea3ff192dc8de0edad64c68efde34c56d302ad55fd6e80f302d5efcdeae953658d3452561b5f36c542efdbdd9f888538d374cef106acf7d93a4445c3c73cd911f0571aaf3d54da12b11ddec375b3"
AA23:F1:type=<System.String>:val=str:"a5a866e1ee186f807668209f3b11236ace5e21f117803a3143abb126dd035d7d2f876b6938aaf2ee3414d5420d753621400db44a49c486ce134300a2106adb6bdb433590fef8ad5c43cba82290dc49530effd86523d9483c00f458af46890036b0e2c61d077d7fbac467a506eba29e467a87198b053c749aa2a4d2840c784e6d"
AA24:type=<System.Runtime.CompilerServices.RuntimeCompatibilityAttribute>:ctor=<System.Runtime.CompilerServices.RuntimeCompatibilityAttribute..ctor>:err=False:F=0:N=1
AA24:N0:name=WrapNonExceptionThrows:kind=Property:type=<System.Boolean>:val=bool:True
AA25:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA25:F0:type=<System.String>:val=str:"System, PublicKey=00000000000000000400000000000000"
AA25:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA26:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA26:F0:type=<System.String>:val=str:"System.Core, PublicKey=00000000000000000400000000000000"
AA26:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA27:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA27:F0:type=<System.String>:val=str:"System.Numerics, PublicKey=00000000000000000400000000000000"
AA27:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA28:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA28:F0:type=<System.String>:val=str:"System.Reflection.Context, PublicKey=00000000000000000400000000000000"
AA28:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA29:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA29:F0:type=<System.String>:val=str:"System.Runtime.WindowsRuntime, PublicKey=00000000000000000400000000000000"
AA29:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA30:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA30:F0:type=<System.String>:val=str:"System.Runtime.WindowsRuntime.UI.Xaml, PublicKey=00000000000000000400000000000000"
AA30:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA31:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA31:F0:type=<System.String>:val=str:"WindowsBase, PublicKey=0024000004800000940000000602000000240000525341310004000001000100B5FC90E7027F67871E773A8FDE8938C81DD402BA65B9201D60593E96C492651E889CC13F1415EBB53FAC1131AE0BD333C5EE6021672D9718EA31A8AEBD0DA0072F25D87DBA6FC90FFD598ED4DA35E44C398C454307E8E33B8426143DAEC9F596836F97C8F74750E5975C64E2189F45DEF46B2A2B1247ADC3652BF5C308055DA9"
AA31:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA32:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA32:F0:type=<System.String>:val=str:"PresentationCore, PublicKey=0024000004800000940000000602000000240000525341310004000001000100B5FC90E7027F67871E773A8FDE8938C81DD402BA65B9201D60593E96C492651E889CC13F1415EBB53FAC1131AE0BD333C5EE6021672D9718EA31A8AEBD0DA0072F25D87DBA6FC90FFD598ED4DA35E44C398C454307E8E33B8426143DAEC9F596836F97C8F74750E5975C64E2189F45DEF46B2A2B1247ADC3652BF5C308055DA9"
AA32:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA33:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA33:F0:type=<System.String>:val=str:"PresentationFramework, PublicKey=0024000004800000940000000602000000240000525341310004000001000100B5FC90E7027F67871E773A8FDE8938C81DD402BA65B9201D60593E96C492651E889CC13F1415EBB53FAC1131AE0BD333C5EE6021672D9718EA31A8AEBD0DA0072F25D87DBA6FC90FFD598ED4DA35E44C398C454307E8E33B8426143DAEC9F596836F97C8F74750E5975C64E2189F45DEF46B2A2B1247ADC3652BF5C308055DA9"
AA33:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA34:type=<System.Runtime.InteropServices.DefaultDllImportSearchPathsAttribute>:ctor=<System.Runtime.InteropServices.DefaultDllImportSearchPathsAttribute..ctor>:err=False:F=1:N=0
AA34:F0:type=<System.Runtime.InteropServices.DllImportSearchPath>:val=num:Int32:2050
AA35:type=<System.Security.Permissions.SecurityPermissionAttribute>:ctor=<System.Security.Permissions.SecurityPermissionAttribute..ctor>:err=False:F=1:N=1
AA35:F0:type=<System.Security.Permissions.SecurityAction>:val=num:Int32:8
AA35:N0:name=SkipVerification:kind=Property:type=<System.Boolean>:val=bool:True
AA36:type=<System.Reflection.AssemblyVersionAttribute>:ctor=<System.Reflection.AssemblyVersionAttribute..ctor>:err=False:F=1:N=0
AA36:F0:type=<System.String>:val=str:"4.0.0.0"
MOD msc n=1
MA0:type=<System.Security.UnverifiableCodeAttribute>:ctor=<System.Security.UnverifiableCodeAttribute..ctor>:err=False:F=0:N=0
IVT msc n=9
IVT msc [0] "System"
IVT msc [1] "System.Core"
IVT msc [2] "System.Numerics"
IVT msc [3] "System.Reflection.Context"
IVT msc [4] "System.Runtime.WindowsRuntime"
IVT msc [5] "System.Runtime.WindowsRuntime.UI.Xaml"
IVT msc [6] "WindowsBase"
IVT msc [7] "PresentationCore"
IVT msc [8] "PresentationFramework"
IVTQ msc "mscorlib"=False
IVTQ msc "System"=True
IVTQ msc "System.Core"=True
IVTQ msc "NO-SUCH"=False
IVTQ msc "system"=True
)gold";

inline constexpr const char* kGoldAsmSystem =
R"gold(
ASM sys n=31
AA0:type=<System.Runtime.CompilerServices.CompilationRelaxationsAttribute>:ctor=<System.Runtime.CompilerServices.CompilationRelaxationsAttribute..ctor>:err=False:F=1:N=0
AA0:F0:type=<System.Int32>:val=num:Int32:8
AA1:type=<System.Diagnostics.DebuggableAttribute>:ctor=<System.Diagnostics.DebuggableAttribute..ctor>:err=True:F=0:N=0
AA2:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA2:F0:type=<System.String>:val=str:"System.Net.Http, PublicKey=002400000480000094000000060200000024000052534131000400000100010007d1fa57c4aed9f0a32e84aa0faefd0de9e8fd6aec8f87fb03766c834c99921eb23be79ad9d5dcc1dd9ad236132102900b723cf980957fc4e177108fc607774f29e8320e92ea05ece4e821c0a5efe8f1645c4c0c93c1ab99285d622caa652c1dfad63d745d6f2de5f17e5eaf0fc4963d261c8a12436518206dc093344d5ad293"
AA2:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA3:type=<System.Runtime.CompilerServices.InternalsVisibleToAttribute>:ctor=<System.Runtime.CompilerServices.InternalsVisibleToAttribute..ctor>:err=False:F=1:N=1
AA3:F0:type=<System.String>:val=str:"System.Net.Http.WebRequest, PublicKey=002400000480000094000000060200000024000052534131000400000100010007d1fa57c4aed9f0a32e84aa0faefd0de9e8fd6aec8f87fb03766c834c99921eb23be79ad9d5dcc1dd9ad236132102900b723cf980957fc4e177108fc607774f29e8320e92ea05ece4e821c0a5efe8f1645c4c0c93c1ab99285d622caa652c1dfad63d745d6f2de5f17e5eaf0fc4963d261c8a12436518206dc093344d5ad293"
AA3:N0:name=AllInternalsVisible:kind=Property:type=<System.Boolean>:val=bool:False
AA4:type=<System.Runtime.CompilerServices.StringFreezingAttribute>:ctor=<System.Runtime.CompilerServices.StringFreezingAttribute..ctor>:err=False:F=0:N=0
AA5:type=<System.Runtime.CompilerServices.DefaultDependencyAttribute>:ctor=<System.Runtime.CompilerServices.DefaultDependencyAttribute..ctor>:err=True:F=0:N=0
AA6:type=<System.Runtime.InteropServices.TypeLibVersionAttribute>:ctor=<System.Runtime.InteropServices.TypeLibVersionAttribute..ctor>:err=False:F=2:N=0
AA6:F0:type=<System.Int32>:val=num:Int32:2
AA6:F1:type=<System.Int32>:val=num:Int32:4
AA7:type=<System.Drawing.BitmapSuffixInSatelliteAssemblyAttribute>:ctor=<System.Drawing.BitmapSuffixInSatelliteAssemblyAttribute..ctor>:err=False:F=0:N=0
AA8:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
AA8:F0:type=<System.Boolean>:val=bool:False
AA9:type=<System.CLSCompliantAttribute>:ctor=<System.CLSCompliantAttribute..ctor>:err=False:F=1:N=0
AA9:F0:type=<System.Boolean>:val=bool:True
AA10:type=<System.Security.AllowPartiallyTrustedCallersAttribute>:ctor=<System.Security.AllowPartiallyTrustedCallersAttribute..ctor>:err=False:F=0:N=0
AA11:type=<System.Security.SecurityRulesAttribute>:ctor=<System.Security.SecurityRulesAttribute..ctor>:err=True:F=0:N=0
AA12:type=<System.Reflection.AssemblyTitleAttribute>:ctor=<System.Reflection.AssemblyTitleAttribute..ctor>:err=False:F=1:N=0
AA12:F0:type=<System.String>:val=str:"System.dll"
AA13:type=<System.Reflection.AssemblyDescriptionAttribute>:ctor=<System.Reflection.AssemblyDescriptionAttribute..ctor>:err=False:F=1:N=0
AA13:F0:type=<System.String>:val=str:"System.dll"
AA14:type=<System.Reflection.AssemblyDefaultAliasAttribute>:ctor=<System.Reflection.AssemblyDefaultAliasAttribute..ctor>:err=False:F=1:N=0
AA14:F0:type=<System.String>:val=str:"System.dll"
AA15:type=<System.Reflection.AssemblyCompanyAttribute>:ctor=<System.Reflection.AssemblyCompanyAttribute..ctor>:err=False:F=1:N=0
AA15:F0:type=<System.String>:val=str:"Microsoft Corporation"
AA16:type=<System.Reflection.AssemblyProductAttribute>:ctor=<System.Reflection.AssemblyProductAttribute..ctor>:err=False:F=1:N=0
AA16:F0:type=<System.String>:val=str:"Microsoft\u00ae .NET Framework"
AA17:type=<System.Reflection.AssemblyCopyrightAttribute>:ctor=<System.Reflection.AssemblyCopyrightAttribute..ctor>:err=False:F=1:N=0
AA17:F0:type=<System.String>:val=str:"\u00a9 Microsoft Corporation.  All rights reserved."
AA18:type=<System.Reflection.AssemblyFileVersionAttribute>:ctor=<System.Reflection.AssemblyFileVersionAttribute..ctor>:err=False:F=1:N=0
AA18:F0:type=<System.String>:val=str:"4.8.9340.0"
AA19:type=<System.Reflection.AssemblyInformationalVersionAttribute>:ctor=<System.Reflection.AssemblyInformationalVersionAttribute..ctor>:err=False:F=1:N=0
AA19:F0:type=<System.String>:val=str:"4.8.9340.0"
AA20:type=<System.Resources.SatelliteContractVersionAttribute>:ctor=<System.Resources.SatelliteContractVersionAttribute..ctor>:err=False:F=1:N=0
AA20:F0:type=<System.String>:val=str:"4.0.0.0"
AA21:type=<System.Resources.NeutralResourcesLanguageAttribute>:ctor=<System.Resources.NeutralResourcesLanguageAttribute..ctor>:err=False:F=1:N=0
AA21:F0:type=<System.String>:val=str:"en-US"
AA22:type=<System.Reflection.AssemblyDelaySignAttribute>:ctor=<System.Reflection.AssemblyDelaySignAttribute..ctor>:err=False:F=1:N=0
AA22:F0:type=<System.Boolean>:val=bool:True
AA23:type=<System.Reflection.AssemblyKeyFileAttribute>:ctor=<System.Reflection.AssemblyKeyFileAttribute..ctor>:err=False:F=1:N=0
AA23:F0:type=<System.String>:val=str:"F:\\dd\\tools\\devdiv\\EcmaPublicKey.snk"
AA24:type=<System.Reflection.AssemblySignatureKeyAttribute>:ctor=<System.Reflection.AssemblySignatureKeyAttribute..ctor>:err=False:F=2:N=0
AA24:F0:type=<System.String>:val=str:"002400000c800000140100000602000000240000525341310008000001000100613399aff18ef1a2c2514a273a42d9042b72321f1757102df9ebada69923e2738406c21e5b801552ab8d200a65a235e001ac9adc25f2d811eb09496a4c6a59d4619589c69f5baf0c4179a47311d92555cd006acc8b5959f2bd6e10e360c34537a1d266da8085856583c85d81da7f3ec01ed9564c58d93d713cd0172c8e23a10f0239b80c96b07736f5d8b022542a4e74251a5f432824318b3539a5a087f8e53d2f135f9ca47f3bb2e10aff0af0849504fb7cea3ff192dc8de0edad64c68efde34c56d302ad55fd6e80f302d5efcdeae953658d3452561b5f36c542efdbdd9f888538d374cef106acf7d93a4445c3c73cd911f0571aaf3d54da12b11ddec375b3"
AA24:F1:type=<System.String>:val=str:"a5a866e1ee186f807668209f3b11236ace5e21f117803a3143abb126dd035d7d2f876b6938aaf2ee3414d5420d753621400db44a49c486ce134300a2106adb6bdb433590fef8ad5c43cba82290dc49530effd86523d9483c00f458af46890036b0e2c61d077d7fbac467a506eba29e467a87198b053c749aa2a4d2840c784e6d"
AA25:type=<System.Runtime.InteropServices.ComCompatibleVersionAttribute>:ctor=<System.Runtime.InteropServices.ComCompatibleVersionAttribute..ctor>:err=False:F=4:N=0
AA25:F0:type=<System.Int32>:val=num:Int32:1
AA25:F1:type=<System.Int32>:val=num:Int32:0
AA25:F2:type=<System.Int32>:val=num:Int32:3300
AA25:F3:type=<System.Int32>:val=num:Int32:0
AA26:type=<System.Runtime.CompilerServices.RuntimeCompatibilityAttribute>:ctor=<System.Runtime.CompilerServices.RuntimeCompatibilityAttribute..ctor>:err=False:F=0:N=1
AA26:N0:name=WrapNonExceptionThrows:kind=Property:type=<System.Boolean>:val=bool:True
AA27:type=<System.Runtime.InteropServices.DefaultDllImportSearchPathsAttribute>:ctor=<System.Runtime.InteropServices.DefaultDllImportSearchPathsAttribute..ctor>:err=True:F=0:N=0
AA28:type=<System.Security.Permissions.SecurityPermissionAttribute>:ctor=<<null>>:err=False:F=1:N=1
AA28:F0:type=<System.Security.Permissions.SecurityAction>:val=num:Int32:8
AA28:N0:name=SkipVerification:kind=Property:type=<System.Boolean>:val=bool:True
AA29:type=<System.Reflection.AssemblyVersionAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA29:F0:type=<System.String>:val=str:"4.0.0.0"
AA30:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA30:F0:type=<System.Type>:val=type:<System.Threading.SemaphoreFullException>
MOD sys n=1
MA0:type=<System.Security.UnverifiableCodeAttribute>:ctor=<System.Security.UnverifiableCodeAttribute..ctor>:err=False:F=0:N=0
IVT sys n=2
IVT sys [0] "System.Net.Http"
IVT sys [1] "System.Net.Http.WebRequest"
IVTQ sys "mscorlib"=False
IVTQ sys "System"=False
IVTQ sys "System.Core"=False
IVTQ sys "NO-SUCH"=False
IVTQ sys "system"=False
)gold";

inline constexpr const char* kGoldAsmCoreLib =
R"gold(
ASM core n=22
AA0:type=<System.Runtime.CompilerServices.CompilationRelaxationsAttribute>:ctor=<System.Runtime.CompilerServices.CompilationRelaxationsAttribute..ctor>:err=False:F=1:N=0
AA0:F0:type=<System.Int32>:val=num:Int32:8
AA1:type=<System.Runtime.CompilerServices.RuntimeCompatibilityAttribute>:ctor=<System.Runtime.CompilerServices.RuntimeCompatibilityAttribute..ctor>:err=False:F=0:N=1
AA1:N0:name=WrapNonExceptionThrows:kind=Property:type=<System.Boolean>:val=bool:True
AA2:type=<System.Diagnostics.DebuggableAttribute>:ctor=<System.Diagnostics.DebuggableAttribute..ctor>:err=False:F=1:N=0
AA2:F0:type=<System.Diagnostics.DebuggableAttribute+DebuggingModes>:val=num:Int32:2
AA3:type=<System.Reflection.Metadata.MetadataUpdateHandlerAttribute>:ctor=<System.Reflection.Metadata.MetadataUpdateHandlerAttribute..ctor>:err=False:F=1:N=0
AA3:F0:type=<System.Type>:val=type:<System.Reflection.Metadata.RuntimeTypeMetadataUpdateHandler>
AA4:type=<System.CLSCompliantAttribute>:ctor=<System.CLSCompliantAttribute..ctor>:err=False:F=1:N=0
AA4:F0:type=<System.Boolean>:val=bool:True
AA5:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
AA5:F0:type=<System.Boolean>:val=bool:False
AA6:type=<System.Runtime.InteropServices.DefaultDllImportSearchPathsAttribute>:ctor=<System.Runtime.InteropServices.DefaultDllImportSearchPathsAttribute..ctor>:err=False:F=1:N=0
AA6:F0:type=<System.Runtime.InteropServices.DllImportSearchPath>:val=num:Int32:2050
AA7:type=<System.Reflection.AssemblyMetadataAttribute>:ctor=<System.Reflection.AssemblyMetadataAttribute..ctor>:err=False:F=2:N=0
AA7:F0:type=<System.String>:val=str:"Serviceable"
AA7:F1:type=<System.String>:val=str:"True"
AA8:type=<System.Reflection.AssemblyMetadataAttribute>:ctor=<System.Reflection.AssemblyMetadataAttribute..ctor>:err=False:F=2:N=0
AA8:F0:type=<System.String>:val=str:"IsTrimmable"
AA8:F1:type=<System.String>:val=str:"True"
AA9:type=<System.Resources.NeutralResourcesLanguageAttribute>:ctor=<System.Resources.NeutralResourcesLanguageAttribute..ctor>:err=False:F=1:N=0
AA9:F0:type=<System.String>:val=str:"en-US"
AA10:type=<System.Runtime.CompilerServices.DisableRuntimeMarshallingAttribute>:ctor=<System.Runtime.CompilerServices.DisableRuntimeMarshallingAttribute..ctor>:err=False:F=0:N=0
AA11:type=<System.Runtime.Versioning.TargetFrameworkAttribute>:ctor=<System.Runtime.Versioning.TargetFrameworkAttribute..ctor>:err=False:F=1:N=1
AA11:F0:type=<System.String>:val=str:".NETCoreApp,Version=v10.0"
AA11:N0:name=FrameworkDisplayName:kind=Property:type=<System.String>:val=str:".NET 10.0"
AA12:type=<System.Reflection.AssemblyCompanyAttribute>:ctor=<System.Reflection.AssemblyCompanyAttribute..ctor>:err=False:F=1:N=0
AA12:F0:type=<System.String>:val=str:"Microsoft Corporation"
AA13:type=<System.Reflection.AssemblyConfigurationAttribute>:ctor=<System.Reflection.AssemblyConfigurationAttribute..ctor>:err=False:F=1:N=0
AA13:F0:type=<System.String>:val=str:"Release"
AA14:type=<System.Reflection.AssemblyCopyrightAttribute>:ctor=<System.Reflection.AssemblyCopyrightAttribute..ctor>:err=False:F=1:N=0
AA14:F0:type=<System.String>:val=str:"\u00a9 Microsoft Corporation. All rights reserved."
AA15:type=<System.Reflection.AssemblyDescriptionAttribute>:ctor=<System.Reflection.AssemblyDescriptionAttribute..ctor>:err=False:F=1:N=0
AA15:F0:type=<System.String>:val=str:"System.Private.CoreLib"
AA16:type=<System.Reflection.AssemblyFileVersionAttribute>:ctor=<System.Reflection.AssemblyFileVersionAttribute..ctor>:err=False:F=1:N=0
AA16:F0:type=<System.String>:val=str:"10.0.826.23019"
AA17:type=<System.Reflection.AssemblyInformationalVersionAttribute>:ctor=<System.Reflection.AssemblyInformationalVersionAttribute..ctor>:err=False:F=1:N=0
AA17:F0:type=<System.String>:val=str:"10.0.8-servicing.26229.119+94ea82652cdd4e0f8046b5bd5becbd11461482ca"
AA18:type=<System.Reflection.AssemblyProductAttribute>:ctor=<System.Reflection.AssemblyProductAttribute..ctor>:err=False:F=1:N=0
AA18:F0:type=<System.String>:val=str:"Microsoft\u00ae .NET"
AA19:type=<System.Reflection.AssemblyTitleAttribute>:ctor=<System.Reflection.AssemblyTitleAttribute..ctor>:err=False:F=1:N=0
AA19:F0:type=<System.String>:val=str:"System.Private.CoreLib"
AA20:type=<System.Reflection.AssemblyMetadataAttribute>:ctor=<System.Reflection.AssemblyMetadataAttribute..ctor>:err=False:F=2:N=0
AA20:F0:type=<System.String>:val=str:"RepositoryUrl"
AA20:F1:type=<System.String>:val=str:"https://github.com/dotnet/dotnet"
AA21:type=<System.Reflection.AssemblyVersionAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA21:F0:type=<System.String>:val=str:"10.0.0.0"
MOD core n=3
MA0:type=<System.Runtime.CompilerServices.RefSafetyRulesAttribute>:ctor=<System.Runtime.CompilerServices.RefSafetyRulesAttribute..ctor>:err=False:F=1:N=0
MA0:F0:type=<System.Int32>:val=num:Int32:11
MA1:type=<System.Runtime.CompilerServices.NullablePublicOnlyAttribute>:ctor=<System.Runtime.CompilerServices.NullablePublicOnlyAttribute..ctor>:err=False:F=1:N=0
MA1:F0:type=<System.Boolean>:val=bool:False
MA2:type=<System.Runtime.CompilerServices.SkipLocalsInitAttribute>:ctor=<System.Runtime.CompilerServices.SkipLocalsInitAttribute..ctor>:err=False:F=0:N=0
IVT core n=0
IVTQ core "mscorlib"=False
IVTQ core "System"=False
IVTQ core "System.Core"=False
IVTQ core "NO-SUCH"=False
IVTQ core "system"=False
)gold";

inline constexpr const char* kGoldAsmFacade =
R"gold(
ASM facade n=283
AA0:type=<System.Reflection.AssemblyTitleAttribute>:ctor=<System.Reflection.AssemblyTitleAttribute..ctor>:err=False:F=1:N=0
AA0:F0:type=<System.String>:val=str:"System.Runtime"
AA1:type=<System.Reflection.AssemblyDescriptionAttribute>:ctor=<System.Reflection.AssemblyDescriptionAttribute..ctor>:err=False:F=1:N=0
AA1:F0:type=<System.String>:val=str:"System.Runtime"
AA2:type=<System.Reflection.AssemblyDefaultAliasAttribute>:ctor=<System.Reflection.AssemblyDefaultAliasAttribute..ctor>:err=False:F=1:N=0
AA2:F0:type=<System.String>:val=str:"System.Runtime"
AA3:type=<System.Reflection.AssemblyCompanyAttribute>:ctor=<System.Reflection.AssemblyCompanyAttribute..ctor>:err=False:F=1:N=0
AA3:F0:type=<System.String>:val=str:"Microsoft Corporation"
AA4:type=<System.Reflection.AssemblyProductAttribute>:ctor=<System.Reflection.AssemblyProductAttribute..ctor>:err=False:F=1:N=0
AA4:F0:type=<System.String>:val=str:"Microsoft\u00ae .NET Framework"
AA5:type=<System.Reflection.AssemblyCopyrightAttribute>:ctor=<System.Reflection.AssemblyCopyrightAttribute..ctor>:err=False:F=1:N=0
AA5:F0:type=<System.String>:val=str:"\u00a9 Microsoft Corporation.  All rights reserved."
AA6:type=<System.Reflection.AssemblyMetadataAttribute>:ctor=<System.Reflection.AssemblyMetadataAttribute..ctor>:err=False:F=2:N=0
AA6:F0:type=<System.String>:val=str:""
AA6:F1:type=<System.String>:val=str:""
AA7:type=<System.Reflection.AssemblyFileVersionAttribute>:ctor=<System.Reflection.AssemblyFileVersionAttribute..ctor>:err=False:F=1:N=0
AA7:F0:type=<System.String>:val=str:"4.8.9221.0"
AA8:type=<System.Reflection.AssemblyInformationalVersionAttribute>:ctor=<System.Reflection.AssemblyInformationalVersionAttribute..ctor>:err=False:F=1:N=0
AA8:F0:type=<System.String>:val=str:"4.8.9221.0"
AA9:type=<System.Reflection.AssemblyVersionAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA9:F0:type=<System.String>:val=str:"4.0.0.0"
AA10:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA10:F0:type=<System.Type>:val=type:<System.Action>
AA11:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA11:F0:type=<System.Type>:val=type:<System.Action`1>
AA12:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA12:F0:type=<System.Type>:val=type:<System.Action`10>
AA13:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA13:F0:type=<System.Type>:val=type:<System.Action`11>
AA14:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA14:F0:type=<System.Type>:val=type:<System.Action`12>
AA15:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA15:F0:type=<System.Type>:val=type:<System.Action`13>
AA16:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA16:F0:type=<System.Type>:val=type:<System.Action`14>
AA17:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA17:F0:type=<System.Type>:val=type:<System.Action`15>
AA18:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA18:F0:type=<System.Type>:val=type:<System.Action`16>
AA19:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA19:F0:type=<System.Type>:val=type:<System.Action`2>
AA20:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA20:F0:type=<System.Type>:val=type:<System.Action`3>
AA21:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA21:F0:type=<System.Type>:val=type:<System.Action`4>
AA22:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA22:F0:type=<System.Type>:val=type:<System.Action`5>
AA23:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA23:F0:type=<System.Type>:val=type:<System.Action`6>
AA24:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA24:F0:type=<System.Type>:val=type:<System.Action`7>
AA25:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA25:F0:type=<System.Type>:val=type:<System.Action`8>
AA26:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA26:F0:type=<System.Type>:val=type:<System.Action`9>
AA27:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA27:F0:type=<System.Type>:val=type:<System.Activator>
AA28:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA28:F0:type=<System.Type>:val=type:<System.ArgumentException>
AA29:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA29:F0:type=<System.Type>:val=type:<System.ArgumentNullException>
AA30:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA30:F0:type=<System.Type>:val=type:<System.ArgumentOutOfRangeException>
AA31:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA31:F0:type=<System.Type>:val=type:<System.ArithmeticException>
AA32:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA32:F0:type=<System.Type>:val=type:<System.Array>
AA33:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA33:F0:type=<System.Type>:val=type:<System.ArraySegment`1>
AA34:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA34:F0:type=<System.Type>:val=type:<System.ArrayTypeMismatchException>
AA35:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA35:F0:type=<System.Type>:val=type:<System.AsyncCallback>
AA36:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA36:F0:type=<System.Type>:val=type:<System.Attribute>
AA37:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA37:F0:type=<System.Type>:val=type:<System.AttributeTargets>
AA38:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA38:F0:type=<System.Type>:val=type:<System.AttributeUsageAttribute>
AA39:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA39:F0:type=<System.Type>:val=type:<System.BadImageFormatException>
AA40:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA40:F0:type=<System.Type>:val=type:<System.Boolean>
AA41:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA41:F0:type=<System.Type>:val=type:<System.Buffer>
AA42:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA42:F0:type=<System.Type>:val=type:<System.Byte>
AA43:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA43:F0:type=<System.Type>:val=type:<System.Char>
AA44:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA44:F0:type=<System.Type>:val=type:<System.CLSCompliantAttribute>
AA45:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA45:F0:type=<System.Type>:val=type:<System.Collections.DictionaryEntry>
AA46:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA46:F0:type=<System.Type>:val=type:<System.Collections.Generic.ICollection`1>
AA47:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA47:F0:type=<System.Type>:val=type:<System.Collections.Generic.IComparer`1>
AA48:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA48:F0:type=<System.Type>:val=type:<System.Collections.Generic.IDictionary`2>
AA49:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA49:F0:type=<System.Type>:val=type:<System.Collections.Generic.IEnumerable`1>
AA50:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA50:F0:type=<System.Type>:val=type:<System.Collections.Generic.IEnumerator`1>
AA51:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA51:F0:type=<System.Type>:val=type:<System.Collections.Generic.IEqualityComparer`1>
AA52:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA52:F0:type=<System.Type>:val=type:<System.Collections.Generic.IList`1>
AA53:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA53:F0:type=<System.Type>:val=type:<System.Collections.Generic.IReadOnlyCollection`1>
AA54:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA54:F0:type=<System.Type>:val=type:<System.Collections.Generic.IReadOnlyDictionary`2>
AA55:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA55:F0:type=<System.Type>:val=type:<System.Collections.Generic.IReadOnlyList`1>
AA56:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA56:F0:type=<System.Type>:val=type:<System.Collections.Generic.ISet`1>
AA57:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA57:F0:type=<System.Type>:val=type:<System.Collections.Generic.KeyNotFoundException>
AA58:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA58:F0:type=<System.Type>:val=type:<System.Collections.Generic.KeyValuePair`2>
AA59:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA59:F0:type=<System.Type>:val=type:<System.Collections.ICollection>
AA60:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA60:F0:type=<System.Type>:val=type:<System.Collections.IComparer>
AA61:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA61:F0:type=<System.Type>:val=type:<System.Collections.IDictionary>
AA62:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA62:F0:type=<System.Type>:val=type:<System.Collections.IDictionaryEnumerator>
AA63:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA63:F0:type=<System.Type>:val=type:<System.Collections.IEnumerable>
AA64:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA64:F0:type=<System.Type>:val=type:<System.Collections.IEnumerator>
AA65:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA65:F0:type=<System.Type>:val=type:<System.Collections.IEqualityComparer>
AA66:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA66:F0:type=<System.Type>:val=type:<System.Collections.IList>
AA67:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA67:F0:type=<System.Type>:val=type:<System.Collections.IStructuralComparable>
AA68:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA68:F0:type=<System.Type>:val=type:<System.Collections.IStructuralEquatable>
AA69:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA69:F0:type=<System.Type>:val=type:<System.Collections.ObjectModel.Collection`1>
AA70:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA70:F0:type=<System.Type>:val=type:<System.Collections.ObjectModel.ReadOnlyCollection`1>
AA71:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA71:F0:type=<System.Type>:val=type:<System.Comparison`1>
AA72:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA72:F0:type=<System.Type>:val=type:<System.ComponentModel.DefaultValueAttribute>
AA73:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA73:F0:type=<System.Type>:val=type:<System.ComponentModel.EditorBrowsableAttribute>
AA74:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA74:F0:type=<System.Type>:val=type:<System.ComponentModel.EditorBrowsableState>
AA75:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA75:F0:type=<System.Type>:val=type:<System.DateTime>
AA76:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA76:F0:type=<System.Type>:val=type:<System.DateTimeKind>
AA77:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA77:F0:type=<System.Type>:val=type:<System.DateTimeOffset>
AA78:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA78:F0:type=<System.Type>:val=type:<System.DayOfWeek>
AA79:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA79:F0:type=<System.Type>:val=type:<System.Decimal>
AA80:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA80:F0:type=<System.Type>:val=type:<System.Delegate>
AA81:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA81:F0:type=<System.Type>:val=type:<System.Diagnostics.ConditionalAttribute>
AA82:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA82:F0:type=<System.Type>:val=type:<System.Diagnostics.DebuggableAttribute>
AA83:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA83:F0:type=<System.Type>:val=type:<System.DivideByZeroException>
AA84:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA84:F0:type=<System.Type>:val=type:<System.Double>
AA85:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA85:F0:type=<System.Type>:val=type:<System.Enum>
AA86:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA86:F0:type=<System.Type>:val=type:<System.EventArgs>
AA87:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA87:F0:type=<System.Type>:val=type:<System.EventHandler>
AA88:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA88:F0:type=<System.Type>:val=type:<System.EventHandler`1>
AA89:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA89:F0:type=<System.Type>:val=type:<System.Exception>
AA90:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA90:F0:type=<System.Type>:val=type:<System.FieldAccessException>
AA91:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA91:F0:type=<System.Type>:val=type:<System.FlagsAttribute>
AA92:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA92:F0:type=<System.Type>:val=type:<System.FormatException>
AA93:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA93:F0:type=<System.Type>:val=type:<System.FormattableString>
AA94:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA94:F0:type=<System.Type>:val=type:<System.Func`1>
AA95:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA95:F0:type=<System.Type>:val=type:<System.Func`10>
AA96:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA96:F0:type=<System.Type>:val=type:<System.Func`11>
AA97:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA97:F0:type=<System.Type>:val=type:<System.Func`12>
AA98:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA98:F0:type=<System.Type>:val=type:<System.Func`13>
AA99:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA99:F0:type=<System.Type>:val=type:<System.Func`14>
AA100:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA100:F0:type=<System.Type>:val=type:<System.Func`15>
AA101:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA101:F0:type=<System.Type>:val=type:<System.Func`16>
AA102:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA102:F0:type=<System.Type>:val=type:<System.Func`17>
AA103:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA103:F0:type=<System.Type>:val=type:<System.Func`2>
AA104:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA104:F0:type=<System.Type>:val=type:<System.Func`3>
AA105:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA105:F0:type=<System.Type>:val=type:<System.Func`4>
AA106:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA106:F0:type=<System.Type>:val=type:<System.Func`5>
AA107:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA107:F0:type=<System.Type>:val=type:<System.Func`6>
AA108:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA108:F0:type=<System.Type>:val=type:<System.Func`7>
AA109:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA109:F0:type=<System.Type>:val=type:<System.Func`8>
AA110:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA110:F0:type=<System.Type>:val=type:<System.Func`9>
AA111:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA111:F0:type=<System.Type>:val=type:<System.GC>
AA112:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA112:F0:type=<System.Type>:val=type:<System.GCCollectionMode>
AA113:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA113:F0:type=<System.Type>:val=type:<System.Globalization.DateTimeStyles>
AA114:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA114:F0:type=<System.Type>:val=type:<System.Globalization.NumberStyles>
AA115:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA115:F0:type=<System.Type>:val=type:<System.Globalization.TimeSpanStyles>
AA116:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA116:F0:type=<System.Type>:val=type:<System.Guid>
AA117:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA117:F0:type=<System.Type>:val=type:<System.IAsyncResult>
AA118:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA118:F0:type=<System.Type>:val=type:<System.IComparable>
AA119:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA119:F0:type=<System.Type>:val=type:<System.IComparable`1>
AA120:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA120:F0:type=<System.Type>:val=type:<System.IConvertible>
AA121:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA121:F0:type=<System.Type>:val=type:<System.ICustomFormatter>
AA122:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA122:F0:type=<System.Type>:val=type:<System.IDisposable>
AA123:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA123:F0:type=<System.Type>:val=type:<System.IEquatable`1>
AA124:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA124:F0:type=<System.Type>:val=type:<System.IFormatProvider>
AA125:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA125:F0:type=<System.Type>:val=type:<System.IFormattable>
AA126:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA126:F0:type=<System.Type>:val=type:<System.IndexOutOfRangeException>
AA127:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA127:F0:type=<System.Type>:val=type:<System.InsufficientExecutionStackException>
AA128:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA128:F0:type=<System.Type>:val=type:<System.Int16>
AA129:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA129:F0:type=<System.Type>:val=type:<System.Int32>
AA130:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA130:F0:type=<System.Type>:val=type:<System.Int64>
AA131:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA131:F0:type=<System.Type>:val=type:<System.IntPtr>
AA132:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA132:F0:type=<System.Type>:val=type:<System.InvalidCastException>
AA133:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA133:F0:type=<System.Type>:val=type:<System.InvalidOperationException>
AA134:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA134:F0:type=<System.Type>:val=type:<System.InvalidProgramException>
AA135:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA135:F0:type=<System.Type>:val=type:<System.InvalidTimeZoneException>
AA136:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA136:F0:type=<System.Type>:val=type:<System.IO.DirectoryNotFoundException>
AA137:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA137:F0:type=<System.Type>:val=type:<System.IO.FileLoadException>
AA138:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA138:F0:type=<System.Type>:val=type:<System.IO.FileNotFoundException>
AA139:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA139:F0:type=<System.Type>:val=type:<System.IO.IOException>
AA140:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA140:F0:type=<System.Type>:val=type:<System.IO.PathTooLongException>
AA141:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA141:F0:type=<System.Type>:val=type:<System.IObservable`1>
AA142:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA142:F0:type=<System.Type>:val=type:<System.IObserver`1>
AA143:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA143:F0:type=<System.Type>:val=type:<System.IProgress`1>
AA144:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA144:F0:type=<System.Type>:val=type:<System.Lazy`1>
AA145:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA145:F0:type=<System.Type>:val=type:<System.Lazy`2>
AA146:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA146:F0:type=<System.Type>:val=type:<System.MemberAccessException>
AA147:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA147:F0:type=<System.Type>:val=type:<System.MethodAccessException>
AA148:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA148:F0:type=<System.Type>:val=type:<System.MissingFieldException>
AA149:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA149:F0:type=<System.Type>:val=type:<System.MissingMemberException>
AA150:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA150:F0:type=<System.Type>:val=type:<System.MissingMethodException>
AA151:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA151:F0:type=<System.Type>:val=type:<System.MTAThreadAttribute>
AA152:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA152:F0:type=<System.Type>:val=type:<System.MulticastDelegate>
AA153:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA153:F0:type=<System.Type>:val=type:<System.NotImplementedException>
AA154:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA154:F0:type=<System.Type>:val=type:<System.NotSupportedException>
AA155:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA155:F0:type=<System.Type>:val=type:<System.Nullable>
AA156:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA156:F0:type=<System.Type>:val=type:<System.Nullable`1>
AA157:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA157:F0:type=<System.Type>:val=type:<System.NullReferenceException>
AA158:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA158:F0:type=<System.Type>:val=type:<System.Object>
AA159:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA159:F0:type=<System.Type>:val=type:<System.ObjectDisposedException>
AA160:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA160:F0:type=<System.Type>:val=type:<System.ObsoleteAttribute>
AA161:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA161:F0:type=<System.Type>:val=type:<System.OutOfMemoryException>
AA162:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA162:F0:type=<System.Type>:val=type:<System.OverflowException>
AA163:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA163:F0:type=<System.Type>:val=type:<System.ParamArrayAttribute>
AA164:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA164:F0:type=<System.Type>:val=type:<System.PlatformNotSupportedException>
AA165:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA165:F0:type=<System.Type>:val=type:<System.Predicate`1>
AA166:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA166:F0:type=<System.Type>:val=type:<System.RankException>
AA167:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA167:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyCompanyAttribute>
AA168:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA168:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyConfigurationAttribute>
AA169:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA169:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyCopyrightAttribute>
AA170:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA170:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyCultureAttribute>
AA171:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA171:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyDefaultAliasAttribute>
AA172:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA172:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyDelaySignAttribute>
AA173:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA173:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyDescriptionAttribute>
AA174:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA174:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyFileVersionAttribute>
AA175:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA175:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyFlagsAttribute>
AA176:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA176:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyInformationalVersionAttribute>
AA177:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA177:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyKeyFileAttribute>
AA178:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA178:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyKeyNameAttribute>
AA179:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA179:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyMetadataAttribute>
AA180:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA180:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyNameFlags>
AA181:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA181:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyProductAttribute>
AA182:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA182:F0:type=<System.Type>:val=type:<System.Reflection.AssemblySignatureKeyAttribute>
AA183:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA183:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyTitleAttribute>
AA184:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA184:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyTrademarkAttribute>
AA185:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA185:F0:type=<System.Type>:val=type:<System.Reflection.AssemblyVersionAttribute>
AA186:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA186:F0:type=<System.Type>:val=type:<System.Reflection.DefaultMemberAttribute>
AA187:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA187:F0:type=<System.Type>:val=type:<System.Reflection.ProcessorArchitecture>
AA188:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA188:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.AccessedThroughPropertyAttribute>
AA189:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA189:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.AsyncStateMachineAttribute>
AA190:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA190:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.CallerFilePathAttribute>
AA191:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA191:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.CallerLineNumberAttribute>
AA192:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA192:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.CallerMemberNameAttribute>
AA193:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA193:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.CompilationRelaxationsAttribute>
AA194:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA194:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.CompilerGeneratedAttribute>
AA195:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA195:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.ConditionalWeakTable`2>
AA196:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA196:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.CustomConstantAttribute>
AA197:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA197:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.DateTimeConstantAttribute>
AA198:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA198:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.DecimalConstantAttribute>
AA199:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA199:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.DisablePrivateReflectionAttribute>
AA200:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA200:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.ExtensionAttribute>
AA201:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA201:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.FixedBufferAttribute>
AA202:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA202:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.FormattableStringFactory>
AA203:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA203:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.IndexerNameAttribute>
AA204:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA204:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.InternalsVisibleToAttribute>
AA205:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA205:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.IsConst>
AA206:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA206:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.IStrongBox>
AA207:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA207:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.IsVolatile>
AA208:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA208:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.IteratorStateMachineAttribute>
AA209:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA209:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.MethodImplAttribute>
AA210:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA210:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.MethodImplOptions>
AA211:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA211:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.ReferenceAssemblyAttribute>
AA212:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA212:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.RuntimeCompatibilityAttribute>
AA213:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA213:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.RuntimeHelpers>
AA214:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA214:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.StateMachineAttribute>
AA215:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA215:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.StrongBox`1>
AA216:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA216:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.TypeForwardedFromAttribute>
AA217:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA217:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.TypeForwardedToAttribute>
AA218:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA218:F0:type=<System.Type>:val=type:<System.Runtime.CompilerServices.UnsafeValueTypeAttribute>
AA219:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA219:F0:type=<System.Type>:val=type:<System.Runtime.ExceptionServices.ExceptionDispatchInfo>
AA220:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA220:F0:type=<System.Type>:val=type:<System.Runtime.GCLargeObjectHeapCompactionMode>
AA221:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA221:F0:type=<System.Type>:val=type:<System.Runtime.GCLatencyMode>
AA222:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA222:F0:type=<System.Type>:val=type:<System.Runtime.GCSettings>
AA223:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA223:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices.CharSet>
AA224:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA224:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices.ComVisibleAttribute>
AA225:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA225:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices.FieldOffsetAttribute>
AA226:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA226:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices.LayoutKind>
AA227:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA227:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices.OutAttribute>
AA228:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA228:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices.StructLayoutAttribute>
AA229:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA229:F0:type=<System.Type>:val=type:<System.Runtime.Versioning.TargetFrameworkAttribute>
AA230:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA230:F0:type=<System.Type>:val=type:<System.RuntimeFieldHandle>
AA231:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA231:F0:type=<System.Type>:val=type:<System.RuntimeMethodHandle>
AA232:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA232:F0:type=<System.Type>:val=type:<System.RuntimeTypeHandle>
AA233:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA233:F0:type=<System.Type>:val=type:<System.SByte>
AA234:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA234:F0:type=<System.Type>:val=type:<System.Security.AllowPartiallyTrustedCallersAttribute>
AA235:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA235:F0:type=<System.Type>:val=type:<System.Security.SecurityCriticalAttribute>
AA236:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA236:F0:type=<System.Type>:val=type:<System.Security.SecurityException>
AA237:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA237:F0:type=<System.Type>:val=type:<System.Security.SecuritySafeCriticalAttribute>
AA238:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA238:F0:type=<System.Type>:val=type:<System.Security.SecurityTransparentAttribute>
AA239:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA239:F0:type=<System.Type>:val=type:<System.Security.VerificationException>
AA240:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA240:F0:type=<System.Type>:val=type:<System.Single>
AA241:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA241:F0:type=<System.Type>:val=type:<System.STAThreadAttribute>
AA242:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA242:F0:type=<System.Type>:val=type:<System.String>
AA243:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA243:F0:type=<System.Type>:val=type:<System.StringComparison>
AA244:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA244:F0:type=<System.Type>:val=type:<System.StringSplitOptions>
AA245:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA245:F0:type=<System.Type>:val=type:<System.Text.StringBuilder>
AA246:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA246:F0:type=<System.Type>:val=type:<System.Threading.LazyThreadSafetyMode>
AA247:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA247:F0:type=<System.Type>:val=type:<System.Threading.Timeout>
AA248:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA248:F0:type=<System.Type>:val=type:<System.Threading.WaitHandle>
AA249:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA249:F0:type=<System.Type>:val=type:<System.ThreadStaticAttribute>
AA250:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA250:F0:type=<System.Type>:val=type:<System.TimeoutException>
AA251:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA251:F0:type=<System.Type>:val=type:<System.TimeSpan>
AA252:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA252:F0:type=<System.Type>:val=type:<System.TimeZoneInfo>
AA253:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA253:F0:type=<System.Type>:val=type:<System.Tuple>
AA254:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA254:F0:type=<System.Type>:val=type:<System.Tuple`1>
AA255:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA255:F0:type=<System.Type>:val=type:<System.Tuple`2>
AA256:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA256:F0:type=<System.Type>:val=type:<System.Tuple`3>
AA257:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA257:F0:type=<System.Type>:val=type:<System.Tuple`4>
AA258:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA258:F0:type=<System.Type>:val=type:<System.Tuple`5>
AA259:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA259:F0:type=<System.Type>:val=type:<System.Tuple`6>
AA260:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA260:F0:type=<System.Type>:val=type:<System.Tuple`7>
AA261:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA261:F0:type=<System.Type>:val=type:<System.Tuple`8>
AA262:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA262:F0:type=<System.Type>:val=type:<System.Type>
AA263:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA263:F0:type=<System.Type>:val=type:<System.TypeAccessException>
AA264:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA264:F0:type=<System.Type>:val=type:<System.TypeCode>
AA265:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA265:F0:type=<System.Type>:val=type:<System.TypeInitializationException>
AA266:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA266:F0:type=<System.Type>:val=type:<System.TypeLoadException>
AA267:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA267:F0:type=<System.Type>:val=type:<System.UInt16>
AA268:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA268:F0:type=<System.Type>:val=type:<System.UInt32>
AA269:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA269:F0:type=<System.Type>:val=type:<System.UInt64>
AA270:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA270:F0:type=<System.Type>:val=type:<System.UIntPtr>
AA271:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA271:F0:type=<System.Type>:val=type:<System.UnauthorizedAccessException>
AA272:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA272:F0:type=<System.Type>:val=type:<System.Uri>
AA273:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA273:F0:type=<System.Type>:val=type:<System.UriComponents>
AA274:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA274:F0:type=<System.Type>:val=type:<System.UriFormat>
AA275:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA275:F0:type=<System.Type>:val=type:<System.UriFormatException>
AA276:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA276:F0:type=<System.Type>:val=type:<System.UriHostNameType>
AA277:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA277:F0:type=<System.Type>:val=type:<System.UriKind>
AA278:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA278:F0:type=<System.Type>:val=type:<System.ValueType>
AA279:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA279:F0:type=<System.Type>:val=type:<System.Version>
AA280:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA280:F0:type=<System.Type>:val=type:<System.Void>
AA281:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA281:F0:type=<System.Type>:val=type:<System.WeakReference>
AA282:type=<System.Runtime.CompilerServices.TypeForwardedToAttribute>:ctor=<<null>>:err=False:F=1:N=0
AA282:F0:type=<System.Type>:val=type:<System.WeakReference`1>
MOD facade n=0
IVT facade n=0
IVTQ facade "mscorlib"=False
IVTQ facade "System"=False
IVTQ facade "System.Core"=False
IVTQ facade "NO-SUCH"=False
IVTQ facade "system"=False
)gold";

inline constexpr const char* kGoldAsmTiny =
R"gold(
ASM tiny n=0
MOD tiny n=0
IVT tiny n=0
IVTQ tiny "mscorlib"=False
IVTQ tiny "System"=False
IVTQ tiny "System.Core"=False
IVTQ tiny "NO-SUCH"=False
IVTQ tiny "system"=False
)gold";

inline constexpr const char* kGoldCurated =
R"gold(
C type System.Exception
E 0200007C n=5
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Runtime.InteropServices.ClassInterfaceAttribute>:ctor=<System.Runtime.InteropServices.ClassInterfaceAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Runtime.InteropServices.ClassInterfaceType>:val=num:Int32:0
A2:type=<System.Runtime.InteropServices.ComDefaultInterfaceAttribute>:ctor=<System.Runtime.InteropServices.ComDefaultInterfaceAttribute..ctor>:err=False:F=1:N=0
A2:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices._Exception>
A3:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
A3:F0:type=<System.Boolean>:val=bool:True
A4:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0
H T:System.Exception None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.Exception Serializable=True
H T:System.Exception ComImport=False
H T:System.Exception SpecialName=False
H T:System.Exception StructLayout=False
H T:System.Exception Flags=False
H T:System.Exception Obsolete=False
H T:System.Exception Extension=False
H T:System.Exception CompilerGenerated=False
H T:System.Exception Nullable=False
H T:System.Exception NullableContext=False
H T:System.Exception IsReadOnly=False
H T:System.Exception IsByRefLike=False
H T:System.Exception Dynamic=False
H T:System.Exception TupleElementNames=False
H T:System.Exception DecimalConstant=False
H T:System.Exception DefaultMember=False
H T:System.Exception ParamArray=False
H T:System.Exception Optional=False
H T:System.Exception In=False
H T:System.Exception Out=False
H T:System.Exception FieldOffset=False
H T:System.Exception NonSerialized=False
H T:System.Exception MarshalAs=False
H T:System.Exception PermissionSet=False
H T:System.Exception DefaultParameterValue=False
H T:System.Exception AssemblyVersion=False
H T:System.Exception InternalsVisibleTo=False
H T:System.Exception TypeForwardedTo=False
G T:System.Exception None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.Exception Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G T:System.Exception ComImport null
G T:System.Exception SpecialName null
G T:System.Exception StructLayout null
G T:System.Exception Flags null
G T:System.Exception Obsolete null
G T:System.Exception Extension null
G T:System.Exception CompilerGenerated null
G T:System.Exception Nullable null
G T:System.Exception NullableContext null
G T:System.Exception IsReadOnly null
G T:System.Exception IsByRefLike null
G T:System.Exception Dynamic null
G T:System.Exception TupleElementNames null
G T:System.Exception DecimalConstant null
G T:System.Exception DefaultMember null
G T:System.Exception ParamArray null
G T:System.Exception Optional null
G T:System.Exception In null
G T:System.Exception Out null
G T:System.Exception FieldOffset null
G T:System.Exception NonSerialized null
G T:System.Exception MarshalAs null
G T:System.Exception PermissionSet null
G T:System.Exception DefaultParameterValue null
G T:System.Exception AssemblyVersion null
G T:System.Exception InternalsVisibleTo null
G T:System.Exception TypeForwardedTo null
C type System.String
E 02000073 n=4
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Reflection.DefaultMemberAttribute>:ctor=<System.Reflection.DefaultMemberAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.String>:val=str:"Chars"
A2:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
A2:F0:type=<System.Boolean>:val=bool:True
A3:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0
H T:System.String None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.String Serializable=True
H T:System.String ComImport=False
H T:System.String SpecialName=False
H T:System.String StructLayout=False
H T:System.String Flags=False
H T:System.String Obsolete=False
H T:System.String Extension=False
H T:System.String CompilerGenerated=False
H T:System.String Nullable=False
H T:System.String NullableContext=False
H T:System.String IsReadOnly=False
H T:System.String IsByRefLike=False
H T:System.String Dynamic=False
H T:System.String TupleElementNames=False
H T:System.String DecimalConstant=False
H T:System.String DefaultMember=True
H T:System.String ParamArray=False
H T:System.String Optional=False
H T:System.String In=False
H T:System.String Out=False
H T:System.String FieldOffset=False
H T:System.String NonSerialized=False
H T:System.String MarshalAs=False
H T:System.String PermissionSet=False
H T:System.String DefaultParameterValue=False
H T:System.String AssemblyVersion=False
H T:System.String InternalsVisibleTo=False
H T:System.String TypeForwardedTo=False
G T:System.String None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.String Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G T:System.String ComImport null
G T:System.String SpecialName null
G T:System.String StructLayout null
G T:System.String Flags null
G T:System.String Obsolete null
G T:System.String Extension null
G T:System.String CompilerGenerated null
G T:System.String Nullable null
G T:System.String NullableContext null
G T:System.String IsReadOnly null
G T:System.String IsByRefLike null
G T:System.String Dynamic null
G T:System.String TupleElementNames null
G T:System.String DecimalConstant null
G T:System.String DefaultMember found
GA:type=<System.Reflection.DefaultMemberAttribute>:ctor=<System.Reflection.DefaultMemberAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.String>:val=str:"Chars"
G T:System.String ParamArray null
G T:System.String Optional null
G T:System.String In null
G T:System.String Out null
G T:System.String FieldOffset null
G T:System.String NonSerialized null
G T:System.String MarshalAs null
G T:System.String PermissionSet null
G T:System.String DefaultParameterValue null
G T:System.String AssemblyVersion null
G T:System.String InternalsVisibleTo null
G T:System.String TypeForwardedTo null
C type System.Math
E 0200010B n=1
A0:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0
H T:System.Math None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.Math Serializable=False
H T:System.Math ComImport=False
H T:System.Math SpecialName=False
H T:System.Math StructLayout=False
H T:System.Math Flags=False
H T:System.Math Obsolete=False
H T:System.Math Extension=False
H T:System.Math CompilerGenerated=False
H T:System.Math Nullable=False
H T:System.Math NullableContext=False
H T:System.Math IsReadOnly=False
H T:System.Math IsByRefLike=False
H T:System.Math Dynamic=False
H T:System.Math TupleElementNames=False
H T:System.Math DecimalConstant=False
H T:System.Math DefaultMember=False
H T:System.Math ParamArray=False
H T:System.Math Optional=False
H T:System.Math In=False
H T:System.Math Out=False
H T:System.Math FieldOffset=False
H T:System.Math NonSerialized=False
H T:System.Math MarshalAs=False
H T:System.Math PermissionSet=False
H T:System.Math DefaultParameterValue=False
H T:System.Math AssemblyVersion=False
H T:System.Math InternalsVisibleTo=False
H T:System.Math TypeForwardedTo=False
G T:System.Math None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.Math Serializable null
G T:System.Math ComImport null
G T:System.Math SpecialName null
G T:System.Math StructLayout null
G T:System.Math Flags null
G T:System.Math Obsolete null
G T:System.Math Extension null
G T:System.Math CompilerGenerated null
G T:System.Math Nullable null
G T:System.Math NullableContext null
G T:System.Math IsReadOnly null
G T:System.Math IsByRefLike null
G T:System.Math Dynamic null
G T:System.Math TupleElementNames null
G T:System.Math DecimalConstant null
G T:System.Math DefaultMember null
G T:System.Math ParamArray null
G T:System.Math Optional null
G T:System.Math In null
G T:System.Math Out null
G T:System.Math FieldOffset null
G T:System.Math NonSerialized null
G T:System.Math MarshalAs null
G T:System.Math PermissionSet null
G T:System.Math DefaultParameterValue null
G T:System.Math AssemblyVersion null
G T:System.Math InternalsVisibleTo null
G T:System.Math TypeForwardedTo null
C type System.Runtime.CompilerServices.ExtensionAttribute
E 020008B6 n=2
A0:type=<System.AttributeUsageAttribute>:ctor=<System.AttributeUsageAttribute..ctor>:err=False:F=1:N=0
A0:F0:type=<System.AttributeTargets>:val=num:Int32:69
A1:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0
H T:System.Runtime.CompilerServices.ExtensionAttribute None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.Runtime.CompilerServices.ExtensionAttribute Serializable=False
H T:System.Runtime.CompilerServices.ExtensionAttribute ComImport=False
H T:System.Runtime.CompilerServices.ExtensionAttribute SpecialName=False
H T:System.Runtime.CompilerServices.ExtensionAttribute StructLayout=False
H T:System.Runtime.CompilerServices.ExtensionAttribute Flags=False
H T:System.Runtime.CompilerServices.ExtensionAttribute Obsolete=False
H T:System.Runtime.CompilerServices.ExtensionAttribute Extension=False
H T:System.Runtime.CompilerServices.ExtensionAttribute CompilerGenerated=False
H T:System.Runtime.CompilerServices.ExtensionAttribute Nullable=False
H T:System.Runtime.CompilerServices.ExtensionAttribute NullableContext=False
H T:System.Runtime.CompilerServices.ExtensionAttribute IsReadOnly=False
H T:System.Runtime.CompilerServices.ExtensionAttribute IsByRefLike=False
H T:System.Runtime.CompilerServices.ExtensionAttribute Dynamic=False
H T:System.Runtime.CompilerServices.ExtensionAttribute TupleElementNames=False
H T:System.Runtime.CompilerServices.ExtensionAttribute DecimalConstant=False
H T:System.Runtime.CompilerServices.ExtensionAttribute DefaultMember=False
H T:System.Runtime.CompilerServices.ExtensionAttribute ParamArray=False
H T:System.Runtime.CompilerServices.ExtensionAttribute Optional=False
H T:System.Runtime.CompilerServices.ExtensionAttribute In=False
H T:System.Runtime.CompilerServices.ExtensionAttribute Out=False
H T:System.Runtime.CompilerServices.ExtensionAttribute FieldOffset=False
H T:System.Runtime.CompilerServices.ExtensionAttribute NonSerialized=False
H T:System.Runtime.CompilerServices.ExtensionAttribute MarshalAs=False
H T:System.Runtime.CompilerServices.ExtensionAttribute PermissionSet=False
H T:System.Runtime.CompilerServices.ExtensionAttribute DefaultParameterValue=False
H T:System.Runtime.CompilerServices.ExtensionAttribute AssemblyVersion=False
H T:System.Runtime.CompilerServices.ExtensionAttribute InternalsVisibleTo=False
H T:System.Runtime.CompilerServices.ExtensionAttribute TypeForwardedTo=False
G T:System.Runtime.CompilerServices.ExtensionAttribute None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.Runtime.CompilerServices.ExtensionAttribute Serializable null
G T:System.Runtime.CompilerServices.ExtensionAttribute ComImport null
G T:System.Runtime.CompilerServices.ExtensionAttribute SpecialName null
G T:System.Runtime.CompilerServices.ExtensionAttribute StructLayout null
G T:System.Runtime.CompilerServices.ExtensionAttribute Flags null
G T:System.Runtime.CompilerServices.ExtensionAttribute Obsolete null
G T:System.Runtime.CompilerServices.ExtensionAttribute Extension null
G T:System.Runtime.CompilerServices.ExtensionAttribute CompilerGenerated null
G T:System.Runtime.CompilerServices.ExtensionAttribute Nullable null
G T:System.Runtime.CompilerServices.ExtensionAttribute NullableContext null
G T:System.Runtime.CompilerServices.ExtensionAttribute IsReadOnly null
G T:System.Runtime.CompilerServices.ExtensionAttribute IsByRefLike null
G T:System.Runtime.CompilerServices.ExtensionAttribute Dynamic null
G T:System.Runtime.CompilerServices.ExtensionAttribute TupleElementNames null
G T:System.Runtime.CompilerServices.ExtensionAttribute DecimalConstant null
G T:System.Runtime.CompilerServices.ExtensionAttribute DefaultMember null
G T:System.Runtime.CompilerServices.ExtensionAttribute ParamArray null
G T:System.Runtime.CompilerServices.ExtensionAttribute Optional null
G T:System.Runtime.CompilerServices.ExtensionAttribute In null
G T:System.Runtime.CompilerServices.ExtensionAttribute Out null
G T:System.Runtime.CompilerServices.ExtensionAttribute FieldOffset null
G T:System.Runtime.CompilerServices.ExtensionAttribute NonSerialized null
G T:System.Runtime.CompilerServices.ExtensionAttribute MarshalAs null
G T:System.Runtime.CompilerServices.ExtensionAttribute PermissionSet null
G T:System.Runtime.CompilerServices.ExtensionAttribute DefaultParameterValue null
G T:System.Runtime.CompilerServices.ExtensionAttribute AssemblyVersion null
G T:System.Runtime.CompilerServices.ExtensionAttribute InternalsVisibleTo null
G T:System.Runtime.CompilerServices.ExtensionAttribute TypeForwardedTo null
C type System.Int32
E 020000FB n=3
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Boolean>:val=bool:True
A2:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0
H T:System.Int32 None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.Int32 Serializable=True
H T:System.Int32 ComImport=False
H T:System.Int32 SpecialName=False
H T:System.Int32 StructLayout=False
H T:System.Int32 Flags=False
H T:System.Int32 Obsolete=False
H T:System.Int32 Extension=False
H T:System.Int32 CompilerGenerated=False
H T:System.Int32 Nullable=False
H T:System.Int32 NullableContext=False
H T:System.Int32 IsReadOnly=False
H T:System.Int32 IsByRefLike=False
H T:System.Int32 Dynamic=False
H T:System.Int32 TupleElementNames=False
H T:System.Int32 DecimalConstant=False
H T:System.Int32 DefaultMember=False
H T:System.Int32 ParamArray=False
H T:System.Int32 Optional=False
H T:System.Int32 In=False
H T:System.Int32 Out=False
H T:System.Int32 FieldOffset=False
H T:System.Int32 NonSerialized=False
H T:System.Int32 MarshalAs=False
H T:System.Int32 PermissionSet=False
H T:System.Int32 DefaultParameterValue=False
H T:System.Int32 AssemblyVersion=False
H T:System.Int32 InternalsVisibleTo=False
H T:System.Int32 TypeForwardedTo=False
G T:System.Int32 None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.Int32 Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G T:System.Int32 ComImport null
G T:System.Int32 SpecialName null
G T:System.Int32 StructLayout null
G T:System.Int32 Flags null
G T:System.Int32 Obsolete null
G T:System.Int32 Extension null
G T:System.Int32 CompilerGenerated null
G T:System.Int32 Nullable null
G T:System.Int32 NullableContext null
G T:System.Int32 IsReadOnly null
G T:System.Int32 IsByRefLike null
G T:System.Int32 Dynamic null
G T:System.Int32 TupleElementNames null
G T:System.Int32 DecimalConstant null
G T:System.Int32 DefaultMember null
G T:System.Int32 ParamArray null
G T:System.Int32 Optional null
G T:System.Int32 In null
G T:System.Int32 Out null
G T:System.Int32 FieldOffset null
G T:System.Int32 NonSerialized null
G T:System.Int32 MarshalAs null
G T:System.Int32 PermissionSet null
G T:System.Int32 DefaultParameterValue null
G T:System.Int32 AssemblyVersion null
G T:System.Int32 InternalsVisibleTo null
G T:System.Int32 TypeForwardedTo null
C type System.IComparable`1
E 02000059 n=1
A0:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0
H T:System.IComparable`1 None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.IComparable`1 Serializable=False
H T:System.IComparable`1 ComImport=False
H T:System.IComparable`1 SpecialName=False
H T:System.IComparable`1 StructLayout=False
H T:System.IComparable`1 Flags=False
H T:System.IComparable`1 Obsolete=False
H T:System.IComparable`1 Extension=False
H T:System.IComparable`1 CompilerGenerated=False
H T:System.IComparable`1 Nullable=False
H T:System.IComparable`1 NullableContext=False
H T:System.IComparable`1 IsReadOnly=False
H T:System.IComparable`1 IsByRefLike=False
H T:System.IComparable`1 Dynamic=False
H T:System.IComparable`1 TupleElementNames=False
H T:System.IComparable`1 DecimalConstant=False
H T:System.IComparable`1 DefaultMember=False
H T:System.IComparable`1 ParamArray=False
H T:System.IComparable`1 Optional=False
H T:System.IComparable`1 In=False
H T:System.IComparable`1 Out=False
H T:System.IComparable`1 FieldOffset=False
H T:System.IComparable`1 NonSerialized=False
H T:System.IComparable`1 MarshalAs=False
H T:System.IComparable`1 PermissionSet=False
H T:System.IComparable`1 DefaultParameterValue=False
H T:System.IComparable`1 AssemblyVersion=False
H T:System.IComparable`1 InternalsVisibleTo=False
H T:System.IComparable`1 TypeForwardedTo=False
G T:System.IComparable`1 None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.IComparable`1 Serializable null
G T:System.IComparable`1 ComImport null
G T:System.IComparable`1 SpecialName null
G T:System.IComparable`1 StructLayout null
G T:System.IComparable`1 Flags null
G T:System.IComparable`1 Obsolete null
G T:System.IComparable`1 Extension null
G T:System.IComparable`1 CompilerGenerated null
G T:System.IComparable`1 Nullable null
G T:System.IComparable`1 NullableContext null
G T:System.IComparable`1 IsReadOnly null
G T:System.IComparable`1 IsByRefLike null
G T:System.IComparable`1 Dynamic null
G T:System.IComparable`1 TupleElementNames null
G T:System.IComparable`1 DecimalConstant null
G T:System.IComparable`1 DefaultMember null
G T:System.IComparable`1 ParamArray null
G T:System.IComparable`1 Optional null
G T:System.IComparable`1 In null
G T:System.IComparable`1 Out null
G T:System.IComparable`1 FieldOffset null
G T:System.IComparable`1 NonSerialized null
G T:System.IComparable`1 MarshalAs null
G T:System.IComparable`1 PermissionSet null
G T:System.IComparable`1 DefaultParameterValue null
G T:System.IComparable`1 AssemblyVersion null
G T:System.IComparable`1 InternalsVisibleTo null
G T:System.IComparable`1 TypeForwardedTo null
C type System.EventArgs
E 020000DF n=3
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Boolean>:val=bool:True
A2:type=<__DynamicallyInvokableAttribute>:ctor=<__DynamicallyInvokableAttribute..ctor>:err=False:F=0:N=0
H T:System.EventArgs None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.EventArgs Serializable=True
H T:System.EventArgs ComImport=False
H T:System.EventArgs SpecialName=False
H T:System.EventArgs StructLayout=False
H T:System.EventArgs Flags=False
H T:System.EventArgs Obsolete=False
H T:System.EventArgs Extension=False
H T:System.EventArgs CompilerGenerated=False
H T:System.EventArgs Nullable=False
H T:System.EventArgs NullableContext=False
H T:System.EventArgs IsReadOnly=False
H T:System.EventArgs IsByRefLike=False
H T:System.EventArgs Dynamic=False
H T:System.EventArgs TupleElementNames=False
H T:System.EventArgs DecimalConstant=False
H T:System.EventArgs DefaultMember=False
H T:System.EventArgs ParamArray=False
H T:System.EventArgs Optional=False
H T:System.EventArgs In=False
H T:System.EventArgs Out=False
H T:System.EventArgs FieldOffset=False
H T:System.EventArgs NonSerialized=False
H T:System.EventArgs MarshalAs=False
H T:System.EventArgs PermissionSet=False
H T:System.EventArgs DefaultParameterValue=False
H T:System.EventArgs AssemblyVersion=False
H T:System.EventArgs InternalsVisibleTo=False
H T:System.EventArgs TypeForwardedTo=False
G T:System.EventArgs None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.EventArgs Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G T:System.EventArgs ComImport null
G T:System.EventArgs SpecialName null
G T:System.EventArgs StructLayout null
G T:System.EventArgs Flags null
G T:System.EventArgs Obsolete null
G T:System.EventArgs Extension null
G T:System.EventArgs CompilerGenerated null
G T:System.EventArgs Nullable null
G T:System.EventArgs NullableContext null
G T:System.EventArgs IsReadOnly null
G T:System.EventArgs IsByRefLike null
G T:System.EventArgs Dynamic null
G T:System.EventArgs TupleElementNames null
G T:System.EventArgs DecimalConstant null
G T:System.EventArgs DefaultMember null
G T:System.EventArgs ParamArray null
G T:System.EventArgs Optional null
G T:System.EventArgs In null
G T:System.EventArgs Out null
G T:System.EventArgs FieldOffset null
G T:System.EventArgs NonSerialized null
G T:System.EventArgs MarshalAs null
G T:System.EventArgs PermissionSet null
G T:System.EventArgs DefaultParameterValue null
G T:System.EventArgs AssemblyVersion null
G T:System.EventArgs InternalsVisibleTo null
G T:System.EventArgs TypeForwardedTo null
C type System.ArgIterator
E 020000A9 n=0
H T:System.ArgIterator None=False
H T:System.ArgIterator Serializable=False
H T:System.ArgIterator ComImport=False
H T:System.ArgIterator SpecialName=False
H T:System.ArgIterator StructLayout=False
H T:System.ArgIterator Flags=False
H T:System.ArgIterator Obsolete=False
H T:System.ArgIterator Extension=False
H T:System.ArgIterator CompilerGenerated=False
H T:System.ArgIterator Nullable=False
H T:System.ArgIterator NullableContext=False
H T:System.ArgIterator IsReadOnly=False
H T:System.ArgIterator IsByRefLike=False
H T:System.ArgIterator Dynamic=False
H T:System.ArgIterator TupleElementNames=False
H T:System.ArgIterator DecimalConstant=False
H T:System.ArgIterator DefaultMember=False
H T:System.ArgIterator ParamArray=False
H T:System.ArgIterator Optional=False
H T:System.ArgIterator In=False
H T:System.ArgIterator Out=False
H T:System.ArgIterator FieldOffset=False
H T:System.ArgIterator NonSerialized=False
H T:System.ArgIterator MarshalAs=False
H T:System.ArgIterator PermissionSet=False
H T:System.ArgIterator DefaultParameterValue=False
H T:System.ArgIterator AssemblyVersion=False
H T:System.ArgIterator InternalsVisibleTo=False
H T:System.ArgIterator TypeForwardedTo=False
G T:System.ArgIterator None null
G T:System.ArgIterator Serializable null
G T:System.ArgIterator ComImport null
G T:System.ArgIterator SpecialName null
G T:System.ArgIterator StructLayout null
G T:System.ArgIterator Flags null
G T:System.ArgIterator Obsolete null
G T:System.ArgIterator Extension null
G T:System.ArgIterator CompilerGenerated null
G T:System.ArgIterator Nullable null
G T:System.ArgIterator NullableContext null
G T:System.ArgIterator IsReadOnly null
G T:System.ArgIterator IsByRefLike null
G T:System.ArgIterator Dynamic null
G T:System.ArgIterator TupleElementNames null
G T:System.ArgIterator DecimalConstant null
G T:System.ArgIterator DefaultMember null
G T:System.ArgIterator ParamArray null
G T:System.ArgIterator Optional null
G T:System.ArgIterator In null
G T:System.ArgIterator Out null
G T:System.ArgIterator FieldOffset null
G T:System.ArgIterator NonSerialized null
G T:System.ArgIterator MarshalAs null
G T:System.ArgIterator PermissionSet null
G T:System.ArgIterator DefaultParameterValue null
G T:System.ArgIterator AssemblyVersion null
G T:System.ArgIterator InternalsVisibleTo null
G T:System.ArgIterator TypeForwardedTo null
C type System.DBNull
E 020000D2 n=2
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Boolean>:val=bool:True
H T:System.DBNull None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.DBNull Serializable=True
H T:System.DBNull ComImport=False
H T:System.DBNull SpecialName=False
H T:System.DBNull StructLayout=False
H T:System.DBNull Flags=False
H T:System.DBNull Obsolete=False
H T:System.DBNull Extension=False
H T:System.DBNull CompilerGenerated=False
H T:System.DBNull Nullable=False
H T:System.DBNull NullableContext=False
H T:System.DBNull IsReadOnly=False
H T:System.DBNull IsByRefLike=False
H T:System.DBNull Dynamic=False
H T:System.DBNull TupleElementNames=False
H T:System.DBNull DecimalConstant=False
H T:System.DBNull DefaultMember=False
H T:System.DBNull ParamArray=False
H T:System.DBNull Optional=False
H T:System.DBNull In=False
H T:System.DBNull Out=False
H T:System.DBNull FieldOffset=False
H T:System.DBNull NonSerialized=False
H T:System.DBNull MarshalAs=False
H T:System.DBNull PermissionSet=False
H T:System.DBNull DefaultParameterValue=False
H T:System.DBNull AssemblyVersion=False
H T:System.DBNull InternalsVisibleTo=False
H T:System.DBNull TypeForwardedTo=False
G T:System.DBNull None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.DBNull Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G T:System.DBNull ComImport null
G T:System.DBNull SpecialName null
G T:System.DBNull StructLayout null
G T:System.DBNull Flags null
G T:System.DBNull Obsolete null
G T:System.DBNull Extension null
G T:System.DBNull CompilerGenerated null
G T:System.DBNull Nullable null
G T:System.DBNull NullableContext null
G T:System.DBNull IsReadOnly null
G T:System.DBNull IsByRefLike null
G T:System.DBNull Dynamic null
G T:System.DBNull TupleElementNames null
G T:System.DBNull DecimalConstant null
G T:System.DBNull DefaultMember null
G T:System.DBNull ParamArray null
G T:System.DBNull Optional null
G T:System.DBNull In null
G T:System.DBNull Out null
G T:System.DBNull FieldOffset null
G T:System.DBNull NonSerialized null
G T:System.DBNull MarshalAs null
G T:System.DBNull PermissionSet null
G T:System.DBNull DefaultParameterValue null
G T:System.DBNull AssemblyVersion null
G T:System.DBNull InternalsVisibleTo null
G T:System.DBNull TypeForwardedTo null
C type System.Diagnostics.EventLog
E 020004CB n=3
A0:type=<System.ComponentModel.DefaultEventAttribute>:ctor=<System.ComponentModel.DefaultEventAttribute..ctor>:err=False:F=1:N=0
A0:F0:type=<System.String>:val=str:"EntryWritten"
A1:type=<System.ComponentModel.InstallerTypeAttribute>:ctor=<System.ComponentModel.InstallerTypeAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.String>:val=str:"System.Diagnostics.EventLogInstaller, System.Configuration.Install, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a"
A2:type=<System.Diagnostics.MonitoringDescriptionAttribute>:ctor=<System.Diagnostics.MonitoringDescriptionAttribute..ctor>:err=False:F=1:N=0
A2:F0:type=<System.String>:val=str:"EventLogDesc"
H T:System.Diagnostics.EventLog None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H T:System.Diagnostics.EventLog Serializable=False
H T:System.Diagnostics.EventLog ComImport=False
H T:System.Diagnostics.EventLog SpecialName=False
H T:System.Diagnostics.EventLog StructLayout=False
H T:System.Diagnostics.EventLog Flags=False
H T:System.Diagnostics.EventLog Obsolete=False
H T:System.Diagnostics.EventLog Extension=False
H T:System.Diagnostics.EventLog CompilerGenerated=False
H T:System.Diagnostics.EventLog Nullable=False
H T:System.Diagnostics.EventLog NullableContext=False
H T:System.Diagnostics.EventLog IsReadOnly=False
H T:System.Diagnostics.EventLog IsByRefLike=False
H T:System.Diagnostics.EventLog Dynamic=False
H T:System.Diagnostics.EventLog TupleElementNames=False
H T:System.Diagnostics.EventLog DecimalConstant=False
H T:System.Diagnostics.EventLog DefaultMember=False
H T:System.Diagnostics.EventLog ParamArray=False
H T:System.Diagnostics.EventLog Optional=False
H T:System.Diagnostics.EventLog In=False
H T:System.Diagnostics.EventLog Out=False
H T:System.Diagnostics.EventLog FieldOffset=False
H T:System.Diagnostics.EventLog NonSerialized=False
H T:System.Diagnostics.EventLog MarshalAs=False
H T:System.Diagnostics.EventLog PermissionSet=False
H T:System.Diagnostics.EventLog DefaultParameterValue=False
H T:System.Diagnostics.EventLog AssemblyVersion=False
H T:System.Diagnostics.EventLog InternalsVisibleTo=False
H T:System.Diagnostics.EventLog TypeForwardedTo=False
G T:System.Diagnostics.EventLog None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G T:System.Diagnostics.EventLog Serializable null
G T:System.Diagnostics.EventLog ComImport null
G T:System.Diagnostics.EventLog SpecialName null
G T:System.Diagnostics.EventLog StructLayout null
G T:System.Diagnostics.EventLog Flags null
G T:System.Diagnostics.EventLog Obsolete null
G T:System.Diagnostics.EventLog Extension null
G T:System.Diagnostics.EventLog CompilerGenerated null
G T:System.Diagnostics.EventLog Nullable null
G T:System.Diagnostics.EventLog NullableContext null
G T:System.Diagnostics.EventLog IsReadOnly null
G T:System.Diagnostics.EventLog IsByRefLike null
G T:System.Diagnostics.EventLog Dynamic null
G T:System.Diagnostics.EventLog TupleElementNames null
G T:System.Diagnostics.EventLog DecimalConstant null
G T:System.Diagnostics.EventLog DefaultMember null
G T:System.Diagnostics.EventLog ParamArray null
G T:System.Diagnostics.EventLog Optional null
G T:System.Diagnostics.EventLog In null
G T:System.Diagnostics.EventLog Out null
G T:System.Diagnostics.EventLog FieldOffset null
G T:System.Diagnostics.EventLog NonSerialized null
G T:System.Diagnostics.EventLog MarshalAs null
G T:System.Diagnostics.EventLog PermissionSet null
G T:System.Diagnostics.EventLog DefaultParameterValue null
G T:System.Diagnostics.EventLog AssemblyVersion null
G T:System.Diagnostics.EventLog InternalsVisibleTo null
G T:System.Diagnostics.EventLog TypeForwardedTo null
C type System.Diagnostics.EventLogEntry
E 020004CD n=3
A0:type=<System.SerializableAttribute>:ctor=<<null>>:err=False:F=0:N=0
A1:type=<System.ComponentModel.ToolboxItemAttribute>:ctor=<System.ComponentModel.ToolboxItemAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Boolean>:val=bool:False
A2:type=<System.ComponentModel.DesignTimeVisibleAttribute>:ctor=<System.ComponentModel.DesignTimeVisibleAttribute..ctor>:err=False:F=1:N=0
A2:F0:type=<System.Boolean>:val=bool:False
C type System.Net.WebPermission
E 02000187 n=1
A0:type=<System.SerializableAttribute>:ctor=<<null>>:err=False:F=0:N=0
C field WIN32_FIND_DATA.dwFileAttributes
E 04003051 n=0
C field WIN32_FIND_DATA.ftCreationTime
E 04003052 n=0
C field WIN32_FIND_DATA.ftLastAccessTime
E 04003053 n=0
C field WIN32_FIND_DATA.ftLastWriteTime
E 04003054 n=0
C field WIN32_FIND_DATA.nFileSizeHigh
E 04003055 n=0
C field WIN32_FIND_DATA.nFileSizeLow
E 04003056 n=0
C field WIN32_FIND_DATA.dwReserved0
E 04003057 n=0
C field WIN32_FIND_DATA.dwReserved1
E 04003058 n=0
C field WIN32_FIND_DATA._cFileName
E 04003059 n=1
A0:type=<System.Runtime.CompilerServices.FixedBufferAttribute>:ctor=<System.Runtime.CompilerServices.FixedBufferAttribute..ctor>:err=False:F=2:N=0
A0:F0:type=<System.Type>:val=type:<System.Char>
A0:F1:type=<System.Int32>:val=num:Int32:260
C field WIN32_FIND_DATA._cAlternateFileName
E 0400305A n=1
A0:type=<System.Runtime.CompilerServices.FixedBufferAttribute>:ctor=<System.Runtime.CompilerServices.FixedBufferAttribute..ctor>:err=False:F=2:N=0
A0:F0:type=<System.Type>:val=type:<System.Char>
A0:F1:type=<System.Int32>:val=num:Int32:14
H F:dwFileAttributes None=False
H F:dwFileAttributes Serializable=False
H F:dwFileAttributes ComImport=False
H F:dwFileAttributes SpecialName=False
H F:dwFileAttributes StructLayout=False
H F:dwFileAttributes Flags=False
H F:dwFileAttributes Obsolete=False
H F:dwFileAttributes Extension=False
H F:dwFileAttributes CompilerGenerated=False
H F:dwFileAttributes Nullable=False
H F:dwFileAttributes NullableContext=False
H F:dwFileAttributes IsReadOnly=False
H F:dwFileAttributes IsByRefLike=False
H F:dwFileAttributes Dynamic=False
H F:dwFileAttributes TupleElementNames=False
H F:dwFileAttributes DecimalConstant=False
H F:dwFileAttributes DefaultMember=False
H F:dwFileAttributes ParamArray=False
H F:dwFileAttributes Optional=False
H F:dwFileAttributes In=False
H F:dwFileAttributes Out=False
H F:dwFileAttributes FieldOffset=False
H F:dwFileAttributes NonSerialized=False
H F:dwFileAttributes MarshalAs=False
H F:dwFileAttributes PermissionSet=False
H F:dwFileAttributes DefaultParameterValue=False
H F:dwFileAttributes AssemblyVersion=False
H F:dwFileAttributes InternalsVisibleTo=False
H F:dwFileAttributes TypeForwardedTo=False
G F:dwFileAttributes None null
G F:dwFileAttributes Serializable null
G F:dwFileAttributes ComImport null
G F:dwFileAttributes SpecialName null
G F:dwFileAttributes StructLayout null
G F:dwFileAttributes Flags null
G F:dwFileAttributes Obsolete null
G F:dwFileAttributes Extension null
G F:dwFileAttributes CompilerGenerated null
G F:dwFileAttributes Nullable null
G F:dwFileAttributes NullableContext null
G F:dwFileAttributes IsReadOnly null
G F:dwFileAttributes IsByRefLike null
G F:dwFileAttributes Dynamic null
G F:dwFileAttributes TupleElementNames null
G F:dwFileAttributes DecimalConstant null
G F:dwFileAttributes DefaultMember null
G F:dwFileAttributes ParamArray null
G F:dwFileAttributes Optional null
G F:dwFileAttributes In null
G F:dwFileAttributes Out null
G F:dwFileAttributes FieldOffset null
G F:dwFileAttributes NonSerialized null
G F:dwFileAttributes MarshalAs null
G F:dwFileAttributes PermissionSet null
G F:dwFileAttributes DefaultParameterValue null
G F:dwFileAttributes AssemblyVersion null
G F:dwFileAttributes InternalsVisibleTo null
G F:dwFileAttributes TypeForwardedTo null
C field String.m_firstChar
E 04000284 n=1
A0:type=<System.NonSerializedAttribute>:ctor=<System.NonSerializedAttribute..ctor>:err=False:F=0:N=0
H F:String.m_firstChar None=False
H F:String.m_firstChar Serializable=False
H F:String.m_firstChar ComImport=False
H F:String.m_firstChar SpecialName=False
H F:String.m_firstChar StructLayout=False
H F:String.m_firstChar Flags=False
H F:String.m_firstChar Obsolete=False
H F:String.m_firstChar Extension=False
H F:String.m_firstChar CompilerGenerated=False
H F:String.m_firstChar Nullable=False
H F:String.m_firstChar NullableContext=False
H F:String.m_firstChar IsReadOnly=False
H F:String.m_firstChar IsByRefLike=False
H F:String.m_firstChar Dynamic=False
H F:String.m_firstChar TupleElementNames=False
H F:String.m_firstChar DecimalConstant=False
H F:String.m_firstChar DefaultMember=False
H F:String.m_firstChar ParamArray=False
H F:String.m_firstChar Optional=False
H F:String.m_firstChar In=False
H F:String.m_firstChar Out=False
H F:String.m_firstChar FieldOffset=False
H F:String.m_firstChar NonSerialized=True
H F:String.m_firstChar MarshalAs=False
H F:String.m_firstChar PermissionSet=False
H F:String.m_firstChar DefaultParameterValue=False
H F:String.m_firstChar AssemblyVersion=False
H F:String.m_firstChar InternalsVisibleTo=False
H F:String.m_firstChar TypeForwardedTo=False
G F:String.m_firstChar None null
G F:String.m_firstChar Serializable null
G F:String.m_firstChar ComImport null
G F:String.m_firstChar SpecialName null
G F:String.m_firstChar StructLayout null
G F:String.m_firstChar Flags null
G F:String.m_firstChar Obsolete null
G F:String.m_firstChar Extension null
G F:String.m_firstChar CompilerGenerated null
G F:String.m_firstChar Nullable null
G F:String.m_firstChar NullableContext null
G F:String.m_firstChar IsReadOnly null
G F:String.m_firstChar IsByRefLike null
G F:String.m_firstChar Dynamic null
G F:String.m_firstChar TupleElementNames null
G F:String.m_firstChar DecimalConstant null
G F:String.m_firstChar DefaultMember null
G F:String.m_firstChar ParamArray null
G F:String.m_firstChar Optional null
G F:String.m_firstChar In null
G F:String.m_firstChar Out null
G F:String.m_firstChar FieldOffset null
G F:String.m_firstChar NonSerialized found
GA:type=<System.NonSerializedAttribute>:ctor=<System.NonSerializedAttribute..ctor>:err=False:F=0:N=0
G F:String.m_firstChar MarshalAs null
G F:String.m_firstChar PermissionSet null
G F:String.m_firstChar DefaultParameterValue null
G F:String.m_firstChar AssemblyVersion null
G F:String.m_firstChar InternalsVisibleTo null
G F:String.m_firstChar TypeForwardedTo null
C field String.m_stringLength
E 04000283 n=1
A0:type=<System.NonSerializedAttribute>:ctor=<System.NonSerializedAttribute..ctor>:err=False:F=0:N=0
H F:String.m_stringLength None=False
H F:String.m_stringLength Serializable=False
H F:String.m_stringLength ComImport=False
H F:String.m_stringLength SpecialName=False
H F:String.m_stringLength StructLayout=False
H F:String.m_stringLength Flags=False
H F:String.m_stringLength Obsolete=False
H F:String.m_stringLength Extension=False
H F:String.m_stringLength CompilerGenerated=False
H F:String.m_stringLength Nullable=False
H F:String.m_stringLength NullableContext=False
H F:String.m_stringLength IsReadOnly=False
H F:String.m_stringLength IsByRefLike=False
H F:String.m_stringLength Dynamic=False
H F:String.m_stringLength TupleElementNames=False
H F:String.m_stringLength DecimalConstant=False
H F:String.m_stringLength DefaultMember=False
H F:String.m_stringLength ParamArray=False
H F:String.m_stringLength Optional=False
H F:String.m_stringLength In=False
H F:String.m_stringLength Out=False
H F:String.m_stringLength FieldOffset=False
H F:String.m_stringLength NonSerialized=True
H F:String.m_stringLength MarshalAs=False
H F:String.m_stringLength PermissionSet=False
H F:String.m_stringLength DefaultParameterValue=False
H F:String.m_stringLength AssemblyVersion=False
H F:String.m_stringLength InternalsVisibleTo=False
H F:String.m_stringLength TypeForwardedTo=False
G F:String.m_stringLength None null
G F:String.m_stringLength Serializable null
G F:String.m_stringLength ComImport null
G F:String.m_stringLength SpecialName null
G F:String.m_stringLength StructLayout null
G F:String.m_stringLength Flags null
G F:String.m_stringLength Obsolete null
G F:String.m_stringLength Extension null
G F:String.m_stringLength CompilerGenerated null
G F:String.m_stringLength Nullable null
G F:String.m_stringLength NullableContext null
G F:String.m_stringLength IsReadOnly null
G F:String.m_stringLength IsByRefLike null
G F:String.m_stringLength Dynamic null
G F:String.m_stringLength TupleElementNames null
G F:String.m_stringLength DecimalConstant null
G F:String.m_stringLength DefaultMember null
G F:String.m_stringLength ParamArray null
G F:String.m_stringLength Optional null
G F:String.m_stringLength In null
G F:String.m_stringLength Out null
G F:String.m_stringLength FieldOffset null
G F:String.m_stringLength NonSerialized found
GA:type=<System.NonSerializedAttribute>:ctor=<System.NonSerializedAttribute..ctor>:err=False:F=0:N=0
G F:String.m_stringLength MarshalAs null
G F:String.m_stringLength PermissionSet null
G F:String.m_stringLength DefaultParameterValue null
G F:String.m_stringLength AssemblyVersion null
G F:String.m_stringLength InternalsVisibleTo null
G F:String.m_stringLength TypeForwardedTo null
C param System.String.Concat parg0
E 06000550:0 n=0
H P:System.String.Concat.arg0 None=False
H P:System.String.Concat.arg0 Serializable=False
H P:System.String.Concat.arg0 ComImport=False
H P:System.String.Concat.arg0 SpecialName=False
H P:System.String.Concat.arg0 StructLayout=False
H P:System.String.Concat.arg0 Flags=False
H P:System.String.Concat.arg0 Obsolete=False
H P:System.String.Concat.arg0 Extension=False
H P:System.String.Concat.arg0 CompilerGenerated=False
H P:System.String.Concat.arg0 Nullable=False
H P:System.String.Concat.arg0 NullableContext=False
H P:System.String.Concat.arg0 IsReadOnly=False
H P:System.String.Concat.arg0 IsByRefLike=False
H P:System.String.Concat.arg0 Dynamic=False
H P:System.String.Concat.arg0 TupleElementNames=False
H P:System.String.Concat.arg0 DecimalConstant=False
H P:System.String.Concat.arg0 DefaultMember=False
H P:System.String.Concat.arg0 ParamArray=False
H P:System.String.Concat.arg0 Optional=False
H P:System.String.Concat.arg0 In=False
H P:System.String.Concat.arg0 Out=False
H P:System.String.Concat.arg0 FieldOffset=False
H P:System.String.Concat.arg0 NonSerialized=False
H P:System.String.Concat.arg0 MarshalAs=False
H P:System.String.Concat.arg0 PermissionSet=False
H P:System.String.Concat.arg0 DefaultParameterValue=False
H P:System.String.Concat.arg0 AssemblyVersion=False
H P:System.String.Concat.arg0 InternalsVisibleTo=False
H P:System.String.Concat.arg0 TypeForwardedTo=False
G P:System.String.Concat.arg0 None null
G P:System.String.Concat.arg0 Serializable null
G P:System.String.Concat.arg0 ComImport null
G P:System.String.Concat.arg0 SpecialName null
G P:System.String.Concat.arg0 StructLayout null
G P:System.String.Concat.arg0 Flags null
G P:System.String.Concat.arg0 Obsolete null
G P:System.String.Concat.arg0 Extension null
G P:System.String.Concat.arg0 CompilerGenerated null
G P:System.String.Concat.arg0 Nullable null
G P:System.String.Concat.arg0 NullableContext null
G P:System.String.Concat.arg0 IsReadOnly null
G P:System.String.Concat.arg0 IsByRefLike null
G P:System.String.Concat.arg0 Dynamic null
G P:System.String.Concat.arg0 TupleElementNames null
G P:System.String.Concat.arg0 DecimalConstant null
G P:System.String.Concat.arg0 DefaultMember null
G P:System.String.Concat.arg0 ParamArray null
G P:System.String.Concat.arg0 Optional null
G P:System.String.Concat.arg0 In null
G P:System.String.Concat.arg0 Out null
G P:System.String.Concat.arg0 FieldOffset null
G P:System.String.Concat.arg0 NonSerialized null
G P:System.String.Concat.arg0 MarshalAs null
G P:System.String.Concat.arg0 PermissionSet null
G P:System.String.Concat.arg0 DefaultParameterValue null
G P:System.String.Concat.arg0 AssemblyVersion null
G P:System.String.Concat.arg0 InternalsVisibleTo null
G P:System.String.Concat.arg0 TypeForwardedTo null
C param System.String.Copy pstr
E 0600054F:0 n=0
H P:System.String.Copy.str None=False
H P:System.String.Copy.str Serializable=False
H P:System.String.Copy.str ComImport=False
H P:System.String.Copy.str SpecialName=False
H P:System.String.Copy.str StructLayout=False
H P:System.String.Copy.str Flags=False
H P:System.String.Copy.str Obsolete=False
H P:System.String.Copy.str Extension=False
H P:System.String.Copy.str CompilerGenerated=False
H P:System.String.Copy.str Nullable=False
H P:System.String.Copy.str NullableContext=False
H P:System.String.Copy.str IsReadOnly=False
H P:System.String.Copy.str IsByRefLike=False
H P:System.String.Copy.str Dynamic=False
H P:System.String.Copy.str TupleElementNames=False
H P:System.String.Copy.str DecimalConstant=False
H P:System.String.Copy.str DefaultMember=False
H P:System.String.Copy.str ParamArray=False
H P:System.String.Copy.str Optional=False
H P:System.String.Copy.str In=False
H P:System.String.Copy.str Out=False
H P:System.String.Copy.str FieldOffset=False
H P:System.String.Copy.str NonSerialized=False
H P:System.String.Copy.str MarshalAs=False
H P:System.String.Copy.str PermissionSet=False
H P:System.String.Copy.str DefaultParameterValue=False
H P:System.String.Copy.str AssemblyVersion=False
H P:System.String.Copy.str InternalsVisibleTo=False
H P:System.String.Copy.str TypeForwardedTo=False
G P:System.String.Copy.str None null
G P:System.String.Copy.str Serializable null
G P:System.String.Copy.str ComImport null
G P:System.String.Copy.str SpecialName null
G P:System.String.Copy.str StructLayout null
G P:System.String.Copy.str Flags null
G P:System.String.Copy.str Obsolete null
G P:System.String.Copy.str Extension null
G P:System.String.Copy.str CompilerGenerated null
G P:System.String.Copy.str Nullable null
G P:System.String.Copy.str NullableContext null
G P:System.String.Copy.str IsReadOnly null
G P:System.String.Copy.str IsByRefLike null
G P:System.String.Copy.str Dynamic null
G P:System.String.Copy.str TupleElementNames null
G P:System.String.Copy.str DecimalConstant null
G P:System.String.Copy.str DefaultMember null
G P:System.String.Copy.str ParamArray null
G P:System.String.Copy.str Optional null
G P:System.String.Copy.str In null
G P:System.String.Copy.str Out null
G P:System.String.Copy.str FieldOffset null
G P:System.String.Copy.str NonSerialized null
G P:System.String.Copy.str MarshalAs null
G P:System.String.Copy.str PermissionSet null
G P:System.String.Copy.str DefaultParameterValue null
G P:System.String.Copy.str AssemblyVersion null
G P:System.String.Copy.str InternalsVisibleTo null
G P:System.String.Copy.str TypeForwardedTo null
)gold";

inline constexpr const char* kGoldOptions =
R"gold(
O type System.String
E 020000C9 n=6
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Runtime.CompilerServices.NullableContextAttribute>:ctor=<System.Runtime.CompilerServices.NullableContextAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Byte>:val=num:Byte:1
A2:type=<System.Runtime.CompilerServices.NullableAttribute>:ctor=<System.Runtime.CompilerServices.NullableAttribute..ctor>:err=False:F=1:N=0
A2:F0:type=<System.Byte>:val=num:Byte:0
A3:type=<System.Reflection.DefaultMemberAttribute>:ctor=<System.Reflection.DefaultMemberAttribute..ctor>:err=False:F=1:N=0
A3:F0:type=<System.String>:val=str:"Chars"
A4:type=<System.Runtime.Versioning.NonVersionableAttribute>:ctor=<System.Runtime.Versioning.NonVersionableAttribute..ctor>:err=False:F=0:N=0
A5:type=<System.Runtime.CompilerServices.TypeForwardedFromAttribute>:ctor=<System.Runtime.CompilerServices.TypeForwardedFromAttribute..ctor>:err=False:F=1:N=0
A5:F0:type=<System.String>:val=str:"mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089"
H O:System.String None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H O:System.String Serializable=True
H O:System.String ComImport=False
H O:System.String SpecialName=False
H O:System.String StructLayout=False
H O:System.String Flags=False
H O:System.String Obsolete=False
H O:System.String Extension=False
H O:System.String CompilerGenerated=False
H O:System.String Nullable=True
H O:System.String NullableContext=True
H O:System.String IsReadOnly=False
H O:System.String IsByRefLike=False
H O:System.String Dynamic=False
H O:System.String TupleElementNames=False
H O:System.String DecimalConstant=False
H O:System.String DefaultMember=True
H O:System.String ParamArray=False
H O:System.String Optional=False
H O:System.String In=False
H O:System.String Out=False
H O:System.String FieldOffset=False
H O:System.String NonSerialized=False
H O:System.String MarshalAs=False
H O:System.String PermissionSet=False
H O:System.String DefaultParameterValue=False
H O:System.String AssemblyVersion=False
H O:System.String InternalsVisibleTo=False
H O:System.String TypeForwardedTo=False
G O:System.String None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G O:System.String Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G O:System.String ComImport null
G O:System.String SpecialName null
G O:System.String StructLayout null
G O:System.String Flags null
G O:System.String Obsolete null
G O:System.String Extension null
G O:System.String CompilerGenerated null
G O:System.String Nullable found
GA:type=<System.Runtime.CompilerServices.NullableAttribute>:ctor=<System.Runtime.CompilerServices.NullableAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.Byte>:val=num:Byte:0
G O:System.String NullableContext found
GA:type=<System.Runtime.CompilerServices.NullableContextAttribute>:ctor=<System.Runtime.CompilerServices.NullableContextAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.Byte>:val=num:Byte:1
G O:System.String IsReadOnly null
G O:System.String IsByRefLike null
G O:System.String Dynamic null
G O:System.String TupleElementNames null
G O:System.String DecimalConstant null
G O:System.String DefaultMember found
GA:type=<System.Reflection.DefaultMemberAttribute>:ctor=<System.Reflection.DefaultMemberAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.String>:val=str:"Chars"
G O:System.String ParamArray null
G O:System.String Optional null
G O:System.String In null
G O:System.String Out null
G O:System.String FieldOffset null
G O:System.String NonSerialized null
G O:System.String MarshalAs null
G O:System.String PermissionSet null
G O:System.String DefaultParameterValue null
G O:System.String AssemblyVersion null
G O:System.String InternalsVisibleTo null
G O:System.String TypeForwardedTo null
O type System.Span`1
E 020001C7 n=12
A0:type=<System.Runtime.CompilerServices.NullableContextAttribute>:ctor=<System.Runtime.CompilerServices.NullableContextAttribute..ctor>:err=False:F=1:N=0
A0:F0:type=<System.Byte>:val=num:Byte:1
A1:type=<System.Runtime.CompilerServices.NullableAttribute>:ctor=<System.Runtime.CompilerServices.NullableAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Byte>:val=num:Byte:0
A2:type=<System.Runtime.CompilerServices.IsByRefLikeAttribute>:ctor=<System.Runtime.CompilerServices.IsByRefLikeAttribute..ctor>:err=False:F=0:N=0
A3:type=<System.ObsoleteAttribute>:ctor=<System.ObsoleteAttribute..ctor>:err=False:F=2:N=0
A3:F0:type=<System.String>:val=str:"Types with embedded references are not supported in this version of your compiler."
A3:F1:type=<System.Boolean>:val=bool:True
A4:type=<System.Runtime.CompilerServices.CompilerFeatureRequiredAttribute>:ctor=<System.Runtime.CompilerServices.CompilerFeatureRequiredAttribute..ctor>:err=False:F=1:N=0
A4:F0:type=<System.String>:val=str:"RefStructs"
A5:type=<System.Runtime.CompilerServices.IsReadOnlyAttribute>:ctor=<System.Runtime.CompilerServices.IsReadOnlyAttribute..ctor>:err=False:F=0:N=0
A6:type=<System.Reflection.DefaultMemberAttribute>:ctor=<System.Reflection.DefaultMemberAttribute..ctor>:err=False:F=1:N=0
A6:F0:type=<System.String>:val=str:"Item"
A7:type=<System.Diagnostics.DebuggerTypeProxyAttribute>:ctor=<System.Diagnostics.DebuggerTypeProxyAttribute..ctor>:err=False:F=1:N=0
A7:F0:type=<System.Type>:val=type:<System.SpanDebugView`1>
A8:type=<System.Diagnostics.DebuggerDisplayAttribute>:ctor=<System.Diagnostics.DebuggerDisplayAttribute..ctor>:err=False:F=1:N=0
A8:F0:type=<System.String>:val=str:"{ToString(),raw}"
A9:type=<System.Runtime.Versioning.NonVersionableAttribute>:ctor=<System.Runtime.Versioning.NonVersionableAttribute..ctor>:err=False:F=0:N=0
A10:type=<System.Runtime.InteropServices.Marshalling.NativeMarshallingAttribute>:ctor=<System.Runtime.InteropServices.Marshalling.NativeMarshallingAttribute..ctor>:err=False:F=1:N=0
A10:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices.Marshalling.SpanMarshaller`2>
A11:type=<System.Runtime.CompilerServices.IntrinsicAttribute>:ctor=<System.Runtime.CompilerServices.IntrinsicAttribute..ctor>:err=False:F=0:N=0
H O:System.Span`1 None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H O:System.Span`1 Serializable=False
H O:System.Span`1 ComImport=False
H O:System.Span`1 SpecialName=False
H O:System.Span`1 StructLayout=False
H O:System.Span`1 Flags=False
H O:System.Span`1 Obsolete=True
H O:System.Span`1 Extension=False
H O:System.Span`1 CompilerGenerated=False
H O:System.Span`1 Nullable=True
H O:System.Span`1 NullableContext=True
H O:System.Span`1 IsReadOnly=True
H O:System.Span`1 IsByRefLike=True
H O:System.Span`1 Dynamic=False
H O:System.Span`1 TupleElementNames=False
H O:System.Span`1 DecimalConstant=False
H O:System.Span`1 DefaultMember=True
H O:System.Span`1 ParamArray=False
H O:System.Span`1 Optional=False
H O:System.Span`1 In=False
H O:System.Span`1 Out=False
H O:System.Span`1 FieldOffset=False
H O:System.Span`1 NonSerialized=False
H O:System.Span`1 MarshalAs=False
H O:System.Span`1 PermissionSet=False
H O:System.Span`1 DefaultParameterValue=False
H O:System.Span`1 AssemblyVersion=False
H O:System.Span`1 InternalsVisibleTo=False
H O:System.Span`1 TypeForwardedTo=False
G O:System.Span`1 None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G O:System.Span`1 Serializable null
G O:System.Span`1 ComImport null
G O:System.Span`1 SpecialName null
G O:System.Span`1 StructLayout null
G O:System.Span`1 Flags null
G O:System.Span`1 Obsolete found
GA:type=<System.ObsoleteAttribute>:ctor=<System.ObsoleteAttribute..ctor>:err=False:F=2:N=0
GA:F0:type=<System.String>:val=str:"Types with embedded references are not supported in this version of your compiler."
GA:F1:type=<System.Boolean>:val=bool:True
G O:System.Span`1 Extension null
G O:System.Span`1 CompilerGenerated null
G O:System.Span`1 Nullable found
GA:type=<System.Runtime.CompilerServices.NullableAttribute>:ctor=<System.Runtime.CompilerServices.NullableAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.Byte>:val=num:Byte:0
G O:System.Span`1 NullableContext found
GA:type=<System.Runtime.CompilerServices.NullableContextAttribute>:ctor=<System.Runtime.CompilerServices.NullableContextAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.Byte>:val=num:Byte:1
G O:System.Span`1 IsReadOnly found
GA:type=<System.Runtime.CompilerServices.IsReadOnlyAttribute>:ctor=<System.Runtime.CompilerServices.IsReadOnlyAttribute..ctor>:err=False:F=0:N=0
G O:System.Span`1 IsByRefLike found
GA:type=<System.Runtime.CompilerServices.IsByRefLikeAttribute>:ctor=<System.Runtime.CompilerServices.IsByRefLikeAttribute..ctor>:err=False:F=0:N=0
G O:System.Span`1 Dynamic null
G O:System.Span`1 TupleElementNames null
G O:System.Span`1 DecimalConstant null
G O:System.Span`1 DefaultMember found
GA:type=<System.Reflection.DefaultMemberAttribute>:ctor=<System.Reflection.DefaultMemberAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.String>:val=str:"Item"
G O:System.Span`1 ParamArray null
G O:System.Span`1 Optional null
G O:System.Span`1 In null
G O:System.Span`1 Out null
G O:System.Span`1 FieldOffset null
G O:System.Span`1 NonSerialized null
G O:System.Span`1 MarshalAs null
G O:System.Span`1 PermissionSet null
G O:System.Span`1 DefaultParameterValue null
G O:System.Span`1 AssemblyVersion null
G O:System.Span`1 InternalsVisibleTo null
G O:System.Span`1 TypeForwardedTo null
O type System.ReadOnlySpan`1
E 020001C0 n=12
A0:type=<System.Runtime.CompilerServices.NullableContextAttribute>:ctor=<System.Runtime.CompilerServices.NullableContextAttribute..ctor>:err=False:F=1:N=0
A0:F0:type=<System.Byte>:val=num:Byte:1
A1:type=<System.Runtime.CompilerServices.NullableAttribute>:ctor=<System.Runtime.CompilerServices.NullableAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Byte>:val=num:Byte:0
A2:type=<System.Runtime.CompilerServices.IsByRefLikeAttribute>:ctor=<System.Runtime.CompilerServices.IsByRefLikeAttribute..ctor>:err=False:F=0:N=0
A3:type=<System.ObsoleteAttribute>:ctor=<System.ObsoleteAttribute..ctor>:err=False:F=2:N=0
A3:F0:type=<System.String>:val=str:"Types with embedded references are not supported in this version of your compiler."
A3:F1:type=<System.Boolean>:val=bool:True
A4:type=<System.Runtime.CompilerServices.CompilerFeatureRequiredAttribute>:ctor=<System.Runtime.CompilerServices.CompilerFeatureRequiredAttribute..ctor>:err=False:F=1:N=0
A4:F0:type=<System.String>:val=str:"RefStructs"
A5:type=<System.Runtime.CompilerServices.IsReadOnlyAttribute>:ctor=<System.Runtime.CompilerServices.IsReadOnlyAttribute..ctor>:err=False:F=0:N=0
A6:type=<System.Reflection.DefaultMemberAttribute>:ctor=<System.Reflection.DefaultMemberAttribute..ctor>:err=False:F=1:N=0
A6:F0:type=<System.String>:val=str:"Item"
A7:type=<System.Diagnostics.DebuggerTypeProxyAttribute>:ctor=<System.Diagnostics.DebuggerTypeProxyAttribute..ctor>:err=False:F=1:N=0
A7:F0:type=<System.Type>:val=type:<System.SpanDebugView`1>
A8:type=<System.Diagnostics.DebuggerDisplayAttribute>:ctor=<System.Diagnostics.DebuggerDisplayAttribute..ctor>:err=False:F=1:N=0
A8:F0:type=<System.String>:val=str:"{ToString(),raw}"
A9:type=<System.Runtime.Versioning.NonVersionableAttribute>:ctor=<System.Runtime.Versioning.NonVersionableAttribute..ctor>:err=False:F=0:N=0
A10:type=<System.Runtime.InteropServices.Marshalling.NativeMarshallingAttribute>:ctor=<System.Runtime.InteropServices.Marshalling.NativeMarshallingAttribute..ctor>:err=False:F=1:N=0
A10:F0:type=<System.Type>:val=type:<System.Runtime.InteropServices.Marshalling.ReadOnlySpanMarshaller`2>
A11:type=<System.Runtime.CompilerServices.IntrinsicAttribute>:ctor=<System.Runtime.CompilerServices.IntrinsicAttribute..ctor>:err=False:F=0:N=0
H O:System.ReadOnlySpan`1 None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H O:System.ReadOnlySpan`1 Serializable=False
H O:System.ReadOnlySpan`1 ComImport=False
H O:System.ReadOnlySpan`1 SpecialName=False
H O:System.ReadOnlySpan`1 StructLayout=False
H O:System.ReadOnlySpan`1 Flags=False
H O:System.ReadOnlySpan`1 Obsolete=True
H O:System.ReadOnlySpan`1 Extension=False
H O:System.ReadOnlySpan`1 CompilerGenerated=False
H O:System.ReadOnlySpan`1 Nullable=True
H O:System.ReadOnlySpan`1 NullableContext=True
H O:System.ReadOnlySpan`1 IsReadOnly=True
H O:System.ReadOnlySpan`1 IsByRefLike=True
H O:System.ReadOnlySpan`1 Dynamic=False
H O:System.ReadOnlySpan`1 TupleElementNames=False
H O:System.ReadOnlySpan`1 DecimalConstant=False
H O:System.ReadOnlySpan`1 DefaultMember=True
H O:System.ReadOnlySpan`1 ParamArray=False
H O:System.ReadOnlySpan`1 Optional=False
H O:System.ReadOnlySpan`1 In=False
H O:System.ReadOnlySpan`1 Out=False
H O:System.ReadOnlySpan`1 FieldOffset=False
H O:System.ReadOnlySpan`1 NonSerialized=False
H O:System.ReadOnlySpan`1 MarshalAs=False
H O:System.ReadOnlySpan`1 PermissionSet=False
H O:System.ReadOnlySpan`1 DefaultParameterValue=False
H O:System.ReadOnlySpan`1 AssemblyVersion=False
H O:System.ReadOnlySpan`1 InternalsVisibleTo=False
H O:System.ReadOnlySpan`1 TypeForwardedTo=False
G O:System.ReadOnlySpan`1 None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G O:System.ReadOnlySpan`1 Serializable null
G O:System.ReadOnlySpan`1 ComImport null
G O:System.ReadOnlySpan`1 SpecialName null
G O:System.ReadOnlySpan`1 StructLayout null
G O:System.ReadOnlySpan`1 Flags null
G O:System.ReadOnlySpan`1 Obsolete found
GA:type=<System.ObsoleteAttribute>:ctor=<System.ObsoleteAttribute..ctor>:err=False:F=2:N=0
GA:F0:type=<System.String>:val=str:"Types with embedded references are not supported in this version of your compiler."
GA:F1:type=<System.Boolean>:val=bool:True
G O:System.ReadOnlySpan`1 Extension null
G O:System.ReadOnlySpan`1 CompilerGenerated null
G O:System.ReadOnlySpan`1 Nullable found
GA:type=<System.Runtime.CompilerServices.NullableAttribute>:ctor=<System.Runtime.CompilerServices.NullableAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.Byte>:val=num:Byte:0
G O:System.ReadOnlySpan`1 NullableContext found
GA:type=<System.Runtime.CompilerServices.NullableContextAttribute>:ctor=<System.Runtime.CompilerServices.NullableContextAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.Byte>:val=num:Byte:1
G O:System.ReadOnlySpan`1 IsReadOnly found
GA:type=<System.Runtime.CompilerServices.IsReadOnlyAttribute>:ctor=<System.Runtime.CompilerServices.IsReadOnlyAttribute..ctor>:err=False:F=0:N=0
G O:System.ReadOnlySpan`1 IsByRefLike found
GA:type=<System.Runtime.CompilerServices.IsByRefLikeAttribute>:ctor=<System.Runtime.CompilerServices.IsByRefLikeAttribute..ctor>:err=False:F=0:N=0
G O:System.ReadOnlySpan`1 Dynamic null
G O:System.ReadOnlySpan`1 TupleElementNames null
G O:System.ReadOnlySpan`1 DecimalConstant null
G O:System.ReadOnlySpan`1 DefaultMember found
GA:type=<System.Reflection.DefaultMemberAttribute>:ctor=<System.Reflection.DefaultMemberAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.String>:val=str:"Item"
G O:System.ReadOnlySpan`1 ParamArray null
G O:System.ReadOnlySpan`1 Optional null
G O:System.ReadOnlySpan`1 In null
G O:System.ReadOnlySpan`1 Out null
G O:System.ReadOnlySpan`1 FieldOffset null
G O:System.ReadOnlySpan`1 NonSerialized null
G O:System.ReadOnlySpan`1 MarshalAs null
G O:System.ReadOnlySpan`1 PermissionSet null
G O:System.ReadOnlySpan`1 DefaultParameterValue null
G O:System.ReadOnlySpan`1 AssemblyVersion null
G O:System.ReadOnlySpan`1 InternalsVisibleTo null
G O:System.ReadOnlySpan`1 TypeForwardedTo null
O type System.Object
E 020000A5 n=5
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Runtime.CompilerServices.NullableContextAttribute>:ctor=<System.Runtime.CompilerServices.NullableContextAttribute..ctor>:err=False:F=1:N=0
A1:F0:type=<System.Byte>:val=num:Byte:2
A2:type=<System.Runtime.InteropServices.ClassInterfaceAttribute>:ctor=<System.Runtime.InteropServices.ClassInterfaceAttribute..ctor>:err=False:F=1:N=0
A2:F0:type=<System.Runtime.InteropServices.ClassInterfaceType>:val=num:Int32:1
A3:type=<System.Runtime.InteropServices.ComVisibleAttribute>:ctor=<System.Runtime.InteropServices.ComVisibleAttribute..ctor>:err=False:F=1:N=0
A3:F0:type=<System.Boolean>:val=bool:True
A4:type=<System.Runtime.CompilerServices.TypeForwardedFromAttribute>:ctor=<System.Runtime.CompilerServices.TypeForwardedFromAttribute..ctor>:err=False:F=1:N=0
A4:F0:type=<System.String>:val=str:"mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089"
H O:System.Object None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H O:System.Object Serializable=True
H O:System.Object ComImport=False
H O:System.Object SpecialName=False
H O:System.Object StructLayout=False
H O:System.Object Flags=False
H O:System.Object Obsolete=False
H O:System.Object Extension=False
H O:System.Object CompilerGenerated=False
H O:System.Object Nullable=False
H O:System.Object NullableContext=True
H O:System.Object IsReadOnly=False
H O:System.Object IsByRefLike=False
H O:System.Object Dynamic=False
H O:System.Object TupleElementNames=False
H O:System.Object DecimalConstant=False
H O:System.Object DefaultMember=False
H O:System.Object ParamArray=False
H O:System.Object Optional=False
H O:System.Object In=False
H O:System.Object Out=False
H O:System.Object FieldOffset=False
H O:System.Object NonSerialized=False
H O:System.Object MarshalAs=False
H O:System.Object PermissionSet=False
H O:System.Object DefaultParameterValue=False
H O:System.Object AssemblyVersion=False
H O:System.Object InternalsVisibleTo=False
H O:System.Object TypeForwardedTo=False
G O:System.Object None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G O:System.Object Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G O:System.Object ComImport null
G O:System.Object SpecialName null
G O:System.Object StructLayout null
G O:System.Object Flags null
G O:System.Object Obsolete null
G O:System.Object Extension null
G O:System.Object CompilerGenerated null
G O:System.Object Nullable null
G O:System.Object NullableContext found
GA:type=<System.Runtime.CompilerServices.NullableContextAttribute>:ctor=<System.Runtime.CompilerServices.NullableContextAttribute..ctor>:err=False:F=1:N=0
GA:F0:type=<System.Byte>:val=num:Byte:2
G O:System.Object IsReadOnly null
G O:System.Object IsByRefLike null
G O:System.Object Dynamic null
G O:System.Object TupleElementNames null
G O:System.Object DecimalConstant null
G O:System.Object DefaultMember null
G O:System.Object ParamArray null
G O:System.Object Optional null
G O:System.Object In null
G O:System.Object Out null
G O:System.Object FieldOffset null
G O:System.Object NonSerialized null
G O:System.Object MarshalAs null
G O:System.Object PermissionSet null
G O:System.Object DefaultParameterValue null
G O:System.Object AssemblyVersion null
G O:System.Object InternalsVisibleTo null
G O:System.Object TypeForwardedTo null
O type System.Decimal
E 02000081 n=4
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Runtime.CompilerServices.IsReadOnlyAttribute>:ctor=<System.Runtime.CompilerServices.IsReadOnlyAttribute..ctor>:err=False:F=0:N=0
A2:type=<System.Runtime.Versioning.NonVersionableAttribute>:ctor=<System.Runtime.Versioning.NonVersionableAttribute..ctor>:err=False:F=0:N=0
A3:type=<System.Runtime.CompilerServices.TypeForwardedFromAttribute>:ctor=<System.Runtime.CompilerServices.TypeForwardedFromAttribute..ctor>:err=False:F=1:N=0
A3:F0:type=<System.String>:val=str:"mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089"
H O:System.Decimal None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H O:System.Decimal Serializable=True
H O:System.Decimal ComImport=False
H O:System.Decimal SpecialName=False
H O:System.Decimal StructLayout=False
H O:System.Decimal Flags=False
H O:System.Decimal Obsolete=False
H O:System.Decimal Extension=False
H O:System.Decimal CompilerGenerated=False
H O:System.Decimal Nullable=False
H O:System.Decimal NullableContext=False
H O:System.Decimal IsReadOnly=True
H O:System.Decimal IsByRefLike=False
H O:System.Decimal Dynamic=False
H O:System.Decimal TupleElementNames=False
H O:System.Decimal DecimalConstant=False
H O:System.Decimal DefaultMember=False
H O:System.Decimal ParamArray=False
H O:System.Decimal Optional=False
H O:System.Decimal In=False
H O:System.Decimal Out=False
H O:System.Decimal FieldOffset=False
H O:System.Decimal NonSerialized=False
H O:System.Decimal MarshalAs=False
H O:System.Decimal PermissionSet=False
H O:System.Decimal DefaultParameterValue=False
H O:System.Decimal AssemblyVersion=False
H O:System.Decimal InternalsVisibleTo=False
H O:System.Decimal TypeForwardedTo=False
G O:System.Decimal None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G O:System.Decimal Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G O:System.Decimal ComImport null
G O:System.Decimal SpecialName null
G O:System.Decimal StructLayout null
G O:System.Decimal Flags null
G O:System.Decimal Obsolete null
G O:System.Decimal Extension null
G O:System.Decimal CompilerGenerated null
G O:System.Decimal Nullable null
G O:System.Decimal NullableContext null
G O:System.Decimal IsReadOnly found
GA:type=<System.Runtime.CompilerServices.IsReadOnlyAttribute>:ctor=<System.Runtime.CompilerServices.IsReadOnlyAttribute..ctor>:err=False:F=0:N=0
G O:System.Decimal IsByRefLike null
G O:System.Decimal Dynamic null
G O:System.Decimal TupleElementNames null
G O:System.Decimal DecimalConstant null
G O:System.Decimal DefaultMember null
G O:System.Decimal ParamArray null
G O:System.Decimal Optional null
G O:System.Decimal In null
G O:System.Decimal Out null
G O:System.Decimal FieldOffset null
G O:System.Decimal NonSerialized null
G O:System.Decimal MarshalAs null
G O:System.Decimal PermissionSet null
G O:System.Decimal DefaultParameterValue null
G O:System.Decimal AssemblyVersion null
G O:System.Decimal InternalsVisibleTo null
G O:System.Decimal TypeForwardedTo null
O type System.IntPtr
E 02000171 n=3
A0:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
A1:type=<System.Runtime.CompilerServices.IsReadOnlyAttribute>:ctor=<System.Runtime.CompilerServices.IsReadOnlyAttribute..ctor>:err=False:F=0:N=0
A2:type=<System.Runtime.CompilerServices.TypeForwardedFromAttribute>:ctor=<System.Runtime.CompilerServices.TypeForwardedFromAttribute..ctor>:err=False:F=1:N=0
A2:F0:type=<System.String>:val=str:"mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089"
H O:System.IntPtr None=EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
H O:System.IntPtr Serializable=True
H O:System.IntPtr ComImport=False
H O:System.IntPtr SpecialName=False
H O:System.IntPtr StructLayout=False
H O:System.IntPtr Flags=False
H O:System.IntPtr Obsolete=False
H O:System.IntPtr Extension=False
H O:System.IntPtr CompilerGenerated=False
H O:System.IntPtr Nullable=False
H O:System.IntPtr NullableContext=False
H O:System.IntPtr IsReadOnly=True
H O:System.IntPtr IsByRefLike=False
H O:System.IntPtr Dynamic=False
H O:System.IntPtr TupleElementNames=False
H O:System.IntPtr DecimalConstant=False
H O:System.IntPtr DefaultMember=False
H O:System.IntPtr ParamArray=False
H O:System.IntPtr Optional=False
H O:System.IntPtr In=False
H O:System.IntPtr Out=False
H O:System.IntPtr FieldOffset=False
H O:System.IntPtr NonSerialized=False
H O:System.IntPtr MarshalAs=False
H O:System.IntPtr PermissionSet=False
H O:System.IntPtr DefaultParameterValue=False
H O:System.IntPtr AssemblyVersion=False
H O:System.IntPtr InternalsVisibleTo=False
H O:System.IntPtr TypeForwardedTo=False
G O:System.IntPtr None EXCEPTION:ArgumentNullException:"Value cannot be null. (Parameter 'value')"
G O:System.IntPtr Serializable found
GA:type=<System.SerializableAttribute>:ctor=<System.SerializableAttribute..ctor>:err=False:F=0:N=0
G O:System.IntPtr ComImport null
G O:System.IntPtr SpecialName null
G O:System.IntPtr StructLayout null
G O:System.IntPtr Flags null
G O:System.IntPtr Obsolete null
G O:System.IntPtr Extension null
G O:System.IntPtr CompilerGenerated null
G O:System.IntPtr Nullable null
G O:System.IntPtr NullableContext null
G O:System.IntPtr IsReadOnly found
GA:type=<System.Runtime.CompilerServices.IsReadOnlyAttribute>:ctor=<System.Runtime.CompilerServices.IsReadOnlyAttribute..ctor>:err=False:F=0:N=0
G O:System.IntPtr IsByRefLike null
G O:System.IntPtr Dynamic null
G O:System.IntPtr TupleElementNames null
G O:System.IntPtr DecimalConstant null
G O:System.IntPtr DefaultMember null
G O:System.IntPtr ParamArray null
G O:System.IntPtr Optional null
G O:System.IntPtr In null
G O:System.IntPtr Out null
G O:System.IntPtr FieldOffset null
G O:System.IntPtr NonSerialized null
G O:System.IntPtr MarshalAs null
G O:System.IntPtr PermissionSet null
G O:System.IntPtr DefaultParameterValue null
G O:System.IntPtr AssemblyVersion null
G O:System.IntPtr InternalsVisibleTo null
G O:System.IntPtr TypeForwardedTo null
)gold";

} // namespace ILSpy::Tests::AttributeGold
