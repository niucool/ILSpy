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
