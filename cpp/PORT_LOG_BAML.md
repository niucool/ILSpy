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
