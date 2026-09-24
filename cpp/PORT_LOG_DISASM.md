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
