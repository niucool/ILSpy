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
