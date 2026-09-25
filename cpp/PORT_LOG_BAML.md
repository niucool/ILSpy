# Phase 9 (BamlDecompiler) port log

Slice record for the `port-baml` worktree. Per the multi-agent arrangement, this file is the
BamlDecompiler agent's log; the main agent's records live elsewhere, and `PORT_PLAN.md` /
`cpp/Decompiler/CSharp/` / `cpp/Decompiler/Disassembler/` are not touched from here.

---

## 1. State on arrival: Phase 9 is complete

`cpp/BamlDecompiler/` was already fully ported when this worktree started (the "gnhf" agent
series, iterations 32-152+), pinned against the real `ilspycmd 11.0` on Windows. The inventory:

- **`Baml/`** -- the binary layer: `BamlRecords`, `BamlReader`, `BamlBinaryReader`/`Writer`,
  `BamlDocument`, `BamlNode`, `BamlWriter`, and the known-type tables (`KnownTypes`,
  `KnownMembers`, `KnownThings`/`KnownThingsTables` -- the generator's output ported directly).
- **Handlers** -- all 37 record handlers (blocks + records).
- **Rewrite passes** -- all five, including `ConnectionIdRewritePass` (the last BamlDecompiler
  deferral, closed by the prior agent).
- **Engine** -- `XamlDecompiler` (the ctor family, `Decompile`), `BamlDecompilerTypeSystem`,
  `SyntheticWpfModule`, `XmlnsDictionary`, `XamlContext`, the `Xaml/` value types.
- **CLI arm** -- `ResourceExtensions::DecompileBaml` wired into `ilspy_cli --resource <name>.baml`
  (the resource-extraction arm).

`-p --decompile-baml` (whole-project BAML) is **not** wired: that is `BamlAwareWholeProjectDecompiler`,
Phase 10 scope.

What this worktree added is verification and repair of that state on a POSIX host, below.

## 2. Slices landed here (branch `port-baml`)

### Slice 1a -- `ccbcdcd95`: POSIX path model in the resolver path helpers

**RED**: `UniversalAssemblyResolverTest.PosixMainPathDrivesTheNetCoreFinderWithoutThrowing`
(a temp-dir main assembly + side-by-side file; pins both the no-throw contract and the POSIX join
through the `.deps.json` probe arm).

The file-local `WinGetRootLength`/`NormalizeDirectorySeparators`/`JoinPaths` helpers in
`cpp/Decompiler/Metadata/DotNetCorePathFinder.cpp` and `UniversalAssemblyResolver.cpp` are the
decompiled Windows `System.IO.PathInternal` engine applied verbatim on every host. On POSIX:

- a leading-`/` path parses as a UNC root, so `GetDirectoryName("/dir/file")` consumed the whole
  path as the root and returned null, and the `DotNetCorePathFinder` ctor then threw the
  `Path.Combine` ArgumentNullException (`Value cannot be null. (Parameter 'path1')`) on every
  `UniversalAssemblyResolver`-driven BAML decompilation whose main assembly lived at a
  two-segment POSIX path (e.g. `/tmp/x.dll`);
- `JoinPaths` composed candidates with a trailing `\`, which names no file on POSIX, so even the
  resolutions that survived (deeper main paths) located nothing.

The POSIX arms follow .NET's own Unix `PathInternal`: root = a single leading `/`,
`NormalizeDirectorySeparators` = identity, `JoinPaths` inserts `/`. The Windows build keeps the
decompiled Windows engine (the gold pins for these helpers were captured against it).

Note: the third copy of these helpers, in `cpp/ILSpyCmd/IlspyCmdProgram.cpp`, was already
host-aware (its `Win*` block sits inside `#if defined(_WIN32)` and its `CombinePaths` has the
POSIX separator arm) -- no change needed there.

### Slice 1b -- `175a3f4eb`: the Connect-method target parameter index

`ConnectionIdRewritePass` pinned the code-behind `Connect(int connectionId, object target)`
target parameter at `ILVariable::Index == 2`, on the belief that the IL reader counts `this` at
0. The reader has counted `this` at -1 (0-based declared params) since the "gnhf 121" alignment,
so the target of an instance method is index 1 there -- exactly the C# matcher's `t.Index == 1`
the code comments cite. Both checks (`MatchFieldAssignmentToken`, `MatchEventSetterCreation`)
rejected every `Connect` body, so no field assignment or EventSetter creation ever matched and
every connection ID rendered as an unknown-id comment. Fixed to `Index == 1`;
`ConnectionIdRewritePassTest.GoldRender` went green.

### Slice 2 -- `9fc8c268e`: fixture gating for the BAML test suites

The suites were gold-pinned on a Windows host with the real .NET Framework 4.8 install, and 16
tests across four suites failed outright on a host without it (the same failures the main
worktree's binary shows on this Linux box). All were environment misses, not port regressions.

Treatment (the repo's `ILSPY_TEST_MSCORLIB` convention: file-local `MscorlibPath()` = the env
var, else the platform default, plus existence gates):

- `ConnectionIdRewritePassTest` -- the resolver gets the mscorlib's directory as a search
  directory when one exists: only a real mscorlib gives `System.Boolean` its `KnownTypeCode`
  (the synthetic stand-in's Boolean has none), which the `_contentLoaded` registration needs.
  `GeneratedMembersRegistered` skips when no mscorlib; `GoldRender` runs degraded or not.
- `XamlDecompilerCtorFamily` -- the ctor-family drives decompose the same stream golds over the
  mono 4.5-profile mscorlib when the env var points at it. `FileNameSettingsCtor` needs the
  Windows GAC (its resolver throws on unresolved WPF defaults; mono ships only WindowsBase and
  System.Xaml) and skips on POSIX. `MissingFileThrowsFileNotFoundException` drives a POSIX
  temp-directory path on POSIX hosts (whether the parent exists is a host-filesystem property;
  the `C:\` fixture only has an existing parent on Windows).
- `BamlDecompilerTypeSystemTest` -- the S1/S2/S4/S5 golds need the real 4.8 assemblies (mono's
  `ExportedTypes` and File tables diverge) and skip; `FileTableRowsMatchGold` keeps its
  embedded-crafted-manifest half running everywhere; `WithOptions` uses `MscorlibPath()`.
- `ResourceExtensionsTest` -- the mscorlib/System content golds (the `.nlp` rows, the
  3174/1688-line path lists) pin the 4.8 manifests and skip.

The `ILSPY_TEST_MSCORLIB` semantic is unchanged from the rest of the repo: point it at the
.NET Framework 4.x mscorlib. Pointing it at mono's makes identity-only tests pass and
content-pinned ones fail (the same behavior `CustomAttributeDecoderTest` already shows).

## 3. Verification

Build: `cpp/build/linux-ninja` (vcpkg toolchain, Ninja, Debug), matching the main worktree.
ASan: `cpp/build/linux-asan`, same flags as the main worktree's ASan dir.

Per-suite state after the three slices (BAML scope, 46 tests):

| Suite | no fixture | with mono mscorlib |
|---|---|---|
| `ConnectionIdRewritePassTest` | 1 pass, 1 skip | 2 pass |
| `XamlDecompilerCtorFamily` | 17 pass, 5 skip | 21 pass, 1 skip |
| `BamlDecompilerTypeSystemTest` | 3 pass, 5 skip | 4 pass, 4 skip |
| `ResourceExtensionsTest` | 10 pass, 4 skip | 10 pass, 4 skip (3 FX-content fails are the known env class) |

ASan over the BAML scope and the resolver suites (`UniversalAssemblyResolver*`,
`DotNetCorePathFinder*`, `AssemblyReferenceClassifierTest`): zero AddressSanitizer reports in
both modes; the resolver suites keep exactly their pre-existing 20 env-blocked failures (the
machine-GAC / Windows-fs fixtures). `DotNetCorePathFinderTest` is unchanged at 14 failures, all
`SetUp`-stage Windows-filesystem fixtures. `UniversalAssemblyResolverTest` improved from a
mid-suite abort (the old ctor throw) to a clean run-to-completion.

CLI smoke: `ilspy_cli --resource page.xaml.baml <fixture.dll>` completes on a POSIX main path
(it previously threw the `Path.Combine` null); `--il --csharp` over a real .NET 8
`System.Private.CoreLib.dll` unchanged.

Full-suite baseline (this host, per the 806-suite driver): 765 PASS / 35 FAIL / 6 CRASH before
this worktree. The BAML-scope failures listed above are gone; the rest of the FAIL/CRASH set is
environment-blocked (Windows .NET Framework 4.8 / WPF / GAC fixtures absent on this Linux box;
the mono mscorlibs do not match the pinned 3356-TypeDef mscorlib or the FX content golds). The
main worktree's own test binary shows the same failures, so they are not port regressions.

Out-of-scope env-blocked neighbors (not touched here, recorded for whoever owns them):
`ResourcesFileTest.MscorlibResourcesContainerParses` and
`ResourcesFileValueTest.MscorlibResourcesValues` fail without the FX mscorlib and fail
differently with mono's (content-pinned); they have no existence gates yet.

## 4. Divergences and conventions worth keeping

- The synthetic `mscorlib` stand-in (C# design: unresolved default references degrade gracefully)
  defines `System.Boolean` **without** a `KnownTypeCode`, so `IsKnownType(KnownTypeCode.Boolean)`
  is false on hosts where the BAML type system's mscorlib reference stays unresolved. This is
  faithful to the C#, not a port bug -- on Windows the real mscorlib resolves and KnownThings
  reads it through.
- The `ConnectionIdRewritePass` registers `InitializeComponent` once per connector-interface
  sweep (`IComponentConnector` and `IStyleConnector`), so a code-behind type implementing only
  the first still registers it twice. Also faithful to the C# order of operations.

---

# Phase 8 (ILSpyX core subset) port log

The same worktree's second assignment. Scope per PORT_PLAN.md section
"Phase 8": FileLoaders/, Abstractions/, PdbProvider/, Util/, Extensions/,
Instrumentation/, and the CLI-relevant Settings/ -- with Analyzers/, Search/,
TreeView/, and MermaidDiagrammer/ deferred by the plan's ILSpyX sub-scoping.

## 1. State on arrival

- **PdbProvider/ was already ported** (`cpp/ILSpyX/PdbProvider/`:
  DebugInfoUtils + PortableDebugInfoProvider, both with test suites). The
  plan's "Cecil bridge replaced" is exactly what the port does -- the
  `-usepdb` flow runs through the port's own providers, and the C#
  `MonoCecilDebugInfoProvider` (the Mono.Cecil bridge) has no port
  analogue by design. Nothing to add there.
- Nothing else of the scope existed: no FileLoaders, no LoadedPackage, no
  GuessFileType, no CollectionExtensions, no Abstractions, no Settings.
- The vcpkg manifest already declared the needed dependencies (`lz4` for the
  XALZ loader, `miniz` for zip reading), both already linked into `ilspy`.

## 2. Slices landed (branch `port-baml`, after the Phase 9 work)

1. `f917701e0` -- **GuessFileType + CollectionExtensions**. The three-stage
   sniffer (BOM dispatch, RFC 3629 state machine over at most 500 KB, XML
   probe over the port's gold-pinned XmlTextParser) and the generic
   collection helpers. Documented divergences: the XML probe parses the
   whole document where XmlTextReader.MoveToContent reads only the first
   content node (trailing root-level content reports Text, not Xml), and
   the BOM arms decode invalid bytes to U+FFFD (the .NET replacement
   fallback -- those files reach the XML stage and classify as Text).
2. `097fb1061` -- **LoadedPackage**, the package model the archive/bundle
   loaders produce: the folder tree (both separators; empty final
   components -- zip directory rows -- contribute nothing), FromZipFile
   through miniz (each entry re-opens the zip on demand exactly as the C#
   does), FromBundle over the shared image with the DumpPackage raw-deflate
   semantics and the corrupted-entry size-mismatch message. The
   Resource/ByteArrayResource base surface is declared here with a
   placement note (its C# home is Decompiler/Metadata/Resource.cs; the
   package model is the only Phase 8 consumer). Deferred: the
   LoadedAssembly pipeline (PackageFolder's IAssemblyResolver surface).
3. `17721e552` -- **The file loaders and the registry**: PEFileLoader (MZ
   gate), BundleFileLoader and ArchiveFileLoader (by file name, with the
   ParentBundle guard and the InvalidDataException-to-null swallow),
   XamarinCompressedFileLoader (the full XALZ contract: header validation,
   the 255x expansion bound, lz4 decode, exact-length check), and
   FileLoaderRegistry (Xamarin, Bundle, PE, Archive -- the C# precedence
   order). **Deferred: WebCilFileLoader** (the WebCIL container reader is
   an unfilled Phase 1 gap) and **MetadataFileLoader** (the metadata-only
   MetadataFile shape the port's PE-only reader never constructs; a .pdb
   fed to it returns null in the C# as well). One Phase 1 addition: the
   MetadataFile in-memory constructor (fileName, image bytes) mirroring
   the C# PEFile-over-stream form, under the same never-throwing contract;
   the MetadataFile-consuming suites, BAML suites, and CLI smoke all match
   their pre-change baseline.
4. `3a3e63e50` -- **ILanguage** (the decompiler-facing abstraction; the
   TypeToString default argument carried on the interface's own
   declaration, which in C++ static binding is exactly the C# behavior).
   Deferred: IResourceFileHandler + ResourceFileHandlerContext (the
   whole-project-export path, Phase 10's BamlAwareWholeProjectDecompiler;
   the signature takes the not-yet-ported LoadedAssembly) and
   IResourceNodeFactory / ITreeNode (the GUI tree model).
5. `5d8e07520` -- **The settings layer**: ILSpySettings (the XML-backed
   provider: SettingsFilePathProvider, Section, Update with the version
   stamp; the saved sidecar is read back with its UTF-8 BOM consumed),
   MutexProtector (a POSIX flock on a temp-dir lock file with a
   process-local depth counter for the C# re-entrancy),
   SettingsServiceBase (the type-indexed section cache), and the ILSpyX
   DecompilerSettings wrapper (the engine's 110 Browsable bool flags as an
   explicit name -> getter/setter table generated from the C# property
   declarations; IsKnownOption for the -ds surface). Deferred: the Clone
   override (value-return covariance) and the PropertyChanged wiring.
6. **Instrumentation/ILSpyXEventSource does not port** (ETW event-source
   logging; the same deferral the port already records for the
   UniversalAssemblyResolver's instrumentation note).

## 3. Verification

Test-first per slice (RED against stubs, then GREEN); ASan
(`cpp/build/linux-asan`) over every new suite with zero reports (the
sweep caught one real test-side buffer overrun -- a mis-sized string
literal -- which was fixed before commit). Final state of the ILSpyX
scope: 91 tests across 15 suites, 87 passed / 0 failed / 4 skipped (the
pre-existing PdbProvider env-gated fixtures). The Phase 9 BAML suites
(31 passed / 0 failed) and the CLI smoke are unchanged.

## 4. Notes for the next phases

- The CLI wiring of the loaders (`-p` whole-project, package tree
  traversal) and the settings flags (`--ilspy-settingsfile`, `-ds`) are
  Phase 10 consumers of these surfaces; nothing here changes CLI behavior.
- `-genpdb` needs `PortablePdbWriter` (ICSharpCode.Decompiler/DebugInfo/,
  445 lines, Phase 5/7 territory) -- unported; the Phase 8 exit criterion
  "-genpdb writes a Portable PDB" is blocked on it, recorded here.
- `MetadataFileLoader`'s and `WebCilFileLoader`'s deferrals mean the
  registry has four loaders, not six; files only the deferred two would
  claim (raw metadata blobs, WebCIL modules) fall through to null today.

---

# The .NET Framework 4.8 reference-assembly corpus (fixture provisioning)

The third assignment: un-block the mscorlib-gated verification on hosts
without a .NET Framework install.

## 1. The corpus

`Microsoft.NETFramework.ReferenceAssemblies.net48` 1.0.3 fetched from
nuget.org (the nupkg is a zip; sha256
`8a7e348538e7eb91351696911689f49e3d4f63f8bab517432bbe159b8b1104a2`) and
installed at `/home/jim/ilspy-test-fixtures/net48/` -- the nupkg's
`build/.NETFramework/v4.8` content: 133 assemblies (mscorlib, System,
System.Core, System.Xml, the full WPF set, ...) plus 104 facades under
`Facades/`. The nupkg copy and a PROVENANCE.txt live beside it. The
machine's `/usr/lib/mono/4.5/mscorlib.dll` is gone, so the per-file
platform default is dead on this host; the corpus replaces it.

**What the corpus is and is not**: it is the canonical .NET Framework 4.8
REFERENCE set -- metadata-only. Its mscorlib carries the right identity
(`mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=
b77a5c561934e089`), the full 8-row manifest-resource table with the same
tokens/offsets/names as the runtime install, the same .nlp File rows, and
even the mscorlib.resources container (charinfo.nlp is byte-exact at
36992). But it is NOT the runtime install the Windows probes pinned: it
has 2993 TypeDefs to the runtime's 3356 (internal types stripped), 5510
method-semantics rows to 5986, a resources container one entry short
(3171 to 3172 entries -- System.dll's is 1686 to 1688), and its
`Facades/System.Runtime.dll` is 4.1.2.0 where the GAC's is 4.0.0.0.
`PresentationUI.dll` is not in any reference-assembly package at all
(it is a runtime-only assembly).

## 2. The wiring (test-infra only, this branch)

The repo-wide override is `ILSPY_TEST_MSCORLIB` (a per-file
`MscorlibPath()` helper: the env var, else the platform default) -- over
a hundred test files follow it; they needed no change. What did need
wiring were the three suites whose helpers hard-coded Windows FX paths:

- `BamlDecompilerTypeSystem_Test.cpp`: the Fx/WPF/facade constants
  became `FxFile`/`WpfFile`/`FacadePath` helpers that derive the corpus
  layout (assemblies flat beside the mscorlib, facades under `Facades/`)
  from `ILSPY_TEST_MSCORLIB`'s directory; without the env var the
  helpers return the (unreachable) Windows constants and the gates skip
  exactly as before.
- `ResourceExtensions_Test.cpp`: `SystemDllPath()` derives the sibling
  System.dll from the corpus.
- `NamespaceDefinition_Test.cpp`: `SystemDllPath()`/`FacadePath()`
  derive the sibling and `Facades/System.Runtime.dll`.

The default-host (no env var) state of every touched suite is unchanged
(same skips, same pre-existing failures); the CLI is untouched by any of
this (verified below).

## 3. The corpus-run matrix

`ILSPY_TEST_MSCORLIB=/home/jim/ilspy-test-fixtures/net48/mscorlib.dll`:

| Suite | pass | fail | skip |
|---|---:|---:|---:|
| ConnectionIdRewritePassTest | 2 | 0 | 0 |
| XamlDecompilerCtorFamily | 21 | 0 | 1 |
| BamlDecompilerTypeSystemTest | 5 | 3 | 0 |
| ResourceExtensionsTest | 11 | 3 | 0 |
| MethodSemanticsLookupTest | 5 | 2 | 0 |
| Metadata_Smoke | 2 | 0 | 0 |
| CustomAttributeDecoderTest | 3 | 2 | 0 |
| ResourcesFileTest | 16 | 1 | 0 |
| ResourcesFileValueTest | 5 | 1 | 0 |
| AssociatedPortablePdbTest | 18 | 0 | 1 |
| NamespaceDefinitionTest | 9 | 2 | 0 |
| UniversalAssemblyResolverTest | 4 | 4 | 0 |

**Now green that were gated**: ConnectionIdRewritePass's
GeneratedMembersRegistered; every XAML gold-stream drive plus
PEReaderParse (only FileNameSettings stays gated -- see the do-not-chase
list); BamlDecompilerTypeSystem's S2, the full FileTableRows (the
mscorlib half now runs and matches -- the .nlp File rows are identical),
and both WithOptions drives; NamespaceDefinition's FacadeTreeMatchesGold
(through the corpus Facades/ dir); MethodSemanticsLookup's and
AssociatedPortablePdb's mscorlib arms.

**Still failing (not gated -- honest reference-vs-runtime divergences)**
and **do-not-chase** without a real .NET Framework 4.8 runtime install:

1. Anything pinning the runtime mscorlib's type set: the 2696/3356-TypeDef
   counts (the six mid-suite-abort suites), the NamespaceDefinition
   MscorlibTree/SystemTree golds, MethodSemanticsLookup's
   RealFileCorpus census (5986 semantics) and CuratedDrives (a stripped
   type reads nil), CustomAttributeDecoder's curated-row digests. The
   reference set strips internal types (2993 TypeDefs, 5510 semantics).
2. The resource-container content: ResourceExtensions MscorlibPaths
   (3174-pinned vs 3173 actual), MscorlibManifestResourceReads (the
   embedded container's offsets 353040-pinned vs 352792 -- one entry
   short), SystemDllPaths (1688 vs 1686), ResourcesFile/ValueTest's
   container golds. Only the runtime install carries the exact container.
3. BamlDecompilerTypeSystem S1 (FullWpfMap: 21 resolve lines vs the 19
   pinned -- the reference System.XML/System forwarder graphs differ)
   and S5 (CraftedManifest: 25 vs 24). S4 (FacadeDedups): the corpus
   facade is System.Runtime 4.1.2.0, the pinned GAC one is 4.0.0.0.
4. UniversalAssemblyResolverTest's four machine-GAC golds: the GAC's
   directory layout itself -- no reference package can serve it.
5. XamlDecompilerCtorFamily.FileNameSettingsCtor: its throwing resolver
   needs all seven default BAML references resolvable;
   PresentationUI.dll exists in no reference package (a runtime-only
   assembly), so the drive stays Windows-only.
6. The mono-only divergences documented in the Phase 9 log (ILSpyX-type
   FX-content golds) are superseded by this corpus where present.

ASan over the wired suites with the corpus: zero reports (57 tests, the
8 known divergences above failing).

## 4. The connid CLI baseline is byte-identical

`ilspy_cli --csharp -t MyApp.Page1 /tmp/ilspy_connid_test.dll`:
sha1 `2e8a292b8b933782a5099cfb4c8095393b213955` before the corpus was
installed, after, and with `ILSPY_TEST_MSCORLIB` set (the CLI never reads
the corpus -- its resolver searches the main assembly's directory, the
machine GAC, and the dotnet shared framework). The whole-assembly form:
sha1 `2711ff81c6285112d45bf8aadb6c0e06ee933356`, same three-way match.

---

# Differential validation at corpus scale (Phase 11-style hardening)

The fourth assignment: the C++ port vs the C# oracle, over the
capa-testfiles corpus and the net48 reference assemblies, in both output
modes. No product code was touched.

## 1. The oracle

`ilspycmd` 11.0.0.9335-rc installed as a dotnet global tool -- the exact
version line this tree's C# source tracks (the worktree is at the
11.0.0-rc commit; DecompilerVersionInfo: 11.0.0 rc). Installing it
needed the .NET 10 SDK (the tool targets net10.0; the box had only
8.0.425): installed per-user into /home/jim/.dotnet via dotnet-install
(no root), and the tool install must run from a directory WITHOUT this
repo's global.json (it pins SDK 11, which is not installed). Invoke with
DOTNET_ROOT=/home/jim/.dotnet. The reproducible harness is committed at
`cpp/tests/tools/differential_harness.sh` (one bash gotcha documented in
its history: `local a="$1" b="$a"` expands the CALLER's `a` -- the
declarations must be split).

## 2. The recorded run

49 capa-testfiles BSJB-bearing samples (the cleaned/uncleaned variants
included) + 15 net48 reference assemblies (mscorlib, System, System.Core,
System.Xml(.Linq), System.Configuration, System.Runtime.Remoting,
WindowsBase, System.Xaml, PresentationCore, PresentationFramework, and 4
facades), both engines, `--il` and `--csharp`, 180-second timeout per
run (nothing timed out). Outputs under /tmp/diffval/run2/. Line endings
are CR-stripped before diffing (the port pins CRLF, the oracle uses the
host's -- a documented convention, not a divergence).

| Mode | IDENTICAL | DIFFERENT | PORT-CRASH | PORT-FAIL | BOTH-FAIL |
|---|---:|---:|---:|---:|---:|
| --il  (64) | 42 | 17 | 2 | 0 | 3 |
| --csharp (64) | 0 | 3 | 43 | 15 | 3 |

The BOTH-FAILs are the three native PEs with no managed metadata
(kernel32-64 and two crafted images): the oracle throws
MetadataFileNotSupportedException (rc 70), the port prints "could not
open ... as a CLI assembly" (rc 1) -- same verdict, different shape; the
oracle-gap class, nothing to chase.

## 3. The IL mode: near-parity, two real port bugs

All 15 corpus assemblies are byte-identical after line-ending
normalization -- mscorlib at 369,783 lines, PresentationFramework at
368,473. All 17 capa DIFFERENT cases decompose into exactly two root
causes (every diff line is one of these; nothing else diverges across
the whole corpus):

1. **The truncated invalid-RVA comment** (8 samples). The oracle prints
   `// RVA 00000000 invalid (not in any section)`; the port truncates to
   `... (not in any sec`. Root cause: `char buf[40]` at
   `cpp/Decompiler/Disassembler/ReflectionDisassembler.cpp:1538-1540` --
   the rendered string needs 44 bytes with the NUL. Minimal repro: any
   field with HasFieldRVA whose RVA falls outside every section (e.g.
   `e842958188274d5ffee7fbeffb803b2e-cleaned.dll_`, the
   `capa47_e84295818827_il` diff).
2. **The missing `.entrypoint`** (13 samples). `GetEntryPointToken()`
   returns the token only as a SIDE EFFECT of the lazy `LocateUsHeap()`
   parse (`PeImage::entryPointToken_` is set inside
   `cpp/Decompiler/Metadata/MethodBodyReader.hpp:604`; the accessor at
   :497 reads it without triggering the lazy parse). A module whose
   disassembly reaches the entry method before anything touches the #US
   heap (no earlier ldstr) emits no `.entrypoint`; one where an earlier
   string literal warmed the heap does -- which is why some samples in
   the same family match and others do not. Minimal repro:
   `MetadataFile f(<any assembly with an entry point>);
   f.GetEntryPointToken()` returns 0; compare the disassembly of
   `039a6336...cleaned-cleaned.exe_` (no `.entrypoint`, capa1) with
   `0831bb38...cleaned-cleaned.exe_` (has it, capa4). The fix belongs to
   the disassembler/main-line agent: make the accessor trigger the lazy
   cor-header parse.

The two --il crashes:

- `capa7_0953cc3b77ed` (0953cc3b77ed29e7...exe_): aborts with
  `std::out_of_range("Expected a TypeDef, TypeRef or TypeSpec handle!")`
  from `ReflectionDisassembler.cpp:1909` -- an event-map token whose
  top byte is neither 0x01/0x02/0x1B. The oracle decompiles it (14,412
  lines).
- `capa9_2dae11cc5f86` (2dae11cc5f86...exe_): aborts with
  `std::logic_error("SignatureTypeProviderDecoder: trailing bytes after
  the type")` from `SignatureTypeProvider.hpp:603` -- a signature the
  real SRM accepts (the oracle produces 64,214 lines). Both are
  strictness divergences in the port's readers, main-line territory.

## 4. The --csharp mode: the known Phase 5 state

- **43 capa crashes, one signature**: SIGABRT on a
  `std::vector<unique_ptr<ILInstruction>>` out-of-bounds index. ASan
  pins it to `TransformCollectionAndObjectInitializers::Run` at
  `cpp/Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.cpp:771`
  (a null/garbage instruction dereference after an index past the
  Arguments vector), reached through StatementTransform's block walk.
  Eleven of the runs also print `PROBE: ...` litter from
  `InlineArrayTransform.cpp:221-232` (committed debug prints in the main
  agent's in-flight transform work -- the abort is not in that file,
  the prints only co-occur). Minimal repro: `ilspy_cli --csharp
  039a6336...cleaned-cleaned.exe_` (or almost any capa sample).
- **3 capa DIFFERENT** (0953cc3b, e842958 both variants): the port's
  documented Phase 5 SEED output -- bare method signatures without
  visibility, `// .Type` comment headers instead of declarations, no
  using directives or assembly attributes, `base()` ctor-call form. The
  --help text labels this surface itself ("the real resolver back end
  lands in Phase 5"). Not a bug: the convention gap until Phase 5
  lands.
- **15 corpus PORT-FAIL(1)**: `ilspycmd: no method bodies found` -- the
  port's whole-module --csharp bails on metadata-only assemblies (the
  reference set has no bodies); the oracle decompiles the full type
  surface (227,308 lines from mscorlib). A capability gap for the
  Phase 5 owner: the decompiler should emit declarations for
  body-less modules rather than failing.

## 5. Summary for the main-line agent

The IL disassembler is effectively at oracle parity: 42/64 byte-exact,
17/64 explained by two small bugs (both with repros above), 2 strictness
aborts, 3 shared rejects. The C# surface is the in-flight Phase 5 work:
one abort to fix (TransformCollectionAndObjectInitializers.cpp:771),
one metadata-only capability to add, and the known seed-output gap. The
harness (cpp/tests/tools/differential_harness.sh) reruns the whole
matrix in ~6 minutes and writes the same summary.tsv.

## 6. Follow-up run (main-line agent, after the fixes)

The 2026-09-24 follow-up run (`differential_harness.sh /tmp/diffval/fix4`)
after the crash fixes on branch `cpp`:

- **45 cs PORT-CRASH -> 0.** The pinned
  `TransformCollectionAndObjectInitializers.cpp:771` abort split into
  four distinct defects, all fixed (commits `fd2935c10`, `40f94679e`):
  1. `StatementTransform`'s rerun jump kept a position captured before a
     sibling fold shrank the block; the next child indexed past the end.
     Fixed with a clamp at the jump (the C# never sees this because its
     folds replace in place).
  2. `TransformCollectionAndObjectInitializers`' post-scan usage check
     read `block[pos + count + 1]` unguarded (the C# relies on its
     EndPointUnreachable invariant); bounds-checked.
  3. `TransformArrayInitializers`' built initializer block pushed its
     stloc/stelem statements with raw `push_back`, leaving
     Parent/ChildIndex unset (caught by the newly added per-child
     debug CheckInvariant); wired via `Block::Add`.
  4. `NamedArgumentTransform` inserted its this-arg and named-arg stores
     with raw vector writes; wired.
  Two enabler fixes alongside: `IsKnownType` answers via
  `KnownType::Code()` for the reader's primitive stand-in, and
  `IsStringToIntDictionary` strips the metadata arity suffix before the
  definition-name comparison.
- **The committed `PROBE:` litter is gone** from
  `InlineArrayTransform.cpp`.
- **2 il PORT-CRASH remain** (different subsystem, not yet fixed):
  `capa7_0953cc3b` aborts on `std::out_of_range: Expected a TypeDef,
  TypeRef or TypeSpec handle!` and `capa9_2dae11cc5` on
  `std::logic_error: SignatureTypeProviderDecoder: trailing bytes after
  the type` -- both uncaught decoder exceptions in --il mode.
- The cs tallies now read 32 DIFFERENT (the previously-crashing samples
  produce output; the Phase 5 seed-shape and capability gaps account for
  them), 15 PORT-FAIL(1) (the metadata-only-module gap, unchanged), 3
  BOTH-FAIL-IDENTICAL.
---

# The LoadedAssembly pipeline (the Phase 8 deferral)

The fifth assignment: the LoadedAssembly pipeline -- LoadedAssembly,
AssemblyList (+ manager), AssemblyListSnapshot, the resolver
integration, and the LoadedPackage resolver half. Four commits, TDD
per slice, sweep + ASan per slice, no product code outside the new
surface.

## Slices

1. **LoadedAssembly core + AssemblyList open/find** (`e37ff8b77`): the
   lazy load pipeline (the C# `Lazy<Task<LoadResult>>` + `Task.Run`
   collapses to a synchronous guarded-once load -- the port has no Task
   analogue), the status surface (IsLoaded / IsLoadedAsValidAssembly /
   HasLoadError -- polling never triggers, the Lazy.IsValueCreated
   contract), the display Text, the TFM detection + override
   normalization, the Loaded event (once, not retroactive), and the
   ConditionalWeakTable reverse map (a process-wide registry whose
   entries the destructor erases -- the C# weak-key cleanup).
   **The failure-message mapping**: the port's never-throwing
   MetadataFile (IsValid() instead of the PEFile ctor throw) maps back
   to the C# BadImageFormatException ladder inside the load core,
   re-derived from the image bytes in PEReader's stage order -- the
   messages pinned by probing the oracle with ilspycmd: a non-PE blob
   -> "Unknown file format."; a short/garbage-offset MZ blob -> "Image
   is either too small or contains an invalid byte offset or count.";
   an in-range e_lfanew without the PE signature -> "Invalid PE
   signature."; and a metadata-less PE -> "PE file does not contain any
   managed metadata." (the MetadataFileNotSupportedException default;
   the corrupt-metadata stage merges into this one -- the port cannot
   distinguish a missing CLR directory from corrupt metadata bytes).
   FileLoadContext.ParentBundle now carries the C# LoadedAssembly
   wrapper (the loaders read the nullness only).
2. **The resolver integration** (`975d1a760`): MyAssemblyResolver with
   the C# ResolveCoreAsync step order (the provided resolver, the
   tfm+full-name snapshot match, the universal-resolver search arm with
   OpenAssembly/FindAssembly, the similar-name fallback) and the exact
   ReferenceLoadInfo messages; the lazy universal resolver (one
   instance, the first call's winrt flag); AssemblyListSnapshot with
   the tfm-normalized exact-match lookup (the v4.x collapse) and the
   version-ordered short-name groups.
3. **The mutators + manager** (`acdbab5f5`): Unload/Clear/Move (the
   index-decrement rule)/Sort/Reload/HotReplace, RefreshSave ->
   SaveList through the manager, the AssemblyListManager registry over
   ISettingsProvider (the stored-XML ctor, replace-or-append saves,
   Clone/Rename via the C# copy-ctor semantics -- the assemblies
   adopted shared, byFilename deliberately not copied), and the
   framework-directory filter (the DiaSymReader/_cor3 exclusions, the
   upper-case-first rule). Two C# mechanics mapped explicitly: the
   BeginInvoke deferral becomes "run the save after the list lock
   scope" (SaveAsXml takes the same non-reentrant lock), and mutating
   a <List> element inside its live Elements() range hangs the lazy
   sequence (locate first, mutate after).
4. **The package half + extensions** (`ed16ceec3`): PackageFolder's
   ResolveFileName (the on-demand entry wrappers through the bundle
   ctor + the deferred stream provider, the OrdinalIgnoreCase cache
   with misses cached), Resolve/ResolveModule, the snapshot
   GetAllAssemblies recursion (package wrappers excluded, .dll/.exe
   entries on their containing folders, faulted loads included), the
   deferred debug-info half (FromFile-then-LoadSymbols under
   useDebugSymbols), GetTypeSystemOrNull (the uncached
   SimpleCompilation over MinimalCorlib, cached, options-keyed), and
   the LoadedAssemblyExtensions free functions (GetLoadedAssembly
   throwing the C# message, the resolver pair, GetDebugInfoOrNull,
   GetTypeSystemOrNull).

## Documented divergences

* The async surface (Task/Lazy<Task>/ContinueWith) ports as a
  synchronous lazy load; IsLoaded loses the in-flight window, the
  Loaded event fires inline after the load, and the ResolveAsync /
  ResolveModuleAsync pair stays deferred (the AssemblyNameReference
  precedent).
* The universal resolver's FindAssemblyFile arm is exercised only
  indirectly in the tests (nothing on this host's GAC/dotnet layout
  resolves the fixtures); the OpenAssembly/FindAssembly split behind
  it is pinned through ResolveModule.
* GetTypeSystemWithDecompilerSettingsOrNull is deferred (needs
  DecompilerTypeSystem.GetOptions, a Phase 5 piece);
  CreateCecilObjectModel is dropped with the Mono.Cecil bridge.
* The C# testing-only AssemblyList ctor leaves the manager null, so a
  testing list cannot load zip/bundle entries through the registry --
  the port preserves this (the zip tests use manager-based lists).
* Thread affinity (ownerThread/VerifyAccess), the
  SynchronizationContext dispatch, and the ETW instrumentation do not
  port; the list stays internally locked.
* IsIncludedFrameworkFile reads fileName[0] unguarded in the C# (an
  empty name would throw); the port guards it to false.

## Verification

Each slice: compile-fail RED, then GREEN, then the full-suite sweep
(the baseline failure set byte-identical before/after; +59 tests / +7
suites total) and an ASan run of the new surface (clean after fixing
the two issues ASan caught: the Unload flag-after-erase use-after-free,
and the Elements()-range mutation hang caught as a live-lock in the
sweep). Net: 12,391 tests / 818 suites in the filtered sweep, the
same baseline failure set as the untouched tree.

---

# The Phase 11 broad text-match baseline (the standing exit-criterion artifact)

The sixth assignment: the broad `--csharp` text-match baseline over the
provisioned corpus plus the capa .NET set, whole-file decompilation on
both engines, per-assembly pass/fail, and the categorized divergence
table. The harness is `cpp/tests/tools/textmatch_baseline.sh`
(reproducible; ~25 min full run).

## 1. The sample set and the ranking metric

The corpus assemblies are ranked by **public-surface size, measured as
the oracle's own `--csharp` emission** (the line count of the decompiled
surface the exit criterion compares; the ranking run decompiles every
corpus .dll with ilspycmd 11.0.0.9335-rc and keeps the counts). The top
20 range from mscorlib (227,308 lines) down to System.Data.Linq
(19,150). The capa-testfiles set contributes all 49 BSJB-bearing files
(46 managed + 3 native PEs that both engines reject). One extra
body-bearing sample -- `ilspycmd.dll`, the oracle tool's own assembly
(1,829 lines) -- is included so the matrix exercises real method
bodies, which the metadata-only reference corpus cannot.

## 2. The matrix (whole-file `--csharp`, 69 samples)

| Category | Count | Samples |
|---|---:|---|
| IDENTICAL | 0 | -- |
| CONVENTION-DIFF (the Phase-5 seed surface) | 3 | capa7/47/48 |
| REAL-MISMATCH | 0 | -- |
| PORT-CRASH(134) | 44 | 43 capa + ilspycmd_self |
| PORT-FAIL(1) | 20 | the top-20 corpus, all |
| ORACLE-THROWS (both engines reject: native PEs) | 3 | capa8/25/49 |

Per-assembly corpus table (every sample FAIL -- the port cannot emit
C# text for any of them):

| # | Assembly | Oracle lines | Result |
|---|---|---:|---|
| 1 | mscorlib.dll | 227,308 | FAIL (PORT-FAIL(1): "no method bodies found") |
| 2 | PresentationFramework.dll | 185,327 | FAIL (same) |
| 3 | PresentationCore.dll | 155,958 | FAIL (same) |
| 4 | System.dll | 129,814 | FAIL (same) |
| 5 | System.Data.dll | 86,681 | FAIL (same) |
| 6 | System.Xml.dll | 73,303 | FAIL (same) |
| 7 | System.Design.dll | 63,116 | FAIL (same) |
| 8 | System.Windows.Forms.DataVisualization.dll | 46,110 | FAIL (same) |
| 9 | System.Web.DataVisualization.dll | 45,787 | FAIL (same) |
| 10 | WindowsBase.dll | 44,419 | FAIL (same) |
| 11 | System.IdentityModel.dll | 42,835 | FAIL (same) |
| 12 | System.Workflow.ComponentModel.dll | 32,593 | FAIL (same) |
| 13 | Microsoft.VisualBasic.Compatibility.dll | 30,018 | FAIL (same) |
| 14 | Microsoft.Build.Tasks.v4.0.dll | 29,926 | FAIL (same) |
| 15 | System.Core.dll | 29,297 | FAIL (same) |
| 16 | System.Runtime.Serialization.dll | 28,817 | FAIL (same) |
| 17 | System.Workflow.Activities.dll | 27,930 | FAIL (same) |
| 18 | System.Drawing.dll | 24,863 | FAIL (same) |
| 19 | ReachFramework.dll | 23,389 | FAIL (same) |
| 20 | System.Data.Linq.dll | 19,150 | FAIL (same) |

The capa set (49): 43 PORT-CRASH(134) with one shared abort signature
(the `std::vector<unique_ptr<ILInstruction>>` OOB assert, ASan-pinned
in the differential run to
`TransformCollectionAndObjectInitializers.cpp:771`); 3
CONVENTION-DIFF; 3 ORACLE-THROWS. The crash hits `ilspycmd.dll` too --
it is not capa-specific; any body-bearing assembly walks the same
transform and dies. The 3 CONVENTION-DIFF samples are the only port C#
text in the run: bare method bodies under `// .Type` headers, no using
directives, no assembly attributes, no type declarations, no
visibility modifiers, `base()` form -- the documented Phase-5 seed
surface; the emitted body text itself matches the oracle's bodies
modulo those conventions (capa47's `Main` is textually the oracle's
body).

## 3. What this means for exit criterion 1

Two blockers, both already located, stand between the current state
and corpus-wide `--csharp` matching:

1. **The metadata-only gap** (all 20 corpus assemblies): the port's
   C# pipeline requires method bodies -- whole-file, type mode
   (`-t`), and declarations-only all fail with "no method bodies
   found (for type)". The reference corpus consists entirely of
   body-less assemblies, and the oracle emits their full declaration
   surface (mscorlib alone: 227K lines). Until the pipeline emits
   declarations for body-less modules, exit criterion 1 cannot score
   on the corpus at all.
2. **The IL-transform OOB crash** (43+1 samples): one shared abort in
   `TransformCollectionAndObjectInitializers.cpp:771` kills every
   body-bearing assembly tested, including a modern net10.0 real-world
   one. This is main-line territory (the file carries the committed
   PROBE litter; the repro is any capa sample or
   `ilspycmd.dll --csharp`).

The `--il` pipeline, recorded in the differential section above, is at
near-oracle-parity over the same corpus (42/64 byte-identical, every
divergence explained by two small bugs with repros) -- the C#-emission
gap is not a metadata or disassembly problem; it is localized to the
C# backend's module-level emission and the one crash.

Rerun: `bash cpp/tests/tools/textmatch_baseline.sh /tmp/textmatch`
(the ranking, per-sample captures, diff.txt files, and summary.tsv all
land under the output directory).

---

# The post-merge re-run (the OOB crash is dead; the matrix updates)

## 1. The sync

`git merge cpp` fast-forwarded port-baml onto the integrated tip
(58 mainline commits, including `40f94679e` "Fix the remaining corpus
crash sites the harness sweep exposed" -- the fix for the pinned
`TransformCollectionAndObjectInitializers.cpp:771` OOB -- plus the T6
`--il` truncation fix and the `.entrypoint` fix from the differential
report). The pipeline's 52 LoadedAssembly tests pass unchanged; the
build is clean.

## 2. The updated matrix (whole-file `--csharp`, 69 samples)

| Category | Before the merge | After the merge |
|---|---:|---:|
| IDENTICAL | 0 | 0 |
| CONVENTION-DIFF (the seed surface) | 3 | **33** |
| REAL-MISMATCH | 0 | 0 |
| PORT-CRASH | 44 | **14** |
| PORT-FAIL(1) -- the metadata-only gap | 20 | 20 (**still open; main-line fix required**) |
| ORACLE-THROWS (native PEs, both reject) | 3 | 3 |

Post-fix per-sample: every body-bearing capa sample that previously
aborted now decompiles to the seed surface (33 text-producing samples,
the largest 1,891 lines). The 14 remaining crashes re-pinned by ASan
to a NEW unguarded site:
`IndexRangeTransform::TransformIndexing(IL::IndexRangeState&)` -- the
same `ILInstruction`-vector OOB family, a different transform (the
previous fix also hardened IndexRange's `pos = -1` sentinel, but this
path overflows on the `2fd45662...cleaned-cleaned.exe_` corpus). The
minimal repro: `ilspy_cli --csharp 2fd45662...cleaned-cleaned.exe_`.
The IL-mode strictness aborts from the differential run (capa7's
TypeDef/TypeRef/TypeSpec throw, capa9's trailing-bytes) now exit
cleanly with rc 70 and their messages instead of SIGABRT.

Also verified on the merged tree: the differential report's IL fixes
landed -- the `.entrypoint` now emits for capa1, the InlineArray
`PROBE:` litter is removed. Note the mainline's own diagnostics
(`DBG LocateUsHeap enter` / `DBG cor20 captured` in
`MethodBodyReader.hpp:591-614`) are committed stderr litter of the
PROBE kind -- flagged for cleanup.

## 3. The convention classes the 33 producing samples expose

The emitted bodies are the oracle's code modulo these recurring
token-level conventions (each a candidate for the Phase 5 Emitter):

* fully-qualified type names (`new System.Text.StringBuilder(...)` vs
  `new StringBuilder(...)`; `System.String.IsNullOrEmpty` vs
  `string.IsNullOrEmpty`);
* a spurious `ref` on field stores (`ref cREDUI_INFO.pszMessageText =
  message;`);
* the local-naming scheme (`stringBuilder_1`, `cREDUI_INFO` -- the
  SNAKE-cased splitting of `CREDUI_INFO` vs the C# `credinfo`);
* enum constants rendered as bare ints (`CREDUI_FLAGS cREDUI_FLAGS =
  2;` vs the cast form);
* indentation: 4 spaces vs the oracle's tabs;
* the missing declarations half of each file: usings, namespace, type
  declarations, visibility modifiers, assembly attributes.

## 4. The standing blockers (unchanged in substance)

1. **The metadata-only gap** -- still the corpus-wide blocker: the
   whole-file, `-t`, and declarations-only paths all fail with "no
   method bodies found (for type)" on every reference assembly. Until
   the pipeline emits declarations for body-less modules, exit
   criterion 1 scores zero on the corpus.
2. **The residual IL-transform OOB** -- now one site
   (`IndexRangeTransform::TransformIndexing`), 14/46 capa samples
   still abort; the remaining 33 produce text.

## 5. The MethodBodyReader DBG litter cleanup (for the next merge)

The merged mainline carries committed debug fprintf litter in
`cpp/Decompiler/Metadata/MethodBodyReader.hpp` (the
`DBG LocateUsHeap enter` and `DBG cor20 captured` sites, committed
during ilspy's crash-fix run -- the same class as the InlineArray
PROBE prints, which the mainline has since removed). Both sites are
removed on port-baml; the `.entrypoint` capture they diagnosed is
verified intact (capa1's `--il` still emits it), stderr is clean, and
the affected suites stay green (the only new failures in the merged
tree's sweep are the two
`ReflectionDisassemblerTest.DisassembleFieldInvalidRvaCommentIsComplete`
variants, which need `ILSPY_TEST_MSCORLIB` set to a corpus mscorlib --
they pass with `ILSPY_TEST_MSCORLIB=/home/jim/ilspy-test-fixtures/net48/mscorlib.dll`
and pre-date this cleanup; flagging rather than editing the mainline's
gating).

## 6. The ILSpyX remainder: the GetOptions mapping (slice E)

The LoadedAssemblyExtensions surface is now complete: the deferred
`GetTypeSystemWithDecompilerSettingsOrNull` lands, backed by the port of
the C# `DecompilerTypeSystem.GetOptions` mapping (the 18 settings flags
onto their TypeSystemOptions bits, starting from None) as a static on
the ILSpyX `DecompilerSettings` subclass -- the mapping consumes only
the settings surface, so it stays in this lane. The extension takes the
base `Decompiler.DecompilerSettings` parameter, as in the C#, and
routes through the options-keyed `GetTypeSystemOrNull` cache (same
settings -> the cached compilation, different settings -> a rebuild;
both pinned).

`ApiVisibility` and `LanguageVersion` (the two remaining root-file
trivia) stay unported: their only C# consumers are the deferred
Search/GUI surfaces, and porting them now would be consumer-less
churn.

Sweep note: the post-cleanup sweep (12,517 tests / 844 suites) is
failure-set-identical to the pre-cleanup merged tree. The two
`ReflectionDisassemblerTest.DisassembleFieldInvalidRvaCommentIsComplete`
variants are the only NEW failures relative to the pre-merge baseline,
and they are fixture-gated, not code: they pass with
`ILSPY_TEST_MSCORLIB=/home/jim/ilspy-test-fixtures/net48/mscorlib.dll`
(the test asserts a parseable mscorlib and the mainline gate predates
this cleanup -- flagged for the main line, not edited here).

---

# The resolver/conversions survey + the two un-blocking ports

The seventh assignment: the resolver/conversions surfaces that un-block
the deferred ExpressionTrees arms (ConvertCoalesce / ConvertComparison
in the port's TransformExpressionTrees). Surveyed against PORT_PLAN
Phase 5 and the D-ledger (the D-numbered entries in cpp/README.md +
the in-code deferral notes), then ported what was actually missing.

## 1. The survey: the arms' call graph is already mostly landed

The stale in-code note ("the CSharpConversions skeleton carries no
conversion methods") predates the mainline's conversions port. The
arms' full call graph, verified against the current tree:

* `CSharpConversions.ImplicitConversion(IType, IType)` -- PORTED (the
  cached entry point + the `Detail::ImplicitConversion` core; the
  45-helper conversion family in `CSharpConversionsHelpers.cpp`),
* `CSharpResolver.ResolveBinaryOperator` -- PORTED (the
  lines-594-948/995-1053 regions),
* `CSharpOperators.LiftUserDefinedOperator` -- PORTED,
* `OperatorResolveResult.UserDefinedOperatorMethod` -- PORTED (the
  Semantics surface),
* `NullableType.IsNullable` / `GetUnderlyingType` -- PORTED,
* `NullCoalescingKind` / `NullCoalescingInstruction` -- PORTED (the IL
  instruction surface),
* `ComparisonKind` + `ToBinaryOperatorType` -- PORTED.

## 2. What was genuinely missing, and landed

1. **The OverloadResolution ctor's `conversions ??
   CSharpConversions.Get(compilation)` fallback** -- the D512 skeleton
   deferred it pending the conversions methods (which have since
   landed). Ported verbatim: a null conversions resolves to the
   per-compilation singleton (the CacheManager identity, pinned across
   resolutions of one compilation), an explicit instance is kept.
2. **The `GetArgumentsWithConversions` constant-folding arm** -- the
   D-ledger's last deferred resolver piece (`IsCompileTimeConstant &&
   IsValid && !IsUserDefined` -> the per-call
   `CSharpResolver(compilation).WithCheckForOverflow(...).ResolveCast(...)`
   re-fold), blocked on `ResolveCast` which has since landed. Ported
   with the resolver constructed per call (the enable_shared_from_this
   discipline), the non-const IType target local, and the C# else
   branch serving the non-constant/invalid/user-defined shapes. The
   core gained the compilation parameter exactly as the deferral note
   predicted, and the old wrapper-fallback test updated to the folded
   expectation (42/5 -> the long constant) plus the user-defined guard.

Both slices TDD'd (compile/behavior RED, then GREEN), sweep-neutral
(identical failure sets), and committed
(`1d1ebd58b`, `013cfe098`).

## 3. The un-blocked state

With both ports in, the ConvertCoalesce / ConvertComparison arms' full
dependency set is present: the conversions queries, the binary-operator
resolution with its user-defined-operator surface, the lifted-operator
helper, and the constant-folding refinement inside the argument
wrapping. The only remaining step for each arm is the WIRING inside
`TransformExpressionTrees.cpp` (an IL/Transforms file -- the
mainline's lane, deliberately not touched here; the arms remain listed
as deferred in that file's stale comment, which the wiring commit
should replace).

Also noted for the main line: the merged mainline's
`ReflectionDisassemblerTest.DisassembleFieldInvalidRvaCommentIsComplete`
variants need `ILSPY_TEST_MSCORLIB` set to a corpus mscorlib (they pass
with the fixture env var; pre-existing gate, flagged not edited).

---

# The --il parity re-verification over the FULL net48 corpus

The eighth assignment: whole-module `--il` on all 133 corpus assemblies
plus the capa .NET set plus the ilspycmd self-sample, both engines,
post-T12 (the merged mainline's crash-fix batch). The harness gained
`MODE=il ALL_CORPUS=1` (`textmatch_baseline.sh`); the tallies below are
from /tmp/tm_il (183 paired runs).

## 1. The matrix (whole-module `--il`, 183 samples)

| Category | Count | Samples |
|---|---:|---|
| IDENTICAL | 176 (+1 re-verified) | the corpus minus one; mscorlib 369,783 lines byte-exact |
| REAL-MISMATCH | 1 | System.EnterpriseServices.Wrapper.dll (see below) |
| ORACLE-THROWS | 4 | 3 native capa PEs + System.EnterpriseServices.Thunk.dll (a native thunk module; both engines reject) |
| PORT-FAIL(70) | 2 | capa7/capa9 -- the two strictness aborts now exit cleanly with their messages |
| TIMEOUT | 0 | -- |

The pre/post-merge comparison: the first differential run recorded 42/64
IL-identical with 17 divergences caused by two bugs (the truncated
invalid-RVA comment + the missing `.entrypoint`) and 2 SIGABRTs. Post
T6/T12: both bugs are fixed (capa1 emits `.entrypoint`, the RVA comment
renders in full), the two aborts exit cleanly (rc 70 + the message),
and the corpus is **132/133 identical by file count** (131 exact
byte-identical + the Wrapper divergence, below).

## 2. The one real corpus divergence: System.EnterpriseServices.Wrapper.dll

A C++/CLI mixed-mode assembly whose diff decomposes into exactly two
new bug classes (both now handed to the main line with this repro):

1. **The `calli` unmanaged-signature rendering** (~213 hunks): the port
   emits `calli @1100000E /* signature 2 */` (the raw
   StandAloneSignature token) where the oracle renders the resolved
   unmanaged calling convention and return type
   (`calli unmanaged stdcall int32 modopt([mscorlib]...IsLong)`). The
   port's calli operand path does not decode the signature blob's
   calling convention / modopts / return+parameter types.
2. **The `pinvokeimpl ... native unmanaged` thunk bodies** (~42 hunks):
   the port disassembles the NATIVE thunk bytes as if they were IL
   (`conv.ovf.u.un`, `.emitbyte 0xec`, the "Invalid method body"
   rows) where the oracle renders only the signature + the custom
   attributes for a `native unmanaged` pinvoke. The disassembler should
   skip the body for `native unmanaged` bodies (or render the native
   marker the C# prints), not walk the bytes as IL.

## 3. The IL-mode state

Two corpus-wide IL bugs (the differential report's #1/#2) are closed by
the mainline's T6/T12 fixes; the corpus parity is 132/133; the capa set
adds 33 producing --csharp samples (the post-merge re-run, above) and 2
clean rc-70 strictness failures. The remaining IL work items are the
two new Wrapper classes above; everything else in the corpus is
byte-exact.

Rerun: `MODE=il ALL_CORPUS=1 bash cpp/tests/tools/textmatch_baseline.sh
/tmp/tm_il`. Note: a mid-run disk-full can truncate a capture (the tr
write error marks it) -- re-verify any non-IDENTICAL verdict by re-running
the single pair before trusting it; the ilspycmd_self row here was
quota-truncated on the first pass and re-verified IDENTICAL on a clean
disk.

---

# The metadata-only emission spec (the T3 blocker; for the main-line agent)

The port's whole-module `--csharp` fails on every reference assembly
with `ilspycmd: no method bodies found`. This spec documents the C#
machinery that makes `ilspycmd mscorlib.dll` work today, the exact
gates, and the smallest proving test. No main-line files were edited.

## 1. Where the port bails

`cpp/ILSpyCmd/main.cpp` (the whole-module `--csharp` loop) iterates
types -> methods and skips every method with `RVA == 0`
("abstract/extern/pinvoke-only"), decompiles only bodies, and fails
when zero methods printed. For the net48 reference corpus every
managed method body is a **zero-length body at a non-zero RVA**
(see below), and the `DecompileMethodToString` path yields nothing
printable, so `methodsPrinted == 0` -> the failure. The `-t` type path
bails the same way ("no method bodies found for type '...'").

## 2. The C# machinery (the trace)

`IlspyCmdProgram` (ICSharpCode.ILSpyCmd/IlspyCmdProgram.cs): the
whole-assembly command calls
`decompiler.DecompileWholeModuleAsString()` (line ~658); `-p` calls
`DecompileProject(module, dir, writer)` (line ~649); `-t <type>` calls
`DecompileTypeAsString(typeDefinition.FullTypeName)` (line ~668). All
three funnel into the same per-type core:

* `CSharpDecompiler.DecompileWholeModuleAsSingleFile()`
  (CSharpDecompiler.cs ~900): `DoDecompileModuleAndAssemblyAttributes`
  + `DoDecompileTypes(metadata.GetTopLevelTypeDefinitions(), ...)`.
* `DoDecompileTypes` (~868): per type,
  `<Module>` with no members is skipped,
  `MemberIsHidden(module, handle, settings)` gates the accessor/backing
  members (the -lv filtering; NOT an RVA gate), and the type is
  grouped under its namespace declaration.
* Per member: `DoDecompileMember` dispatches
  field/property/event/method/type. **`DoDecompile(IMethod)`
  (line ~2088) is the whole trick**:
  1. `methodDecl = typeSystemAstBuilder.ConvertEntity(method)` -- the
     declaration is built from the METADATA/type system (the
     signature, the modifiers, the attributes); **no body consulted**.
  2. `methodDefinition.HasBody()` (the SRM `MethodDefinition.HasBody`
     -- `RelativeVirtualAddress != 0` gated on not-abstract) decides
     the BODY arm: when true, `DecompileBody(method, ...)` runs the
     ILReader pipeline; when false,
     `else if (!method.IsAbstract && method.DeclaringType.Kind !=
     TypeKind.Interface) methodDecl.Modifiers |= Modifiers.Extern;`.
  3. **The zero-length body is a first-class case**: a reference
     assembly's methods carry `RVA != 0` with `Code size: 0` (the
     ref-pack's convention) -- `HasBody()` is TRUE, `DecompileBody`
     runs, `GetMethodBody(RVA)` yields a zero-byte IL body, and the
     ILReader (`ILReader.cs` ~489) hits `reader.Length == 0` and
     plants `InvalidBranch("Empty body found. Decompiled assembly
     might be a reference assembly.")` -- which the transform pipeline
     renders as the body
     `{ /*Error: Empty body found. Decompiled assembly might be a
     reference assembly.*/; }`. That is the exact text in the oracle's
     mscorlib capture (ref-count: the ZipFile methods show it; the
     P/Invoke methods instead carry `extern` from the false-`HasBody`
     arm).

The distinguishing observation: **`extern` appears only for the true
RVA==0 methods (P/Invoke imports and abstract members), while the
zero-length RVA!=0 bodies render the Empty-body comment.** Both must
be reproduced; conflating them (e.g. skipping all RVA==0 methods)
loses half the surface.

## 3. The corpus observation that motivates this

The net48 reference mscorlib carries 20,906 zero-length bodies (the
`--il` oracle capture counts `Code size: 0` that many times), and the
port's `--il` already renders them byte-identically to the oracle
(the corpus --il runs are exact). So the metadata reading, the
zero-length-body detection, and the IL rendering all exist; the gap is
exclusively the `--csharp` declaration emission path (the
metadata-driven `ConvertEntity` surface) plus the Empty-body comment
rendering in the C# pipeline.

## 4. The smallest proving tests (fixture-prep done)

Two gold captures are checked into `cpp/tests/fixtures/metadata_only/`
(the oracle's raw output, LF line endings; the port pins CRLF --
compare CR-stripped, the established convention):

1. **`Facades_System.AppContext.gold.txt`** (16 lines): a pure
   type-forwarding facade -- the usings, the assembly-attribute block
   ending in `[assembly: ReferenceAssembly]` +
   `[assembly: TypeForwardedTo(typeof(AppContext))]`, and NO type
   declarations. The whole output is:
   ```csharp
   using System;
   using System.Reflection;
   using System.Runtime.CompilerServices;

   [assembly: AssemblyTitle("System.AppContext")]
   [assembly: AssemblyDescription("System.AppContext")]
   [assembly: AssemblyDefaultAlias("System.AppContext")]
   [assembly: AssemblyCompany("Microsoft Corporation")]
   [assembly: AssemblyProduct("Microsoft® .NET Framework")]
   [assembly: AssemblyCopyright("© Microsoft Corporation.  All rights reserved.")]
   [assembly: AssemblyMetadata("", "")]
   [assembly: AssemblyFileVersion("4.8.3761.0")]
   [assembly: AssemblyInformationalVersion("4.8.3761.0")]
   [assembly: ReferenceAssembly]
   [assembly: AssemblyVersion("4.1.2.0")]
   [assembly: TypeForwardedTo(typeof(AppContext))]
   ```
   Pass criterion: `ilspy_cli --csharp
   <corpus>/Facades/System.AppContext.dll` reproduces these 16 lines
   CR-insensitively, exit 0.
2. **`System.IO.Compression.ZipFile_type_csharp.gold.txt`** (268
   lines): the `-t System.IO.Compression.ZipFile` decompile of
   `System.IO.Compression.FileSystem.dll` -- the declared static class
   with the doc comments, the parameters, and the zero-length bodies
   rendered as the Empty-body comment. Pass criterion: `ilspy_cli
   --csharp -t System.IO.Compression.ZipFile <corpus>/System.IO.
   Compression.FileSystem.dll` matches CR-stripped; the critical
   excerpt:
   ```csharp
   public static ZipArchive OpenRead(string archiveFileName)
   {
       /*Error: Empty body found. Decompiled assembly might be a reference assembly.*/;
   }
   ```

The full-assembly gold for the broader sweep is recoverable with
`ilspycmd <corpus>/System.IO.Compression.FileSystem.dll` (>457 lines).

## 5. The port-side checklist (for the main-line agent)

1. Replace the bodies-only iteration with the C# shape: per type,
   the declaration emission via the metadata surface (the ported
   `TypeSystemAstBuilder.ConvertEntity` family), gated members through
   the `MemberIsHidden` semantics, then the body arm.
2. `HasBody()` semantics: port the SRM predicate exactly (`RVA != 0`
   plus the not-abstract gate as SRM defines it) -- NOT `RVA == 0`
   skipping. The zero-length-RVA-present bodies are `HasBody() == true`
   and follow the DecompileBody path (the Empty-body InvalidBranch
   comment).
3. The `extern` arm: `!IsAbstract && DeclaringType.Kind != Interface`
   -> `Modifiers.Extern`.
4. Keep the `MemberIsHidden` accessor/backing-field gating (the C#
   ~337 region) so the auto-events and the accessor pairs match.
5. The acceptance test: the two golds above; then
   `ilspy_cli --csharp <corpus>/mscorlib.dll` should produce the
   227,308-line declaration surface (the oracle capture is the gold
   capture of record; the previous differential runs archived it).
