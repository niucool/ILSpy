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

// The gold dump of the BamlDecompilerTypeSystem slice: the REAL
// ICSharpCode.BamlDecompiler 11.0 class driven over the same fixed-map stub
// resolver and the same local fixtures the port tests drive (the
// C:/temp-probe/BdtsProbe probe) -- the S1-S5 sections pin the ctor's
// reference-queue walk (the resolver call order, the dedup, the module list /
// order / identity, the synthetic-stand-in substitution, the MinimalCorlib
// fallback, and the FindType results) and S6 the raw File-table rows
// (metadata.AssemblyFiles: Name + ContainsMetadata).
// Regenerate with C:/temp-probe/BdtsProbe/gen_fixture.py after any probe
// change.

#pragma once

#include <array>

namespace ILSpy::Tests {

inline const std::array<const char*, 19> kBdtsGold_S1 = {
    R"bdts(RESOLVE|Resolve|mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|System.Xaml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(MODS|7)bdts",
    R"bdts(MOD|0|Name=mscorlib|Asm=mscorlib|Full=mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|1|Name=System|Asm=System|Full=System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|2|Name=WindowsBase|Asm=WindowsBase|Full=WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=MetadataModule)bdts",
    R"bdts(MOD|3|Name=PresentationCore|Asm=PresentationCore|Full=PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=MetadataModule)bdts",
    R"bdts(MOD|4|Name=PresentationFramework|Asm=PresentationFramework|Full=PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=MetadataModule)bdts",
    R"bdts(MOD|5|Name=PresentationUI|Asm=PresentationUI|Full=PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=MetadataModule)bdts",
    R"bdts(MOD|6|Name=System.Xml|Asm=System.Xml|Full=System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MAIN|Type=MetadataModule|SameAsMods0=True|IsMainModule=True)bdts",
    R"bdts(FIND|Void|System.Void)bdts",
    R"bdts(FIND|Int32|System.Int32)bdts",
};

inline const std::array<const char*, 18> kBdtsGold_S2 = {
    R"bdts(RESOLVE|Resolve|mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(MODS|7)bdts",
    R"bdts(MOD|0|Name=mscorlib|Asm=mscorlib|Full=mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|1|Name=System|Asm=System|Full=System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|2|Name=WindowsBase|Asm=WindowsBase|Full=WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|3|Name=PresentationCore|Asm=PresentationCore|Full=PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|4|Name=PresentationFramework|Asm=PresentationFramework|Full=PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|5|Name=PresentationUI|Asm=PresentationUI|Full=PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|6|Name=System.Xml|Asm=System.Xml|Full=System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=SyntheticWpfModule)bdts",
    R"bdts(MAIN|Type=MetadataModule|SameAsMods0=True|IsMainModule=True)bdts",
    R"bdts(FIND|Void|System.Void)bdts",
    R"bdts(FIND|Int32|System.Int32)bdts",
};

inline const std::array<const char*, 20> kBdtsGold_S3 = {
    R"bdts(RESOLVE|Resolve|mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(MODS|9)bdts",
    R"bdts(MOD|0|Name=tiny|Asm=tiny|Full=tiny|Type=MetadataModule)bdts",
    R"bdts(MOD|1|Name=mscorlib|Asm=mscorlib|Full=mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|2|Name=System|Asm=System|Full=System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|3|Name=WindowsBase|Asm=WindowsBase|Full=WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|4|Name=PresentationCore|Asm=PresentationCore|Full=PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|5|Name=PresentationFramework|Asm=PresentationFramework|Full=PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|6|Name=PresentationUI|Asm=PresentationUI|Full=PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|7|Name=System.Xml|Asm=System.Xml|Full=System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|8|Name=corlib|Asm=corlib|Full=corlib|Type=MinimalCorlib)bdts",
    R"bdts(MAIN|Type=MetadataModule|SameAsMods0=True|IsMainModule=True)bdts",
    R"bdts(FIND|Void|System.Void)bdts",
    R"bdts(FIND|Int32|System.Int32)bdts",
};

inline const std::array<const char*, 22> kBdtsGold_S4 = {
    R"bdts(RESOLVE|Resolve|mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|System.Core, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|System.ComponentModel.Composition, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(MODS|9)bdts",
    R"bdts(MOD|0|Name=System.Runtime|Asm=System.Runtime|Full=System.Runtime, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a|Type=MetadataModule)bdts",
    R"bdts(MOD|1|Name=mscorlib|Asm=mscorlib|Full=mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|2|Name=System.Core|Asm=System.Core|Full=System.Core, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|3|Name=System|Asm=System|Full=System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|4|Name=WindowsBase|Asm=WindowsBase|Full=WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|5|Name=PresentationCore|Asm=PresentationCore|Full=PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|6|Name=PresentationFramework|Asm=PresentationFramework|Full=PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|7|Name=PresentationUI|Asm=PresentationUI|Full=PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|8|Name=System.Xml|Asm=System.Xml|Full=System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=SyntheticWpfModule)bdts",
    R"bdts(MAIN|Type=MetadataModule|SameAsMods0=True|IsMainModule=True)bdts",
    R"bdts(FIND|Void|System.Void)bdts",
    R"bdts(FIND|Int32|System.Int32)bdts",
};

inline const std::array<const char*, 24> kBdtsGold_S5 = {
    R"bdts(RESOLVE|ResolveModule|sub.mod)bdts",
    R"bdts(RESOLVE|Resolve|DepOne, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null)bdts",
    R"bdts(RESOLVE|Resolve|DepTwo, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null)bdts",
    R"bdts(RESOLVE|Resolve|mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|Resolve|WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35)bdts",
    R"bdts(RESOLVE|Resolve|System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089)bdts",
    R"bdts(RESOLVE|ResolveModule|linked.mod)bdts",
    R"bdts(MODS|9)bdts",
    R"bdts(MOD|0|Name=BdtsSynth|Asm=BdtsSynth|Full=BdtsSynth, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null|Type=MetadataModule)bdts",
    R"bdts(MOD|1|Name=System|Asm=System|Full=System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|2|Name=System.Xml|Asm=System.Xml|Full=System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|3|Name=BdtsDep|Asm=BdtsDep|Full=BdtsDep, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null|Type=MetadataModule)bdts",
    R"bdts(MOD|4|Name=mscorlib|Asm=mscorlib|Full=mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089|Type=MetadataModule)bdts",
    R"bdts(MOD|5|Name=WindowsBase|Asm=WindowsBase|Full=WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|6|Name=PresentationCore|Asm=PresentationCore|Full=PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|7|Name=PresentationFramework|Asm=PresentationFramework|Full=PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MOD|8|Name=PresentationUI|Asm=PresentationUI|Full=PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35|Type=SyntheticWpfModule)bdts",
    R"bdts(MAIN|Type=MetadataModule|SameAsMods0=True|IsMainModule=True)bdts",
    R"bdts(FIND|Void|System.Void)bdts",
    R"bdts(FIND|Int32|System.Int32)bdts",
};

inline const std::array<const char*, 7> kBdtsGold_S6 = {
    R"bdts(FILE|mscorlib|normidna.nlp|False)bdts",
    R"bdts(FILE|mscorlib|normnfc.nlp|False)bdts",
    R"bdts(FILE|mscorlib|normnfd.nlp|False)bdts",
    R"bdts(FILE|mscorlib|normnfkc.nlp|False)bdts",
    R"bdts(FILE|mscorlib|normnfkd.nlp|False)bdts",
    R"bdts(FILE|BdtsSynth|sub.mod|True)bdts",
    R"bdts(FILE|BdtsSynth|nometa.mod|False)bdts",
};

} // namespace ILSpy::Tests
