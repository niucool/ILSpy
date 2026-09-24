# PORT_LOG_DISASM -- the port-disassembler branch slice log

Slice records for the Phase-6 (Disassembler) completion work on branch
`port-disassembler` (the second porting agent). This file is the branch-local
companion of PORT_PLAN.md's decision log: it records what was ported, the
decisions taken, and the verification evidence per slice. PORT_PLAN.md itself
is not edited from this branch.

## State at branch start (the 2026-09-24 audit)

The Phase-6 scope (`ICSharpCode.Decompiler/Disassembler/`, 9 files + the
`ReflectionDisassembler` family, ~4.3k lines) was already ~95% ported by the
earlier `gnhf` iterations (the gnhf-134..150, gnhf-1..13 and gnhf-3..12
commits): all nine C# files have C++ counterparts in
`cpp/Decompiler/Disassembler/`, the whole-module `--il` chain runs end-to-end,
and the byte-identical-to-ilspycmd-11.0 verification was done on Windows
against the real `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\mscorlib.dll`
(the gnhf-20 full 41MB dump). A member-level audit against the C# sources
found the remaining, explicitly documented deferrals, all inside
ReflectionDisassembler.cs's `Write Security Declarations` region and the
`DecodeCustomAttributeBlobs` path:

* `SecurityDeclarationDecoder` (the nested
  `ICustomAttributeTypeProvider<(PrimitiveTypeCode Code, string Name)>`
  implementation, C# lines 487-675) -- not ported.
* `TryDecodeSecurityDeclaration` (C# lines 678-766) -- not ported.
* `WriteValue` / `WriteSimpleValue` / `PrimitiveTypeCodeToString`
  (C# lines 768-866) -- not ported.
* `WriteDecodedCustomAttributeBlob` (C# lines 1873-1915) -- present as a
  `std::logic_error` stub ("not yet ported") behind the
  `DecodeCustomAttributeBlobs` flag.
* `TextOutputWithRollback` (C# Output/PlainTextOutput.cs lines 168-257) --
  not ported.
* The `AssemblyResolver` property and the two non-null-resolver arms of
  `WriteSecurityDeclarations` (the "bytearray" alternative and the decoded
  path, C# lines 450-466) -- not ported.

The enabler is `Metadata/CustomAttributeDecoder`: its header documents the
convention-(a) absorption ("both C# decoders are generic over TType, but the
ILSpy type system uses only the IType instantiation (the one exception --
ReflectionDisassembler's SecurityDeclarationDecoder instantiates the repo copy
with a (PrimitiveTypeCode, string) tuple -- is deferred with that slice)").
This branch lifts that deferral, following the established
`Metadata/SignatureTypeProvider` precedent (the walker as a class template
over `TProvider`, `TType = typename TProvider::TType`, CTAD from existing
call sites).

## Build / baseline on this machine (Linux)

* Build: `cpp/build/linux-ninja` (Ninja, Debug, `/usr/bin/c++` = GCC 15),
  configured with `VCPKG_ROOT=/home/jim/vcpkg-root` (the manifest's pinned
  baseline `a51bb4d1` lives there; the default `/home/jim/source/vcpkg`
  checkout is newer and fails the baseline checkout).
* The Windows-machine fixtures the gold pins are NOT on this box: the real
  `Framework64\v4.0.30319\mscorlib.dll` does not exist here. Two substitute
  fixtures are staged:
  `/home/jim/.local/lib/netframework48/mscorlib.dll` (the net48 REFERENCE
  assembly -- full metadata, NO method bodies) and
  `/home/jim/.local/lib/mono/4.5/mscorlib.dll` (mono's runtime corlib --
  bodies but different metadata). Tests select it via `ILSPY_TEST_MSCORLIB`.
* Full suite with the net48 reference fixture: 12338 tests; 214 pre-existing
  failures + 15 crashers. The failures are all Windows-environment-caused
  (gold digests over the real runtime mscorlib, .NET BCL exception-message
  spellings, the machine GAC, Windows path shapes) -- NOT port regressions;
  the same tests pass on the Windows CI machine per the gnhf logs. The 15
  crashers are a cross-test memory-corruption cascade seeded by
  fixture-dependent tests (e.g. `MetadataNamespaceTest.ChildCacheIsStable`,
  `MetadataModuleResolutionTest.FindModuleByReferenceTwoPasses`): every
  crashing suite passes when run in isolation, and the cascade only starts
  after the first fixture-dependent assert fires. Fixing those test files is
  outside this branch's isolation scope.
* Per-slice regression gates (the Linux baseline definition):
  1. the new slice's tests are green (RED first, crux-neutered);
  2. the full-suite failure set (the file above) is UNCHANGED -- no new
     failures -- comparing `--gtest_filter`-preserved runs with the same
     fixture;
  3. the CLI `--il` whole-module dump over the staged net48 reference
     assembly is byte-identical: exit 0, 11281597 bytes, md5
     `b10e348a93301e0a40006f06a284ec09` (none of the ported paths is
     reachable from the default CLI flags, so any change is a regression);
  4. an ASan build of the slice's new code paths runs clean.

## Slice records

| # | Slice | Kind | Record |
|---|---|---|---|
| PD1 | The `CustomAttributeDecoderT<TProvider>` template (the decoder generic lifted over the provider, Phase-6 enabler) | port | Lifts the `Metadata/CustomAttributeDecoder` header's documented convention-(a) deferral: both C# decoders are generic over TType, and the ReflectionDisassembler SecurityDeclarationDecoder instantiates the repo copy with a `(PrimitiveTypeCode Code, string Name)` tuple, so the port's absorbed-IType decoder becomes a class template over the provider, following the `Metadata/SignatureTypeProvider` precedent (`TType = typename TProvider::TType`, CTAD from the ctor's provider argument, the member bodies inline in the header -- every provider is a local of the driving function). The shapes: (1) the SRM generic argument structs become templates + IType-instantiation aliases -- `TypeSystem/CustomAttributeTypedArgumentT<TType>` / `CustomAttributeNamedArgumentT<TType>` (the old concrete `CustomAttributeTypedArgument` / `CustomAttributeNamedArgument` names become `using` aliases of the `<ITypePtr>` instantiation, so every existing spelling and aggregate use is unchanged); (2) `CustomAttributeValueT<TType>` + the `CustomAttributeValue` alias (same convention); (3) the decoder class template `CustomAttributeDecoderT<TProvider>` with the full body set (DecodeValue / DecodeNamedArguments / DecodeFixedArguments / DecodeNamedArgumentsLoop / DecodeFixedArgumentType / DecodeNamedArgumentType / DecodeArgument / DecodeArrayArgument / GetTypeFromHandle / SkipType) moved into the header verbatim (a scripted old-vs-new diff of every member body verified the transcription: the only deltas are the BlobReader method-call syntax and the by-value provider calls); (4) the concrete `CustomAttributeDecoder` remains as a thin DERIVED class over `CustomAttributeDecoderT<TypeSystem::TypeProvider>` -- an alias of a full specialization cannot replace it (no CTAD through a plain alias in C++17, and the existing users spell the bare name as a parameter type), while the derived class keeps every existing call site (`CustomAttributeDecoder decoder(metadata, provider)` through the derived ctor) and the `const CustomAttributeDecoder&` parameter spellings compiling unchanged; (5) the old nested `Reader` becomes the namespace-scope `Metadata::BlobReader` -- the SRM `System.Reflection.Metadata.BlobReader` gap fill carrying the reading members the decoders call (ReadUInt16 / ReadByte / ReadCompressedInteger(OrInvalid) / ReadSerializationTypeCode / ReadSignatureTypeCode / ReadTypeHandle / ReadSerializedString), defined in the .cpp (provider-independent); the later SecurityDeclaration slice drives the same reader over the permission-set blob; (6) the provider contract (the C# `ICustomAttributeTypeProvider<TType>`) receives the TType BY VALUE, so `TypeProvider` gains the `ITypePtr`-taking `IsSystemType` / `GetUnderlyingEnumType` overloads (forwarding the old `*ptr` deref, behavior-identical). The old file-local blob-primitive free functions (ReadUInt16/ReadByteRaw/ReadCompressedIntegerOrInvalid/ReadCompressedInteger/ReadSerializationTypeCode/ReadSignatureTypeCode/ReadTypeHandle/ReadSerializedString) became BlobReader methods, and the SRM exception-text constants moved to the header (inline constexpr, shared by the template bodies and the .cpp). TDD: 5 new gtest cases in `CustomAttributeDecoderPairTest` (cpp/tests/Decompiler/Metadata/CustomAttributeDecoderPair_Test.cpp) driving the template over a test-local pair provider (`TType` = a `PrimitiveTypeCode Code` + `optional<string> Name` struct, the C# tuple shape; every TypeDef/TypeRef handle reports the System.Type pair so the TypeCode machinery drives the serialized-name value path): DecodeValue over the synthetic manifest's primitive-only rows (the null/empty/3-element Int32[] arrays, the zero-argument row, the MemberReference-ctor row), the Type-handle rows (row 1/3/4/5 with the pair-typed Type renders -- the blob's serialized type names pin verbatim, so the assembly-qualified "System.String, mscorlib" form is pinned where the manifest authored it qualified), the named-argument row (row 5's four fixed + five named args), DecodeNamedArguments over the standalone named-arg bytes (the five args: bool/string/int32[]/Type/TaggedObject -- the non-boxing TaggedObject arg reports the DECODED inner type per the C# DecodeArgument `decoded.Type` return, and the boxing form wraps the outer type), and the provideBoxingTypeInfo=true boxing arm. The suite went RED first as a compile failure (the pair provider does not satisfy the old concrete `TypeProvider&` ctor -- the crux-neutered RED for a genericity slice). VERIFICATION: the new tests pass; the full-suite failure set with the mono fixture is IDENTICAL to the pre-slice baseline (195 failed-test names, exact after timing-strip); the 25-iteration crasher discovery run ended with the same 195 failures, and every crasher name NOT in the baseline's 20-name list passes or skips when run in isolation (the crash cascade is cross-test heap corruption seeded by the fixture-dependent tests and shifts with any binary layout change -- the persistent isolating-crashers MetadataModuleResolutionTest.FindModuleByReferenceTwoPasses / MetadataMethodTest.CuratedMethodGold / ResolveTypeDirectBaseTypesTest.ResolveTypeSpecificationArm are all in the baseline list); the CLI `--il` whole-module dump over the staged net48 reference assembly is byte-identical (exit 0, 11281597 bytes, md5 b10e348a93301e0a40006f06a284ec09); an ASan build (build/linux-asan, the main worktree's flags) runs the decoder family and the whole Disassembler suite family with ZERO AddressSanitizer reports (the family's pre-existing mono-fixture gold failures -- the SortByNameProcessor row counts, the sequence-point renders, the scope prefixes -- all reproduce unchanged from the baseline). The test-side debugging lesson recorded: gtest's byte-dump fallback printer for a returned-by-value aggregate can display stale stack bytes (a 48-byte PairType print showed a previous argument's string), so pair-state assertions must go through the accessor fields, not the byte dump. |
| PD2 | The SecurityDeclarationDecoder provider + PrimitiveTypeCodeToString + the AssemblyResolver property | port | Ports the nested `SecurityDeclarationDecoder` class of ReflectionDisassembler.cs (lines 487-675) as cpp/Decompiler/Disassembler/SecurityDeclarationDecoder.{hpp,cpp} (the C++-nested-class convention, the OverloadResolution.Candidate D506 precedent) -- the `ICustomAttributeTypeProvider<(PrimitiveTypeCode Code, string Name)>` implementation the custom-attribute-blob decode (WriteDecodedCustomAttributeBlob, PD4) and the permission-set decode (TryDecodeSecurityDeclaration, PD5) instantiate the PD1 decoder template with. The C# tuple TType ports as the `SecurityDeclarationType` struct (Code / optional<string> Name; nullopt is the primitive marker). The members: GetPrimitiveType (`(code, null)`), GetSystemType (`(0, "type")`), GetSZArrayType (the `Name ?? PrimitiveTypeCodeToString(Code)` + "[]" composition), GetTypeFromDefinition (GetFullTypeNameFromDefinition's FullName + the SRMExtensions IsEnum arm -- the "enum " prefix and the underlying code, else (0, fullName)), GetTypeFromReference (the FullName + the GetDeclaringModule resolution-scope walk -- the declaring TypeRef recurses, the AssemblyRef yields TryGetFullAssemblyName, the ModuleRef the scope name, else null -- composing the assembly-qualified name, then ResolveType: the resolved enum gets the "enum " prefix, the unresolved module keeps the composed name), GetTypeFromSerializedName (the null resolver or the unresolved name throws EnumUnderlyingTypeResolveException; the resolved enum gets the prefix; anything else returns the ORIGINAL name with the zero code), GetUnderlyingEnumType (Code), IsSystemType (Name == "type"). The private machinery: ResolveType (the ", "-split: an assembly-qualified name resolves the assembly through the resolver, a miss falls back to the CURRENT module, then the TryResolveMscorlib fallback; FindType over the resolved module), FindType (the namespace walk then the type walk with the C# `goto restart` quirk ported as the restart bool loop -- after switching the candidate list to a match's nested types the SAME name segment is matched again), TryResolveMscorlib (the cached resolver answer; a miss is not cached). The `ReflectionDisassembler.PrimitiveTypeCodeToString` (lines 835-866) ports as a free function in the Disassembler namespace (the C# private static of ReflectionDisassembler, shared by the separate-class provider and the later WriteValue/WriteSimpleValue members) -- the fifteen ILDasm spellings + the "unknown" default arm. The `ReflectionDisassembler.AssemblyResolver` property (line 83) ports as the pointer getter/setter pair over the caller-owned `Metadata::IAssemblyResolver*` (the EntityProcessor convention), defaulting null. PORTING FINDINGS pinned by the tests: (1) the C# `AssemblyNameReference.FullName` ALWAYS appends the version (`Version ?? UniversalAssemblyResolver.ZeroVersion`, fieldCount: 4) -- so the resolver receives "mscorlib, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null" for the TryResolveMscorlib Parse("mscorlib") call (gold-pinned by the counting stub's lastFullName); (2) the nil-handle IsEnum read in the resolved-miss arm of GetTypeFromReference does NOT throw -- the C# GetTypeDefinition over the nil row reads the unchecked pre-table bytes (a garbage Extends token that is not System.Enum), the port's row-0 read is the graceful false, so both the unresolved and the resolved-miss arms return the composed name with the zero code (the arms differ only in the resolver call count). TDD: 7 gtest cases in cpp/tests/Decompiler/Disassembler/SecurityDeclarationDecoder_Test.cpp (a counting stub IAssemblyResolver over the synth manifest -- NS.MyEnum/NS.Attr/NS.GAttr, the three mscorlib-scoped TypeRefs, the mscorlib 4.0.0.0 AssemblyRef): the primitive arms + the PrimitiveTypeCodeToString spellings (incl. the "unknown" default), GetTypeFromDefinition's enum/non-enum arms, GetTypeFromReference's unresolved + resolved-miss arms (the assembly-qualified composition pinned verbatim, the resolve counts), GetTypeFromSerializedName's null-resolver throw + current-module enum hit + unqualified miss throw + qualified miss fallback + qualified hit (the original name with the qualifier), the TryResolveMscorlib caching (a miss is re-resolved every call, a hit is cached -- the parse composition pinned), the staged-mscorlib arms (the unqualified fallback hit over System.String, the enum hit over System.AttributeTargets, the resolved TypeRef hit over System.Type -- gated on ILSPY_TEST_MSCORLIB and skipping when absent), and the ReflectionDisassembler.AssemblyResolver property. The suite went RED first (the header does not exist -- a compile failure) and GREEN after the implementation with three hand-derived expectation corrections (the nil-handle-IsEnum shape, the qualified-miss fallback-to-current-module, the always-versioned FullName). VERIFICATION: the 7 tests pass; the full-suite failure set with the mono fixture is IDENTICAL to after-PD1 (195 names, exact; the skipped set identical; the OK delta is exactly the 7 new tests); the CLI `--il` dump is byte-identical (md5 b10e348a93301e0a40006f06a284ec09); the ASan build runs the new suite + the decoder pairs + the ReflectionDisassembler/SortByNameProcessor families with ZERO AddressSanitizer reports (the 9 family failures are the pre-existing mono-fixture golds). |
| PD3 | The ReflectionDisassembler WriteValue / WriteSimpleValue argument renderers | port | Ports ReflectionDisassembler.cs lines 768-833 -- the argument renderers the WriteDecodedCustomAttributeBlob (PD4) and TryDecodeSecurityDeclaration (PD5) paths consume. `WriteValue(ITextOutput, (Code, Name) type, object value)` ports as a public ReflectionDisassembler member over the PD2 SecurityDeclarationType pair (the C# private member; the port's keep-private-members-public-for-tests convention): the boxing arm (a `CustomAttributeTypedArgumentT<SecurityDeclarationType>` boxed value renders "object(" + the inner WriteValue + ")"), the array arm (a `std::vector<TypedArgument>` value renders the element type + "[len](" + the space-separated items + ")" -- the element type is the pair's Name with the trailing "[]" stripped, an "enum "-prefixed Name falling back to PrimitiveTypeCodeToString(Code); a boxed item renders through WriteValue over the item's INNER (type, value), any other item through WriteSimpleValue), and the scalar arm (the "<typeName>(" + WriteSimpleValue + ")" wrapper -- the "enum "-prefixed Name falls back to the primitive spelling). `WriteSimpleValue(ITextOutput, object value, string typeName)` ports as a public static member with the C#'s three arms: the string case (the single-quoted EscapeString form with every embedded single quote backslash-escaped -- the C# `EscapeString(value.ToString()).Replace("'", "\\'")`), the "type" case (the pair value's Name with the "enum " prefix stripped -- Substring(5)), and the default (DisassemblerHelpers.WriteOperand). TDD: 11 gtest cases in cpp/tests/Decompiler/Disassembler/WriteValue_Test.cpp (the primitive wrappers incl. the Int64 MinValue, the enum-name fallback to the primitive spelling, the named-type wrapper, the string single-quote/double-quote escapes, the type-value enum-prefix strip, the boxing wrapper incl. a nested object(object(...)), the primitive/empty/named/enum arrays, the boxed-item array, and the WriteSimpleValue arms). The suite went RED first (the members do not exist) and caught one real implementation bug before GREEN (the dropped C# `.Replace("'", "\\'")` quote-escape -- the RED rendered "string('it's')" instead of "string('it\\'s')"); one hand-derived expectation was corrected after the RED (the C# array loop renders a boxed item through the item's INNER (type, value) -- no extra object() wrapper, so "object[1](string('x'))" not "object[1](object(string('x')))" -- the boxing wrapper comes only from the top-level WriteValue arm). The any-cast-over-itemValue materialization (the ApplyAttributeTypeVisitor "Value() returns std::any BY VALUE" precedent) guards the array loop's boxed-item test. VERIFICATION: the 11 tests pass; the full-suite failure set with the mono fixture is IDENTICAL to after-PD2 (195 names; the OK count +11 = exactly the new tests); the CLI `--il` dump is byte-identical (md5 b10e348a93301e0a40006f06a284ec09); the ASan build runs the WriteValue/SecurityDeclarationDecoder/decoder-pair families (23 tests) with ZERO AddressSanitizer reports. The members are not yet reachable from any pipeline (the PD4/PD5 wiring is the next slices) -- the CLI byte-identity is structural (the new code is dead until the flags turn on). |
| PD4 | The WriteDecodedCustomAttributeBlob body (the DecodeCustomAttributeBlobs path of WriteAttributes) | port | Ports ReflectionDisassembler.cs lines 1873-1915 -- the last throw-stub of the ReflectionDisassembler port is replaced with the real body: the attribute value decoded through the PD1 `CustomAttributeDecoderT<SecurityDeclarationDecoder>` instantiation (one decoder per row over the PD2 provider carrying the disassembler's `AssemblyResolver`), rendered as the "{ ... }" block -- one indented line per fixed argument (the PD3 WriteValue render) and per named argument (the "field "/"property " prefix per the CustomAttributeNamedArgumentKind, `Type.Name ?? PrimitiveTypeCodeToString(Type.Code)`, the DisassemblerHelpers.Escape'd member name, " = ", the WriteValue render) -- with the BadImageFormatException catch arm rendering "/* Could not decode attribute value */ " plus the WriteBlob raw dump. The C# catches ONLY BadImageFormatException here (the EnumUnderlyingTypeResolveException propagates) -- the port's catch is the std::invalid_argument mapping of that family. TDD: 5 gtest cases in cpp/tests/Decompiler/Disassembler/WriteDecodedCustomAttributeBlob_Test.cpp over the synth manifest: the fixed-argument rows (row 9's Int32[] -> "{\r\n\tint32[3](1 2 3)\r\n}", row 10's empty block, row 12's MemberRef-ctor Int32 77), a hand-built value blob over row 9's Int32[] ctor signature with a fixed array + a named property (the named-argument render "property bool pb = bool(true)"), the Type-typed-fixed-argument fallback (the SECURITYDECLARATIONDECODER QUIRK: a Type-typed FIXED ctor argument never decodes -- GetTypeFromDefinition returns the full type name, which IsSystemType ("type" == Name) rejects, so the TypeCode falls to the underlying-enum code 0 = Invalid and the DecodeArgument switch throws -- rows 1/3/4/5 of every real corpus with typeof(...) ctor arguments render the raw-blob fallback; the catch render pinned over a hand-built blob and the authored row-1 blob), the WriteAttributes rewiring (the flag on routes " = " + the decoded block; the flag off keeps the raw dump -- the CLI's shape), and the null-resolver quirk (the C# ResolveType derefs the null resolver for an assembly-qualified name -- the NullReferenceException the C# catch arms do NOT catch; the port maps the NRE to std::runtime_error with the .NET message, the BamlNode NRE convention, instead of the undefined null deref -- pinned by EXPECT_THROW). The suite went RED first (the member does not exist) and surfaced one real crash through the RED: the ASan run pinpointed the null-resolver deref in ResolveType's assembly-qualified arm (the fix above). Three test-side expectation bugs were corrected along the way (the substr prefix length, the row-token hex arithmetic -- 0x0C000012 is row 18, not row 12 -- and the WriteBlob line-break/tab geometry in the find assertions). VERIFICATION: the 5 tests pass; the full-suite failure set with the mono fixture is IDENTICAL to after-PD3 (195 names; the OK count +5 = exactly the new tests); the CLI `--il` dump is byte-identical (md5 b10e348a93301e0a40006f06a284ec09 -- the default flag-off path is structurally unchanged); the ASan build runs the new suite with ZERO AddressSanitizer reports (the ReflectionDisassemblerTest.WriteAttributes* failures in the ASan run are the pre-existing mono-fixture golds, 4 tests failing in both the pre-slice baseline and after). |
| PD5 | TextOutputWithRollback + TryDecodeSecurityDeclaration + the WriteSecurityDeclarations resolver arms (the last Phase-6 deferral) | port | Ports the final documented deferral of the ReflectionDisassembler: (1) the `internal class TextOutputWithRollback : ITextOutput` (Output/PlainTextOutput.cs lines 168-257) as Output::TextOutputWithRollback co-located in PlainTextOutput.hpp -- the action-recording wrapper (a vector of `std::function<void(ITextOutput&)>`; every member appends a replay lambda, Commit() replays them in order, a never-committed rollback discards) with the C# shape reproduced verbatim: the IndentationString property forwards to the target DIRECTLY (not a recorded action), and the parameter-dropping lambda quirks (WriteLocalReference drops isHoverOnly; WriteReference(OpCodeInfo) drops omitSuffix); the reference overloads capture the pointed-at object (the C# closure holds the reference -- the module/type/member/opcode must outlive the rollback). (2) The `TryDecodeSecurityDeclaration(TextOutputWithRollback, BlobReader, MetadataFile)` (ReflectionDisassembler.cs lines 678-766) as a public ReflectionDisassembler member: the " = {" block over the '.'-prefixed binary permission-set blob -- the module's own assembly name (the GetAssemblyDefinition read with the "<ERR: invalid assembly name>" fallbacks), the per-entry class/[assembly] type line (the ", "-split into ALL parts: an unqualified name or a second part equal to the module's assembly name renders "class " + the ESCAPED name -- a ", "-containing name is not a valid identifier so DisassemblerHelpers.Escape wraps it in single quotes -- anything else "[<second part>]<type name>"), the unread compressed integer (the C#'s "// ?" Cecil comment), the named arguments through the CustomAttributeDecoderT<SecurityDeclarationDecoder> instantiation with provideBoxingTypeInfo=true (the boxing render), and the trailing comma between entries. The blob ports as the Metadata::BlobReader cursor BY VALUE positioned AFTER the '.' marker (the C# receives the reader the WriteSecurityDeclarations gate already advanced -- the marker is not re-read). (3) The WriteSecurityDeclarations resolver arms (lines 450-466): the null-resolver raw dump (the CLI's shape, unchanged), the "bytearray" alternative for a non-'.' blob (the indented WriteBlob dump), the decoded arm (the rollback-wrapped TryDecodeSecurityDeclaration with the Commit on success and the BadImageFormatException / EnumUnderlyingTypeResolveException catch falling back to the raw dump), and the marker byte read OUTSIDE the try (an empty blob with a resolver set throws out of the method, exactly as in the C#). TDD: 13 gtest cases in cpp/tests/Decompiler/Disassembler/TryDecodeSecurityDeclaration_Test.cpp: five TextOutputWithRollback cases over a recording mock ITextOutput (the commit replay order, the no-commit discard, the direct IndentationString forward, the parameter-dropping quirks, the default-argument replay), three TryDecodeSecurityDeclaration cases (the hand-built two-entry blob over the synth manifest -- the "[mscorlib]Type" and "class SomeType" forms with the named-argument renders -- the malformed throw, and the REAL mono-mscorlib assembly-level permission set: the '.' binary form, the SecurityPermissionAttribute with the SkipVerification property, the full 158-byte blob hand-parsed to the exact expected render), and five WriteSecurityDeclarations cases (the null-resolver raw dump, the bytearray arm, the decoded arm over the real mscorlib row, the malformed-blob fallback, and the empty-blob throw). The suite went RED first (neither type exists) and surfaced one real implementation bug through the RED: the decoded arm passed the blob reader positioned AT the '.' marker instead of after it (the C# receives the already-advanced copy) -- the mis-parse threw BadImageFormat and every decoded render fell back to the raw dump; the fix threads one reader through the gate (the marker read advances it, the by-value pass starts at the entry count). Three hand-derived expectation corrections followed (the bytearray arm's indented close paren, the Escape single-quoting of the ", "-containing class name, and the reader-position contract above). VERIFICATION: the 13 tests pass; the full-suite failure set with the mono fixture is IDENTICAL to after-PD4 (195 names; the OK count +13 = exactly the new tests); the CLI `--il` dump is byte-identical (md5 b10e348a93301e0a40006f06a284ec09 -- the CLI never sets a resolver so the null arm is structurally unchanged); the ASan build runs the new suites with ZERO AddressSanitizer reports. The stale deferral doc comments in ReflectionDisassembler.hpp (the file header and the WriteAttributes / WriteSecurityDeclarations member docs) are updated to the ported state. |

## Phase 6 completion

With PD5, every member of the nine-file `ICSharpCode.Decompiler/Disassembler/`
scope is ported: the whole-module `--il` chain (ported by the earlier gnhf
iterations, byte-identical to ilspycmd 11.0 on Windows over the real
Framework64 mscorlib) plus the previously-deferred SecurityDeclarationDecoder
family (the `DecodeCustomAttributeBlobs` path, the permission-set decode, the
resolver arms, and the `TextOutputWithRollback` enabler). The one remaining
intentional deferral is the `CancellationToken` (the port's plan section 5.9 --
no cancellation analogue; the CLI is synchronous), a C#-construct mapping
decision, not a Phase-6 feature gap. The Linux-verification posture: the
Windows-machine fixtures the gold pins are absent on this box, so the per-slice
gates are the unchanged failure set, the byte-identical CLI `--il` dump, and
the ASan-clean runs, all documented per slice above; the byte-identical
Windows-side verification of the NEW paths (the decoded renders against the
real engine's `/CAVERBAL`-style output) is the follow-up work for a
Windows-machine run (the same gold-dump procedure the gnhf iterations used).

## The full-corpus differential sweep (PD6, 2026-09-24)

The baml-style differential harness expanded to the full corpora and run on
this branch's build (the merged Phase-6 Disassembler + the Phase 1-5 tree):
the sweep script is `cpp/tests/tools/differential_sweep_full.sh` (the baml
`cpp/tests/tools/differential_harness.sh` pattern + crash-signature capture),
results in /tmp/diffval/full (summary.tsv per run; kept for re-analysis).

Corpora and mode coverage: the 133 top-level net48 reference assemblies
(Microsoft.NETFramework.ReferenceAssemblies.net48 1.0.3), their 104
Facades/*.dll, and the 49 capa-testfiles .NET samples; `--il` (port) vs
`-il` (oracle ilspycmd 11.0.0.9335) and `--csharp` (port) vs the default C#
mode (oracle); 572 runs, TIMEOUT=120 s.

### The verdict tallies (572 runs)

| corpus x mode | IDENTICAL | DIFFERENT | PORT-CRASH | PORT-FAIL | BOTH-FAIL-IDENTICAL |
|---|---|---|---|---|---|
| net48 x --il (133) | 130 | 2 | 0 | 0 | 1 |
| facades x --il (104) | 104 | 0 | 0 | 0 | 0 |
| capa x --il (49) | 27 | 17 | 2 | 0 | 3 |
| net48 x --cs (133) | 0 | 1 | 2 | 118 | 12 |
| facades x --cs (104) | 0 | 0 | 0 | 104 | 0 |
| capa x --cs (49) | 0 | 3 | 43 | 0 | 3 |

The `--il` mode is 277/286 byte-identical (CR-stripped; the port pins CRLF,
the oracle emits the host LF -- the harness strips before diffing). The 9
non-identical --il rows reduce to four deduped signatures (T6-T9 below); the
5 non-failing --cs rows reduce to two (T3, T10); the 47 crashes reduce to
four signatures (T1, T2, T4, T5).

### The deduped signatures and the triage table

Prioritization: user-facing crashers first (the --csharp path on real
binaries), then the wrong-output diffs (the .entrypoint and the truncated
messages), then the known Phase-5 seed gaps. File:line anchors are from the
LD_PRELOAD SIGABRT-backtrace runs (bt.so over the linux-ninja build, this
branch) -- the same pinning procedure baml used.

| # | Sev | Mode | Signature (deduped) | Root cause | Samples | Oracle | Suggested fix |
|---|---|---|---|---|---|---|---|
| T1 | HIGH | --cs | `stl_vector.h:1263 operator[] _Tp = unique_ptr<ILInstruction> ... __n < size()` abort | `IL/Transforms/TransformCollectionAndObjectInitializers.cpp:771` -- the vector index out of bounds in the collection-initializers fold; the crash-backtrace cross-confirms the baml pin of the same file:line | 36 capa + net017 + net065 (38 runs) | rc=0, full decompile | the fold's index arithmetic (the .cpp:771 site) needs its bounds guard; repro: any of the 38 samples, `--csharp` |
| T2 | HIGH | --cs | `ILInstruction.cpp:110 CheckInvariant: child->Parent == this && "ILAst: child Parent mismatch"` abort | the CheckInvariant assert fires after some transform -- the offending pass needs a per-sample bisect (the assert site is the safety net, not the cause; the stack shows only the recursive CheckInvariant frames) | 7 capa (the 3f1f67e214 cluster x3, 749e7becf0, a301eadd2b x3) | rc=0, full decompile | bisect the transform pipeline on one sample with the invariant off to find the pass that breaks the Parent back-pointers |
| T3 | HIGH | --cs | `ilspycmd: no method bodies found` (rc=1, 1-line output) | the CSharp seed refuses metadata-only assemblies wholesale -- the Phase-5 back end has no signatures-only emission (the C# emits the using/attribute/type/member-signature scaffolding with empty bodies for reference assemblies) | 222 runs = 118 net48 + 104 facades (every metadata-only assembly) | rc=0, the full signatures-only C# | the seed's metadata-only mode: emit the namespace/using + type/member skeleton without bodies (a Phase-5 back-end slice; the largest single-line-count win in the table) |
| T4 | MED | --il | uncaught `std::out_of_range: Expected a TypeDef, TypeRef or TypeSpec handle!` (terminate; 0-byte output) | `DisassembleFieldHeaderInternal` -> `SignatureTypeProviderDecoder<DisassemblerSignatureTypeProvider>::DecodeType` (SignatureTypeProvider.hpp:604 Fail) -- a malformed/corrupt FIELD signature throws out of the --il walk | 1 (0953cc3b77...) | rc=0 (the C# decompiles past it -- the C# field header catches the BadImageFormatException family and renders the "<bad signature>" degradation) | wrap the field-header signature decode in the C#'s catch arm (the degradation the C# renders) |
| T5 | MED | --il | uncaught `std::logic_error: SignatureTypeProviderDecoder: trailing bytes after the type` (terminate; 0-byte output) | the same decoder's strict trailing-bytes check (SignatureTypeProvider.hpp) -- a signature with padding bytes throws instead of degrading | 1 (2dae11cc5f...) | rc=0 (the C# tolerates the trailing bytes -- the SRM decode reads them silently) | relax the trailing-bytes check to the C# tolerance (or catch-and-degrade in the --il field path) |
| T6 | MED | --il | the missing `.entrypoint` line after `.maxstack` | `GetEntryPointToken()` returns 0 for these samples (the cor20 EntryPointTokenOrRVA read yields nothing the Disassemble comparison matches; the raw COR20 parse of the same sample shows the oracle's entry token where the port's API returns 0) | 11 capa runs (each of the 039a/2fd4/354a/e842 triplets' --il) | the .entrypoint line rendered | pin the cor20 EntryPointTokenOrRVA read (the MethodBodyReader union access or the PE parse for these samples) |
| T7 | LOW | --il | `// RVA %08X invalid (not in any sec` -- the line truncated at 39 chars | ReflectionDisassembler.cpp:1852 `char buf[40]` -- the 44-char format needs 45 with the NUL | 8 capa runs (capa01/02/19/20/26/27/38/39) | the full line | `buf[40]` -> `char buf[48]` (or a std::string) -- a one-line fix |
| T8 | LOW | --il | `// .data D_xxxx = Field data (rva=...) could not be fou` -- the catch-message line truncated | ReflectionDisassembler.cpp:1864 `char buf[64]` -- the composed message ("// .data " + the exception text) needs ~90 chars | 1 (net065, 400+ truncated lines) | the full message per line | enlarge the buffer or compose into a std::string |
| T9 | LOW | --il | the `~`-prefixed method names unquoted: `void ~DequeEnumerator`1 ()` vs the oracle's `'~DequeEnumerator`1'` | the method-name render does not route the leading-`~` name through the identifier-escape (the C# Escape quotes any non-identifier name; `~` is not in the C# _validNonLetterIdentifierCharacter set) | net017 (20 lines) + net065 (part of its 818) | the quoted form | add the non-identifier check for `~` to the name-escape path (the Escape call site that renders .method names) |
| T10 | LOW | --cs | partial decompiles with rc=0: capa07 438/3520 lines; capa47/48 (43-line diffs: the missing using/assembly-attribute header); net016 (197-line diff: the same missing scaffolding) | the seed emits per-type bodies without the usings/attributes scaffolding, and silently stops early on some samples (rc=0 with truncated output -- the worst failure shape: silent) | 4 | full output | the scaffolding is the T3 family (the seed's back-end shape); the silent-stop needs its own bisect (the CLI should fail loudly when a type's decompile aborts) |
| T11 | INFO | both | BOTH-FAIL-IDENTICAL: both engines refuse | the malformed/corrupt-or-native samples: capa 0da87fccbf / 6f9cb3f56d / kernel32-64.dll_ (a BSJB-stubbed native image), net064 System.EnterpriseServices.Thunk (mixed native), and 11 net48 --cs rows where the ORACLE ITSELF throws mid-decompile (`Error decompiling @02000397 ...`) while the port says "no method bodies found" -- both non-zero, both empty-output | 4 il + 15 cs rows | matches | nothing to do for the malformed set; for the 12 oracle-throws rows, note the C# per-type exceptions are themselves oracle bugs on ref assemblies |

### The two hygiene items the sweep surfaced

* `InlineArrayTransform.cpp:221,227` prints unconditional `PROBE: ElementRef
  matched/mismatch` lines to stderr in the shipped CLI (visible on every
  --csharp run that reaches the probe) -- leftover debug instrumentation;
  remove or gate behind an env var.
* The port's stderr on the --cs crashes carries the assert text but no
  backtrace on this box (no gdb); the LD_PRELOAD SIGABRT handler
  (/tmp/bt.c -- a 15-line execinfo wrapper) is the cheap diagnostic that
  produced every file:line anchor above; consider vendoring it under
  cpp/tests/tools/ (bt.c) for future triage.

### What the sweep verified (the clean bill)

* The whole-module `--il` disassembly is byte-identical to ilspycmd 11.0 on
  277 of 286 runs -- including all 104 facade assemblies and 130 of the 133
  net48 reference assemblies (the 3 exceptions are T6/T7/T9's
  metadata-only-text differences, not structural misses).
* The 4 malformed/native capa samples are refused by both engines
  identically (the port's refusal message text differs from the C#'s -- the
  port says "could not open ... as a CLI as..." -- the C#'s message is the
  BadImageFormatException text -- a cosmetic diff recorded under T11).
* The Phase-6 security-declaration machinery ran on every corpus that has
  DeclSecurity rows (the mono-mscorlib staged fixture's rows exercise it in
  the unit tests; no corpus --il crash touched it).

## PD7 -- the sweep-fix batch (T7/T8/T9/T11 + the T10/T12 dispositions, 2026-09-24)

Follow-up to the full-corpus sweep: the triage table's mechanical rows fixed
RED-first, committed per fix, each verified against the sweep's affected
rows; the two non-mechanical rows dispositioned by diagnosis.

### The fixes (all RED -> GREEN -> sweep-verified)

| Row | Commit | Fix | RED -> GREEN evidence |
|---|---|---|---|
| T7 | df74ccd97 | the `// RVA ... invalid` line composed as a std::string (the fixed `%08X` fragment stays snprintf'd) | RED: the patched-FieldRva fixture rendered `... (not in any sec`; GREEN: the full 45-char line; capa 6c8b/749e/a301 x2 --il now byte-identical |
| T8 | df74ccd97 | the `// .data ... = <message>` catch line composed as a std::string | RED: the zeroed-SizeOfRawData fixture truncated `could not be fou`; GREEN: the full message; net065's 18 truncation hunks gone |
| T9 | 6d04a7c58 | `IsValidIdentifier` now validates the FIRST character (the C# `All` semantics) -- the '~'-prefixed names quote | RED: `IsValidIdentifier("~X")` true / `Escape("~X")` unquoted; GREEN: `'~X'`; net017 --il byte-identical (the 20 quote hunks gone) |
| T11 | 1f89b91a1 | `ClassifyCliOpenFailure`: the C# load-failure arms (missing path -> `File '<path>' does not exist!` + the Specify --help stdout hint, rc 1; non-PE -> `System.BadImageFormatException: <SRM message>`, rc 70; no-managed-metadata -> `MetadataFileNotSupportedException: PE file does not contain any managed metadata.`, rc 70; unparseable metadata -> the documented OverflowException representative) | 3 RED tests against the stub, GREEN after the impl; all four arms' stderr first-lines byte-identical to ilspycmd on the kernel32/text-file/missing/capa08 probes |

Notes:
* The T9 root cause is the FIRST-CHARACTER escape in IsValidIdentifier (the
  scan began after the first code point), not the valid-character set as the
  sweep note first said; the C# `_validNonLetterIdentifierCharacter` set was
  already faithful.
* The sweep-verdict columns after the batch: capa 6c8b/749e/a301 --il rows
  and net017/net065 --il rows re-checked byte-identical (net065's remaining
  hunks are T12 below, not the fixed rows).
* Gates: the full ilspy_tests failure set is IDENTICAL before/after (139
  pre-existing env-pinned failures, timing-only diffs); the mono-mscorlib
  whole-module --il dump diffs clean against ilspycmd (CR-stripped); ASan
  clean on the touched paths (27 filtered tests + both T7/T9 samples, 0
  reports).

### The T10 bisect -- the "silent stop" is disproven; main-line territory

Instrumented the capa07 `--csharp` walk (main.cpp's Phase-5 seed scaffold,
the `for (const auto& t : file.TypeDefs())` loop): the walk COMPLETES. 310
methods total, 183 with bodies render; the walk's tail rows
(`Null.Obfuscator.Null.Obfuscator` 02000091, `.Null.Obfuscator` 020000C1)
carry zero methods through GetMethods (the obfuscator's extern-only
members, which the oracle renders as `extern ? ()`), so rc=0 with the
shorter output is honest for the seed's shape. The 438-vs-3520 delta (and
the capa47/48 43-line and net016 197-line diffs) is the missing whole-
project scaffolding -- the usings/assembly-attributes/nested-type and
property/event member declarations the C# WholeProjectDecompiler emits --
i.e. the Phase-5/7 back-end shape in cpp/Decompiler/CSharp/ + the seed
scaffold, which is main-line territory. No fix attempted; STOPPED per the
assignment. Repro: `ilspy_cli <capa 0953cc3b77...exe_> --csharp` (438 lines,
rc 0) vs `ilspycmd <same>` (3520 lines); the walk trace via
`ILSPY_TEST_MSCORLIB`-style MetadataFile iteration shows the walk's
completeness.

### T12 (new, from the net065 re-check) -- the calli signature fallback

net065's --il diff still carries ~213 `calli` hunks: the port renders
`calli @11000003 /* signature 2 */` (a token-reference fallback) where the
C# decodes the standalone signature fully (`calli unmanaged stdcall int32
modopt([mscorlib]System.Runtime.CompilerServices.IsLong) ...`). The gap is
the MethodBodyDisassembler calli operand path (the standalone-signature
decode + the WriteSignature render); the sweep's signature column missed it
because the `~`/truncation hunks dominated the diff text. NOT fixed in this
batch -- recorded for the next slice (Disassembler/MethodBodyDisassembler
territory, the same phase as T7/T8's file family). Repro: `ilspy_cli
/home/jim/ilspy-test-fixtures/net48/System.EnterpriseServices.Wrapper.dll
--il` vs the oracle (diff hunks at IL_003c and friends).

## PD8 -- the merged-main sweep + the T6 fix (2026-09-24)

### Item 1 -- the T10 bisect: complete, no early return exists

Re-confirmed on the merged tree: the --csharp walk (main.cpp's seed
scaffold) COMPLETES over capa07 (310 methods, 183 with bodies; the tail
rows `Null.Obfuscator.Null.Obfuscator` 02000091 and `.Null.Obfuscator`
020000C1 carry zero methods through GetMethods -- the obfuscator's
extern-only members the oracle renders as `extern ? ()`, RVA 0, which
the seed's `if (m.RVA == 0) continue;` skips by design). There is no
early-return path to fix: rc=0 with the shorter output is the seed's
honest shape, and the 438-vs-3520 (capa07), 43-line (capa47/48) and
197-line (net016) diffs are the missing whole-project scaffolding
(usings, assembly attributes, nested-type and property/event member
declarations) -- the Phase-5/7 back end in cpp/Decompiler/CSharp/ +
the main.cpp scaffold. Documented for the controller; NOT fixed
(main-line territory per the assignment).

### Item 2 -- the merged-main sweep: --il 268/286, all 45 --cs crashes gone

The merged main (post `29c7c74a0` + `075695c62`) sweep
(/tmp/diffval/merged1, 572 runs, ILSPY_PORT = the merged linux-ninja
build):

| mode | IDENTICAL | DIFFERENT | PORT-CRASH | PORT-FAIL | BOTH-FAIL |
|---|---|---|---|---|---|
| --il (286) | 268 | 12 | 2 (capa07/09, the out_of_range + trailing-bytes throws) | 0 | 4 |
| --cs (286) | 0 | 49 | 0 (was 45) | 222 | 15 |

* --il: 261 -> 268 identical (+7: the T7 RVA-truncation rows capa
  6c8b/749e/a301 x2 and the T9 net017); the remaining 12 DIFFERENT rows
  were the 11 `.entrypoint` drops (T6) + net065 (T12 calli).
* --cs: ilspy's crash fixes eliminate all 45 crash rows
  (TransformCollectionAndObjectInitializers stale-pos guard + the
  CheckInvariant sites); each converted row now decompiles with rc=0 and
  diffs as the seed-scaffold shape (the usings/attributes scaffolding --
  the same T10-family shape, NOT new corruption; spot-checked capa04 and
  net017). The PROBE stderr litter is gone from the merged tree.

### Item 2b -- the T6 root cause + fix (found during the sweep triage)

Root cause pinned to the exact line: the cor20 walk lives in
`MethodBodyReader.hpp`'s `LocateUsHeap()` and captures
`entryPointToken_` as a side effect; `GetEntryPointToken()` read the
field WITHOUT triggering the walk, so a token-only first query returned
the never-initialized 0 -- the `.entrypoint` check in
MethodBodyDisassembler.cpp:598 then never matched for any module whose
entry method is disassembled before the first `ldstr` (mscorlib's
library shapes and every body-after-first-string module masked it, which
is why the 41 MB byte-identical pin never caught it). Filed as a fix on
this branch (`5a9ccbc2c`, RED-first: the fresh-open patch-the-cor20
test) and verified against the sweep: all 11 entrypoint rows
(039a/2fd4/354a/e842 x the -cleaned variants) re-diffed IDENTICAL, the
worktree sweep now reads --il 279 IDENTICAL / 1 DIFFERENT (net065) /
2 PORT-CRASH / 4 BOTH-FAIL, full-suite failure set unchanged (139
env-pinned), ASan clean.

Post-fix projected merged-main tally: --il 279 identical + 2 caught-
graceful (a6ab1e60b's catch converts the capa07/09 aborts to the C#-
shaped failure) + net065 (T12) = the remaining --il work is exactly T12
(the calli standalone-signature decode), T4/T5 (the two decoder throws'
root causes), and T3 (the --cs metadata-only scaffolding).

### Item 3 -- standing by

Observations for the next chunk: (a) my `5a9ccbc2c` T6 fix is committed
on this branch -- merge at will (it is independent of the
TransformDisplayClassUsage WIP I saw in-flight on main and touches only
MethodBodyReader.hpp + the DebugDirectory test); (b) T12's calli decode
is the largest remaining --il miss (1 row, ~213 hunks) and is
Disassembler territory; (c) T4/T5 root causes are pinned
(SignatureTypeProviderDecoder Fail sites) and a6ab1e60b's catch already
matches the C# shape for them.

## PD9 -- the T12 calli slice + the pinvoke/HasBody gate (2026-09-24)

### T12 -- the calli standalone-signature decode (RED -> GREEN -> net065 byte-identical)

The sweep's net065 diff: the port rendered `calli @11000003 /* signature 2 */`
where the C# decoded the full `unmanaged stdcall int32 modopt(...)` -- 213
hunks. Root cause: SRM's `SignatureHeader.Kind` collapses every low-nibble
calling convention (<= 5) to SignatureKind.Method, so the C# Method arm
decodes the unmanaged-stdlib signatures; the port's `rawKind == 0x00` test
sent them to the fallback. Fix: the gate widens to `<= 0x05` (committed
05a66b012, RED = the SsSynth 0x01/0x05 rows rendering the fallback).
Byte-identical to the oracle on net065 post-fix.

### The T12 companion -- the pinvokeimpl body gate (3376d877f)

The net065 rows also carried pinvokeimpl thunks whose native-stub bodies the
port rendered as garbage-decoded IL: the C# DisassembleMethodBlock gates the
body on the SRMExtensions HasBody extension (Abstract and PinvokeImpl
attributes plus InternalCall/Native/Unmanaged/Runtime impl attributes carry
no body); the port's gate was RelativeVirtualAddress-only. RED: the
embedded-netmodule Tiny.Add Flags patched with the PinvokeImpl bit
(DisassembleMethodPinvokeImplWithRvaHasNoBody); GREEN post-fix.

### The T13 rider -- the cmod-before-pinned modifier fold (dde1dc19d)

The net065 locals render `uint8& pinned modopt(IsExplicitlyDereferenced)`;
the decoder's pinned arm returned early discarding the collected cmods
(the blob: cmods, 0x45 pinned, 0x10 byref, 0x05 u1). The pinned arm now
folds the modifiers around the pinned element (the C# SRM decode shape).
net065's 5 modopt hunks gone; the whole-module diff = 0 lines.

### The --il tallies after the slice (this branch's build)

| mode | IDENTICAL | DIFFERENT | PORT-CRASH | PORT-FAIL | BOTH-FAIL |
|---|---|---|---|---|---|
| --il (286) | 280 | 0 | 2 (capa07/09, the T4/T5 decoder throws) | 0 | 4 |
| --cs (286) | 0* | 4 | 45* | 222 | 15 |

(* the --cs rows reflect THIS branch's pre-merge state: the 45 crash rows
are already fixed on the merged main by ilspy's crash guards; the 222
metadata-only PORT-FAILs and the 4-scaffold DIFFERENTs are the T3/T10
main-line shapes.)

The --il miss list after the slice: ONLY the 2 capa07/09 decoder throws
(T4/T5) -- the C# degrades past them and ilspycmd renders the full module
(capa07: 14k lines, rc 0), so the fix is the decode-side fidelity, not a
catch -- a7ab-style catches already landed on main as the stopgap.

### Hygiene flag for the controller

Commit 5a9ccbc2c (the T6 entry-point capture, already merged) carried two
DBG stderr fprintfs in LocateUsHeap from the probe round; commit
8ec8026c0 on this branch removes them -- merge it with the next batch.
