# ILSpy C++ Port Plan

Companion to `MIRROR_LAYOUT_REORGANIZATION.md`. The mirror doc fixes the *how*: basename
matching, directory mirroring, the C++17/CMake/vcpkg toolchain, and the C#-to-C++ construct
mappings. This document fixes the *what and in what order*: what we are porting, the dependency
order it must be built in, the hard problems that will bite, and a phase plan with exit criteria and
tests for each milestone.

Everything here targets the **core components and CLI** scope the mirror doc already narrows to.
GUI, AddIns, Installer, ReadyToRun, and PowerShell are out.

---

## 1. Scope

### In scope (four C# projects)

| C# project | Role | .cs files | lines |
|---|---|---:|---:|
| `ICSharpCode.Decompiler` | The engine: metadata, type system, ILAst, transforms, C# AST, resolver, output | 579 | ~168,800 |
| `ICSharpCode.ILSpyX` | Shared, UI-host-agnostic support (file loaders, PDB, abstractions) | 89 | ~12,900 |
| `ICSharpCode.BamlDecompiler` | BAML to XAML decompiler | 70 | ~9,500 |
| `ICSharpCode.ILSpyCmd` | `ilspycmd` CLI front-end | 9 | ~2,200 |

Total in scope: ~193,000 lines of C#, of which a meaningful slice is **generated** (see 5.2).

### Out of scope (explicitly excluded)

`ILSpy/` (Avalonia UI), `ILSpy.Tests/`, `ILSpy.Tests.Windows/`, `ILSpy.AddIn*`, `ILSpy.Installer`,
`ICSharpCode.Decompiler.PowerShell`, `ICSharpCode.ReadyToRun`, and the .NET test projects
(`ICSharpCode.Decompiler.Tests`, `ICSharpCode.BamlDecompiler.Tests`, `ICSharpCode.ILSpyCmd.Tests`).
Their *behaviour* is preserved as Google Test suites under `cpp/tests/`, but the .NET test projects
themselves are not ported. `TestFixtures.Resources/` and `ILSpy-tests/` are reused as input fixtures
only (see 11).

### ILSpyX sub-scoping

The CLI imports only `ICSharpCode.ILSpyX.PdbProvider` and `ICSharpCode.ILSpyX.MermaidDiagrammer`
from ILSpyX. The rest of ILSpyX is GUI-oriented (Analyzers, Search, TreeView) or packaging. For the
CLI port we take the **core subset**: `FileLoaders`, `Abstractions`, `PdbProvider`, `Util`,
`Extensions`, `Instrumentation`, and `Settings` (the small settings needed by `--ilspy-settingsfile`
and `--decompiler-setting`). We **defer** `Analyzers`, `Search`, `TreeView`, and
`MermaidDiagrammer` (the diagrammer is an optional CLI flag; re-add in a later phase if needed).
`System.Composition` (MEF) is used only by the deferred Analyzers, so it is dropped entirely for the
CLI target.

---

## 2. Scale and reality check

This is not a thin port. Three numbers make the point:

1. The **metadata layer** is a thin wrapper over `System.Reflection.Metadata` (SRM), the BCL's
   mature ECMA-335 reader. C++ has no equivalent on vcpkg, but `microsoft/winmd` (MIT, header-only
   C++17, the reader behind C++/WinRT) already provides the PE container, the complete ECMA-335
   table set, coded indices, signature decoders, and custom-attribute decoding. We adopt it as the
   baseline (see 5.1) and fill the gaps it leaves -- method-body/IL reading, Portable PDB debug
   tables, WebCIL, single-file bundles, and the ILSpy `MetadataFile`/`PEFile` ergonomics adapter.
   This de-risks the single largest sub-project considerably, though the gaps are still real work.

2. The engine **embeds a complete C# semantic engine** -- name lookup, overload resolution, type
   inference, conversions, nullability -- and re-resolves its own output while emitting it. That is
   the `CSharp/Resolver` and `Semantics` subsystems, ~25,000 lines, and it is the second-hardest part
   (see 5.3).

3. Roughly **200 IL instruction classes** and a large slice of the **C# AST node hierarchy** are
   *generated* (T4 template + a Roslyn source generator). The generation machinery does not exist
   in C++; we must decide a codegen strategy (see 5.2) and port the *output* either way.

The honest read: this is a multi-engineer, multi-quarter effort, not a weekend. The plan below is
phased so that each milestone is independently useful and testable, and so the riskiest pieces
(metadata reader, codegen, resolver) are confronted first rather than discovered late.

---

## 3. Architecture recap (the pipeline we must reproduce)

Taken from `doc/DecompilerArchitecture.html`. The engine has three stages and two intermediate
representations:

```
Assembly (PE/WebCIL/bundle)
  -> Metadata layer        (MetadataFile / PEFile, over SRM)         [Phase 1]
  -> Type system           (DecompilerTypeSystem, MetadataModule)    [Phase 2]
  -- per method: IL bytes + generic context --
  -> ILReader + BlockBuilder   (decode IL, simulate eval stack)      [Phase 3]
  -> ILAst                (ILFunction: typed instruction tree)        [Phase 3]
  -> ~40 IL transforms    (state machines, loops, sugar, ...)        [Phase 4]
  -> Statement/Expression/CallBuilder  (resolver-checked)            [Phase 5]
  -> C# AST (SyntaxTree)                                          [Phase 5]
  -> ~15 AST transforms (prettification)                          [Phase 5]
  -> OutputVisitor -> ITextOutput                                 [Phase 5]

Parallel back end (shares only ITextOutput):
  ReflectionDisassembler / Disassembler  (metadata + IL to text)     [Phase 6]

Orchestrator: CSharpDecompiler (one instance = one assembly + type system + settings)  [Phase 7]
```

Key invariants we must preserve in C++:
- The ILAst is a **strict tree** (a child belongs to exactly one parent); branch targets and
  variables are *references*, not children.
- Children live in **typed slots** with `SlotInfo` policy (`CanInlineInto`, etc.).
- Every node knows its `ResultType` (StackType lattice) and `InstructionFlags` (MayThrow,
  SideEffect, ControlFlow, ...), computed bottom-up and invalidated up the parent chain.
- Every node carries its `ILRange` provenance, surviving to the C# AST (sequence points,
  navigation).
- `BlockContainer`s are single-entry regions; `Branch` targets current-or-enclosing container,
  `Leave` exits a named container; no fall-through.
- `DecompilerSettings` exposes ~150 feature flags; `SetLanguageVersion` switches them in blocks
  (C# 1 through C# 15). Transforms whose flag is off simply do nothing.
- Robustness: unverifiable IL becomes `InvalidBranch`/`InvalidExpression` with warnings; a failed
  symbolic analysis throws internally and leaves the method lower-level rather than aborting.

---

## 4. Dependency graph and porting order

Build order is forced: nothing above the metadata layer compiles without it, and the back end
cannot be written before the front end produces an ILAst.

```
Util (primitives)                              Phase 0
  |
  v
Metadata + ECMA-335 reader                     Phase 1   <-- foundation, hardest #1
  |
  v
Type system + Semantics                        Phase 2
  |
  +-----------------------------+
  |                             |
  v                             v
ILAst model + front end         Disassembler    Phase 3 / 6
  |                             (needs only Metadata+TypeSystem)
  v
IL transform pipeline (~40)     Phase 4   <-- bulk of IL
  |
  v
C# AST + resolver + back end    Phase 5   <-- hardest #2 (resolver)
  |
  v
Decompiler orchestration        Phase 7   (CSharpDecompiler, settings, whole-module)
  |
  +-------------------+
  |                   |
  v                   v
ILSpyX core subset   BamlDecompiler   Phase 8 / 9
  |                   |
  +---------+---------+
            |
            v
        ilspycmd CLI                Phase 10
            |
            v
      Integration + hardening       Phase 11
```

The Disassembler (Phase 6) is parked here after Phase 2 for sequencing, but it is small and could
be pulled forward as an early "the metadata reader works" milestone.

---

## 5. Top porting risks and decisions

### 5.1 No System.Reflection.Metadata equivalent -- adopt microsoft/winmd as the baseline

The metadata layer is the dominant risk and (even with a baseline) the largest single sub-project.
The C# `Metadata/` layer (7,700 lines) is a thin wrapper over `System.Reflection.Metadata` (SRM), the
BCL's mature ECMA-335 reader; what we are really replacing is the engine *under* that surface.

Baseline: **adopt `microsoft/winmd`** (https://github.com/microsoft/winmd) -- MIT-licensed,
header-only C++17, the ECMA-335 reader that powers C++/WinRT. Inspected against this repo's needs,
it already provides:

- A **PE container reader** (`pe.h`: DOS/optional/file headers, section table, RVA->offset) -- so
  `pe-parse` is no longer needed.
- The **complete ECMA-335 table set** (TypeDef, TypeRef, TypeSpec, MethodDef, MemberRef, Field,
  Param, InterfaceImpl, Constant, CustomAttribute, FieldMarshal, DeclSecurity, ClassLayout,
  FieldLayout, StandAloneSig, EventMap, Event, PropertyMap, Property, MethodSemantics,
  MethodImpl, Module, ModuleRef, ImplMap, FieldRVA, Assembly, AssemblyProcessor, AssemblyOS,
  AssemblyRef, AssemblyRefProcessor, AssemblyRefOS, File, ExportedType, ManifestResource,
  NestedClass, GenericParam, MethodSpec, GenericParamConstraint).
- **Coded-index** machinery with bit-width constants (TypeDefOrRef, HasConstant, HasCustomAttribute,
  HasFieldMarshal, HasDeclSecurity, MemberRefParent, HasSemantics, MethodDefOrRef, MemberForwarded,
  Implementation, CustomAttributeType, ...).
- **Signature decoders** (`signature.h`: FieldSig, MethodDefSig, PropertySig, TypeSpecSig, TypeSig
  with ELEMENT_TYPE_*, custom modifiers, SZArray/Array/Ptr, generic instantiation, byref).
- **Custom-attribute** decoding (`custom_attribute.h`).
- Cross-platform file I/O (`mmap` on POSIX, `MapViewOfFile` on Windows); uses `<regex>` itself.

Gaps we must fill on top of winmd (localized additions, not a rewrite):

- **Method-body / IL reading**: winmd targets WinMD (metadata-only) files, so it omits method
  bodies. Add fat/tiny method-header parsing, IL-byte slicing by `MethodDef.RVA` (winmd already
  has section/RVA->offset helpers), local-variable signatures, and exception-region tables. This is
  the input to Phase 3's ILReader.
- **Portable PDB debug tables**: winmd has none. Extend its table enum/schema with the debug tables
  (Module/Document/MethodDebugInformation/LocalScope/LocalVariable/LocalConstant/ImportScope/
  StateMachineMethod/CustomDebugInformation). Needed by `-usepdb` (Phase 8) and `-genpdb` writes
  (Phase 7, deflate via miniz -- see 5.8).
- **WebCIL** and **single-file bundle** extraction (uses miniz/lz4 -- see 5.8).
- **`.deps.json`/`.runtimeconfig.json`** parsing for `UniversalAssemblyResolver`/
  `DotNetCorePathFinder` (nlohmann-json replaces the vendored LightJson).
- The **ILSpy ergonomics adapter**: port the C# `Metadata/` surface (`MetadataFile`, `PEFile`,
  `WebCilFile`, `MetadataExtensions`, `MetadataTokenHelpers`, `MetadataGenericContext`, handle
  types, `CustomAttributeDecoder`, `FindTypeDecoder`, `FullTypeNameSignatureDecoder`, ...) on top
  of winmd's row/view types, exposing the API the type system, IL reader, and back end consume.

Layout: vendor winmd's ~19 headers under `cpp/Decompiler/Metadata/Ecma335/winmd/` (C++-only, MIT
attribution preserved), with the ported `Metadata/` ergonomics layer above it. This mirrors the
repo's existing vendoring practice (Humanizer, LightJson). vcpkg has no `winmd` port (it ships via
NuGet), so vendoring is the only option; pin the upstream commit in
`cpp/Decompiler/Metadata/Ecma335/winmd/README`.

Alternatives considered:
- **`Team-RTCLI/RTCLI.Runtime`** (https://github.com/Team-RTCLI/RTCLI.Runtime): MIT, C++17, a full
  .NET runtime (VM/GC/heap). Inspected: ~2,100 lines, **no metadata reader, no PE parser, no
  ECMA-335 decoder at all** (only an opcode table). Not useful as a baseline.
- **Hand-write from scratch**: rejected given winmd exists and is MIT; would duplicate ~5,800 lines
  of mature table/signature/coded-index machinery.

Decision: **adopt `microsoft/winmd` as the ECMA-335 baseline; drop `pe-parse`** (winmd reads the PE
container). Fill the method-body, debug-table, WebCIL, bundle, resolver, and ergonomics gaps on
op. This phase still gets the most test coverage (see Phase 1 exit criteria).

### 5.2 Generated code -- IL instructions and the C# AST

Two generation mechanisms feed the C# source:

- `IL/Instructions.tt` (T4) -> `IL/Instructions.cs` (9,096 lines): ~200 IL instruction classes with
  uniform constructors, typed child slots, `InstructionFlags` computation, visitor methods,
  `WriteTo` dumping, and `Match...` structural-match helpers.
- `Metadata/ILOpCodes.tt` -> `Metadata/ILOpCodes.cs` (32 lines): the opcode enum/table.
- `DecompilerSyntaxTreeGenerator` (a Roslyn **source generator**) emits a large slice of
  `CSharp/Syntax/` (the `AstNode` hierarchy: Expression/Statement/TypeMember/GeneralScope nodes,
  their typed slots, child accessors, visitor/`Role` machinery, pattern-match helpers). The
  generated portion lives inside the 15,500-line `CSharp/Syntax` subtree.

In C++ there is no Roslyn source generator and T4 does not run. Strategy:

- **Recommended for the initial port: port the generated *output*, not the generators.** Treat
  `Instructions.cs` and the generator-emitted `CSharp/Syntax` files as the source of truth and
  hand-translate them to `.hpp`/`.cpp` pairs (merging partials per the mirror doc's rule 2). Keep
  the slot/flag/visitor/`Match` machinery uniform *by hand*. This is the fastest path to a working
  engine and is correct.
- **Recommended for long-term maintainability: introduce a lightweight build-time code generator**
  (a Python or CMake-time script) consuming a declarative node spec (slots, flags, result type,
  children) and emitting the C++ instruction/AST classes, mirroring what T4 and the Roslyn
  generator do. This restores the "one spec, many nodes" property that makes ~200 nodes
  maintainable and keeps the port in sync if the C# generators evolve. Land this after Phase 5 is
  green, not before.
- The 32-line `ILOpCodes.cs` is trivial; port by hand.

Decision: start with the port-the-output approach; revisit codegen after the back end works.

### 5.3 The embedded C# semantic engine (resolver)

`CSharp/Resolver` (~14k) plus `Semantics` (~2.3k) implement real C# semantics: member lookup,
overload resolution, type inference, conversions, lift operators, nullable tracking, tuple/dynamic
handling. The back end (`ExpressionBuilder`, `StatementBuilder`, `CallBuilder`) emits a cast or
qualifier only when the resolver proves omitting it would change meaning. This is intrinsic to the
"round-trip correctness" design tenet and cannot be skipped or stubbed. It is the second-hardest
sub-project. It depends on a complete type system (Phase 2). Port it faithfully and test it
aggressively against the C# engine's own resolver fixtures.

### 5.4 Immutable collections

`System.Collections.Immutable` is pervasive (immutable stacks in the IL reader, immutable arrays
of children, `ImmutableStack<ILVariable>`, etc.). There is no vcpkg equivalent.

Decision: do not seek a general immutable-collections library. Adopt a small, fixed set of
conventions:
- Use `std::vector` for arrays; treat as immutable-by-convention after construction where the C#
  used `ImmutableArray`. Copy-on-write is not needed for correctness (the trees are strict, and
  children are set once).
- Write a tiny `ImmutableStack<T>` (a `std::shared_ptr`-linked cons-list) for the few spots that
  genuinely need structural sharing (the IL reader's `currentStack`). `std::stack` is not a
  substitute because the reader snapshots and branches on the stack.
- `std::unordered_map` / `std::map` for dictionaries; `std::optional` for nullable returns.
- Document the convention in `cpp/README` so every phase applies it uniformly.

### 5.5 Nullable reference types

The C# is compiled with `WarningsAsErrors=nullable` and annotates the whole codebase. C++ has no
equivalent.

Decision: adopt a written convention, enforced in review:
- Ownership: `std::unique_ptr` for exclusive ownership, `std::shared_ptr` only where cycles are
  provably absent and sharing is real (rare), raw `T*`/`T&` for non-owning observation (parents,
  references). Never `new`/`delete` directly.
- Nullability: `std::optional<T>` for value-like nullable returns; raw `T*` may be null only where
  documented; references `T&` are never null.
- The `CheckInvariant` debug machinery (per-transform invariant checks) is ported verbatim and run
  in debug builds exactly as in C#; it is the safety net for the loss of NRT checking.

### 5.6 String and text encoding

.NET strings are UTF-16; ECMA-335 `#Strings` is UTF-8, `#US` is UTF-16LE; output is text.

Decision: **UTF-8 (`std::string`) everywhere internally.** Convert at the boundaries: decode `#US`
UTF-16LE to UTF-8 on read, encode identifiers and string literals back to the output representation
on write. Use a small UTF-8/UTF-16 conversion helper in `cpp/Util`. This keeps the cross-platform
Linux target simple. Avoid `std::u16string` except at the exact conversion boundary if needed.

### 5.7 PDB / debug info

The CLI's `-genpdb`/`-usepdb` route through `ICSharpCode.ILSpyX/PdbProvider`, which bridges to
**Mono.Cecil's** PDB readers (`MonoCecilDebugInfoProvider`). The core `ICSharpCode.Decompiler/DebugInfo`
(984 lines) wraps SRM's portable-PDB readers instead. In C++ we have neither SRM nor Cecil.

Decision:
- **Portable PDB** is just ECMA-335 metadata with the debug tables; extend the winmd-based Phase 1
  reader with the debug-table set (see 5.1). This supports `-usepdb` variable names and sequence
  points, and `-genpdb` writes (deflate via miniz, see 5.8).
- **Windows PDB** (the legacy PDB format) is a separate binary format needing its own reader. This
  is a real sub-component. Options: depend on LLVM's pdbutils/DIA, or write a minimal reader, or
  scope Windows-PDB support out of the initial port and support Portable PDB only. Recommend:
  **Portable PDB only for the initial port**, document the Windows-PDB gap, revisit in hardening.

### 5.8 Compression (single-file bundles, WebCIL, PDB)

Audit of the in-scope code confirms three compression uses; **no brotli** anywhere:

- **LZ4** for Xamarin-compressed assemblies (`XamarinCompressedFileLoader.cs`) and .NET 6+
  single-file bundles that carry a `CompressedSize` (`SingleFileBundle.cs` v6+). Add the vcpkg
  `lz4` port (target `unofficial-lz4`). Required if Xamarin/compressed-bundle paths stay in scope
  (both are in the ILSpyX core subset); optional if those two are deferred.
- **Deflate** for `-genpdb` Portable PDB writes (`PortablePdbWriter.cs`, `CompressionLevel.Optimal`)
  and for `-d`/`--dump-package` + `LoadedPackage` reads (`IlspyCmdProgram.cs`, `LoadedPackage.cs`).
  Use **miniz** (vcpkg) instead of zlib: miniz is a small permissive (public-domain/UNLICENSE)
  zlib-compatible implementation that covers deflate/inflate with compression levels, which is
  exactly the surface needed. It keeps the dependency footprint minimal.
- **Brotli**: confirmed unused in the in-scope projects; not added.

### 5.9 MEF / System.Composition -- drop for CLI

MEF is used only by the deferred `Analyzers` (the `[Export]` attributes are all in
`Analyzers/Builtin/`). The CLI does not compose analyzers. Drop `System.Composition` for the CLI
target. If analyzers are ever restored, decide a C++ plugin mechanism then.

### 5.10 CLI parsing and update checker -- replace

- `McMaster.Extensions.Hosting.CommandLine` (CLI verbs/options) -> replace with **cxxopts** (vcpkg).
  The `ilspycmd` option set is flat (~40 options, no real subcommands -- the diagrammer options are
  prefix-grouped, not subcommanded), and cxxopts handles everything it needs: multiple-value
  options (`-l`, `-r`, `-ds`), optional-value options (`-usepdb` via `implicit_value`), and no-value
  flags. The custom validation in `ValidationAttributes.cs` is hand-rolled either way (no library
  reproduces C# attribute-style validation). cxxopts is chosen over CLI11 for being lighter and
  sufficient; CLI11 remains a viable swap if config-file/subcommand needs emerge later.
- `NuGet.Protocol` powers the dotnet-tool update check (`DotNetToolUpdateChecker`). It is optional
  (`--disable-updatecheck`). **Drop it for the initial port**; the CLI starts with update-check
  disabled and a stub.

### 5.11 Humanizer

`Humanizer/` (406 lines) is vendored third-party (string pluralization, used for naming). It is
vendored C# with its own LICENSE. Port the small subset actually used, or vendor a C++ equivalent,
keeping the LICENSE note per the repo's vendored-code rule.

### 5.12 Regex

`System.Text.RegularExpressions` is used in **core** paths, not just the deferred diagrammer:
`Metadata/DotNetCorePathFinderExtensions.cs` and `Metadata/UniversalAssemblyResolver.cs` (Phase 1,
path/version patterns), `IL/Transforms/LocalFunctionDecompiler.cs` (Phase 4, the
`<name>g__handler|n` local-function-name pattern), and `CSharp/Transforms/PatternStatementTransform.cs`
plus `Humanizer/Vocabulary.cs` (Phase 5). The patterns are simple (capture groups + `IgnoreCase`,
matched once per name), so `std::regex` is adequate. (The winmd baseline also uses `std::regex`
internally, which makes the choice consistent.) RE2 is faster and ReDoS-safe but the patterns do not
backreference, so its advantages don't apply here.

Decision: **use `std::regex`**; add no regex library. If a fixture later needs RE2's guarantees,
revisit per-pattern, not globally.

---

## 6. Third-party library reconciliation (vs the mirror doc table)

The mirror doc lists four vcpkg libraries (pe-parse, nlohmann-json, plog, gtest). The C# reality
differs: `pe-parse` is replaced by the winmd baseline (which reads the PE container itself);
`plog` is kept, and several others are added. Reconciled table:

| Need | C# source | C++ choice | In mirror doc? |
|---|---|---|---|
| PE container + ECMA-335 metadata + signatures + coded indices + custom attributes | `System.Reflection.PortableExecutable` + `System.Reflection.Metadata` | **microsoft/winmd** (vendored, MIT; see 5.1) + method-body/debug-table/WebCIL/bundle/ergonomics on top | replaces `pe-parse` |
| Immutable stacks/arrays | `System.Collections.Immutable` | conventions + tiny `ImmutableStack` (5.4) | n/a (no lib) |
| JSON | `.deps.json`/`.runtimeconfig.json` + `--dump-table --json` | **nlohmann-json** (vcpkg) | yes |
| Logging | (none; C# uses `Debug.WriteLine`) | **plog** (vcpkg) | yes |
| LZ4 decompression | `K4os.Compression.LZ4` | **lz4** (vcpkg, `unofficial-lz4`) | NO -- add (5.8) |
| Deflate compress/decompress | `System.IO.Compression.DeflateStream` | **miniz** (vcpkg) | NO -- add (5.8) |
| CLI option parsing | `McMaster.Extensions.Hosting.CommandLine` | **cxxopts** (vcpkg) | NO -- add (5.10) |
| Regex | `System.Text.RegularExpressions` | **std::regex** (no dep; 5.12) | NO -- no dep |
| PDB (portable) | SRM debug tables | winmd debug-table extension (5.1) | n/a |
| PDB (Windows legacy) | Mono.Cecil.Pdb | deferred / LLVM or hand-written | n/a |
| Tests | xUnit/NUnit | **gtest** (vcpkg) | yes |

Net vcpkg dependencies (final): `nlohmann-json`, `plog`, `lz4`, `miniz`, `cxxopts`,
`gtest`. Plus the vendored `microsoft/winmd` headers (not a vcpkg package). `pe-parse`,
`brotli`, `zlib`, `re2`, and `cli11` are all **not** used.

Note: during initial bring-up, `miniz` was made **optional** (it is unused until
Phase 7/8's deflate features) and moved behind a `deflate` vcpkg manifest feature,
because the vcpkg miniz source download hit an environment SSL/proxy error. It
becomes a hard dependency again when the first deflate call site lands. See
decision D12; the implemented `cpp/CMakeLists.txt` and `cpp/vcpkg.json` are
authoritative where they differ from the CMake sketch in section 8 (e.g.
`find_package(lz4)` + `lz4::lz4`, not `unofficial-lz4`).

---

## 7. C++ directory layout under `cpp/`

Mirror rule: C++ directories **exactly mirror** C# project folder structure, with `ICSharpCode.`
stripped and namespaces mapped to subdirectories. Proposed root:

```
cpp/
  CMakeLists.txt                 # top-level, adds subdirectories
  vcpkg.json                     # manifest: nlohmann-json, lz4, miniz, cxxopts, gtest
  README                         # build/run/conv conventions (immutable, ownership, encoding)
  Decompiler/                    # <- ICSharpCode.Decompiler
    Metadata/                    #   ported Metadata surface (MetadataFile/PEFile/...)
      Ecma335/                   #   (C++-only) SRM replacement
        winmd/                   #     vendored microsoft/winmd headers (MIT; pinned commit)
    TypeSystem/Implementation/   #   mirror subfolders exactly
    IL/ControlFlow/ IL/Instructions/ IL/Patterns/ IL/Transforms/
    CSharp/OutputVisitor/ CSharp/ProjectDecompiler/ CSharp/Resolver/
    CSharp/Syntax/{Expressions,GeneralScope,PatternMatching,Statements,TypeMembers}/
    CSharp/Transforms/ CSharp/TypeSystem/
    DebugInfo/ DebugSteps/ Disassembler/ Documentation/ FlowAnalysis/
    Humanizer/ IL/ Instrumentation/ Output/ Semantics/ Solution/ Util/
    Properties/                  #   header/version info only
  ILSpyX/                        # <- ICSharpCode.ILSpyX (core subset only)
    Abstractions/ FileLoaders/ PdbProvider/ Util/ Extensions/ Instrumentation/ Settings/
    (Analyzers/ Search/ TreeView/ MermaidDiagrammer/ deferred)
  BamlDecompiler/                # <- ICSharpCode.BamlDecompiler
    Baml/ Handlers/{Blocks,Records}/ Rewrite/ Xaml/ Properties/
  ILSpyCmd/                      # <- ICSharpCode.ILSpyCmd
    (AsContainer/ holds Dockerfiles; not C# -- skip)
  tests/                         # gtest, mirrors source dirs per mirror doc test rule
    Decompiler/Metadata/PE_Test.cpp   # e.g. for Decompiler/Metadata/PE.cpp
    ...
```

`cpp/Decompiler/Metadata/Ecma335/` is **C++-only** (no C# counterpart); per the mirror doc's status
legend it is marked `C++-only` and placed logically under `Metadata/` because it is the SRM
replacement that `Metadata/` wraps. The vendored `winmd/` headers live under it with their MIT
notice and a pinned-commit note; the method-body/debug-table/WebCIL/bundle extensions live alongside
them as C++-only additions.

File naming follows the mirror doc: every C# `Foo.cs` -> `Foo.hpp` + `Foo.cpp` with identical
basename; C# partial classes merge into one pair (rule 2).

---

## 8. Build system

Per the mirror doc: CMake >= 3.15, C++17, vcpkg toolchain. Three targets:

- `ilspy` -- the core static library (`ilspy.lib` / `libilspy.a`), aggregating `Decompiler/` +
  `ILSpyX/` core + `BamlDecompiler/`.
- `ilspy_cli` -- the `ilspycmd` executable, linking `ilspy` + cxxopts.
- `ilspy_tests` -- the gtest executable.

CMake sketch (extends the mirror doc example):

```cmake
cmake_minimum_required(VERSION 3.15)
project(ilspy VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vcpkg manifest mode; deps declared in vcpkg.json
find_package(nlohmann_json CONFIG REQUIRED)
find_package(plog CONFIG REQUIRED)                # logging (5.10)
find_package(unofficial-lz4 CONFIG REQUIRED)   # LZ4 (5.8)
find_package(miniz CONFIG REQUIRED)             # deflate (5.8)
find_package(cxxopts CONFIG REQUIRED)           # CLI option parsing (5.10)
find_package(GTest CONFIG REQUIRED)
# microsoft/winmd is vendored under Decompiler/Metadata/Ecma335/winmd/, not a vcpkg package.
# No pe-parse, brotli, zlib, re2, or cli11.

add_subdirectory(Decompiler)
add_subdirectory(ILSpyX)
add_subdirectory(BamlDecompiler)
add_subdirectory(ILSpyCmd)
add_subdirectory(tests)

add_library(ilspy STATIC ...)          # aggregate core objects
add_executable(ilspy_cli ...)
add_executable(ilspy_tests ...)
target_link_libraries(ilspy_tests PRIVATE ilspy GTest::gtest)
enable_testing()
add_test(NAME ilspy_tests COMMAND ilspy_tests)
```

Warnings: compile with `-Wall -Wextra` (GCC/Clang) and `/W4` (MSVC); do **not** set
`-Werror` initially (the C# uses `WarningsAsErrors=nullable`, which has no C++ analogue). Revisit
`-Werror` for `ilspy` only after Phase 11.

---

## 9. Conventions: C# to C++ mapping

Extends the mirror doc's "Key Migration Considerations" with project-specific decisions:

| C# | C++ | Notes |
|---|---|---|
| `namespace A.B.C` | `namespace A::B::C { ... }` | directory mirrors namespace |
| `partial class` | single merged class in one `.hpp/.cpp` | mirror rule 2 |
| properties | `const T& GetX()` / `void SetX(T)` | |
| events / delegates | `std::function` callbacks | |
| LINQ | STL algorithms / range-for | avoid allocations in hot paths |
| `async/await` | callbacks (no coroutines initially) | the CLI is synchronous anyway |
| `List<T>`, `Dictionary<K,V>` | `std::vector`, `std::unordered_map` | |
| `ImmutableArray<T>` | `std::vector<T>` (immutable-by-convention) | 5.4 |
| `ImmutableStack<T>` | tiny cons-list with `shared_ptr` | 5.4 |
| `Nullable<T>` / `T?` | `std::optional<T>` | |
| `object`, `dynamic`, `variant` unions | `std::variant` | |
| `IDisposable` / `using` | RAII (destructors) | |
| `IDisposable`+`try/finally` | RAII + `try/catch` | |
| interfaces | abstract base + pure virtual | |
| generics | templates | |
| extension methods | free functions in the type's namespace | |
| `System.String` (UTF-16) | `std::string` UTF-8 | 5.6 |
| `Span<T>`/`Memory<T>` | `std::span<T>` (C++20) or pointer+length | C++17 has no `span`; add a tiny `span` polyfill or use `string_view`/`(ptr,len)` |
| `InterningProvider` | ported interner | type system uses it; port faithfully |
| `CheckInvariant` debug | ported, run in `#ifndef NDEBUG` after every transform | the safety net for lost NRT checking |

`std::span` is C++20; since we target C++17, vendor a minimal `span` polyfill in `cpp/Util` (or
accept C++20 for `span` only -- not allowed, we are C++17). Use the polyfill.

---

## 10. Porting phases

Each phase has: scope, dependency, deliverable, exit criteria, and tests. The TDD rule from
`CLAUDE.md` applies: write the failing test, show red, implement, show green. Never skip red.

### Phase 0 -- Scaffolding and primitives

Scope: `cpp/` skeleton, CMake/vcpkg, `Util/` primitives (UTF-8/16 conversion, `span` polyfill,
`ImmutableStack`, small `Optional`/`Result` ergonomics if desired), logging via plog, JSON via
nlohmann, the C#-to-C++ convention README.

Dependency: none.

Deliverable: empty `ilspy`/`ilspy_cli`/`ilspy_tests` targets build; a `Util` unit test passes.

Exit criteria: `cmake --build build` succeeds on Windows (MSVC) and Linux (GCC/Clang);
`ilspy_tests` runs and `Util_*` tests pass.

Tests: `tests/Util/...` for the primitives.

### Phase 1 -- Metadata layer on the winmd baseline (foundation)

Scope: vendor `microsoft/winmd` under `Metadata/Ecma335/winmd/`; then port the C# `Metadata/`
ergonomics surface on top and fill the gaps (see 5.1): method-body/IL reading (fat/tiny headers,
IL bytes by RVA, local-var sigs, exception regions), Portable PDB debug tables, `WebCilFile`,
`SingleFileBundle` (LZ4), `PEFile`/`MetadataFile`, `UniversalAssemblyResolver` +
`DotNetCorePathFinder` (`.deps.json` via nlohmann-json), `MetadataExtensions`,
`MetadataTokenHelpers`, `MetadataGenericContext`, `MemberReferenceMetadata`,
`TypeReferenceMetadata`, `ExportedTypeMetadata`, `ModuleReferenceMetadata`, `AssemblyReferences`,
`CustomAttributeDecoder`, `FindTypeDecoder`, `FullTypeNameSignatureDecoder`,
`MethodSemanticsLookup`, `PropertyAndEventBackingFieldLookup`, `Resource`, `SignatureBlobComparer`,
`ReferenceLoadInfo`, `UnresolvedAssemblyNameReference`, `CodeMappingInfo`,
`EnumUnderlyingTypeResolveException`, plus `ILOpCodes` (the 32-line enum) and `OperandType`.

Dependency: Phase 0.

Deliverable: load a `.dll`/`.exe`, read PE + metadata, enumerate TypeDef/MethodDef/Field/Property,
decode method bodies by RVA, decode signatures, resolve assembly references (framework search),
decode custom attributes, handle single-file bundles (LZ4) and WebCIL.

Exit criteria:
- Parse `mscorlib`/`System.Runtime` and dump correct table row counts and selected signatures that
  match the C# engine's `--dump-table` output byte-for-byte.
- Round-trip a `TypeDef`/`MethodDef`/`Field` signature to the ECMA-335 text form and compare.
- A bundled single-file app loads and its embedded assemblies enumerate.
- Method bodies (fat + tiny) decode to the same IL bytes the C# `MethodBodyBlock` exposes.
- Portable PDB debug tables decode and yield the same variable names/sequence points as the C#
  `-usepdb` path.
- The reader handles malformed/obfuscated input gracefully (invalid blobs -> warnings, not
  crashes), matching the robustness tenet.

Tests: `tests/Decompiler/Metadata/` -- PE header parse (via winmd's reader), each table decoder,
each signature decoder, coded-index resolution, heap decoders, method-body decoding, debug-table
decoding, bundle extraction, reference resolution. Still the heaviest test investment, but the
winmd baseline covers the table/signature/coded-index machinery, so tests focus on the gap
additions and the ergonomics adapter rather than re-verifying ECMA-335 fundamentals.

### Phase 2 -- Type system and semantics

Scope: `TypeSystem/` (all 106 files, including `Implementation/`), `Semantics/` (27 files),
`KnownTypeReference`, `FullTypeName`, `AssemblyQualifiedTypeName`, `ApplyAttributeTypeVisitor`,
`MetadataModule`, `DecompilerTypeSystem`, interning (`IInterningProvider`), accessibility,
`ComHelper`, `InheritanceHelper`, `UsefulReturnType`/flow bits as needed.

Dependency: Phase 1.

Deliverable: a resolved, semantic view over a main module and its references; resolve a type by
name, walk members, read attributes, handle type forwarders and implicit references, honor
`TypeSystemOptions`.

Exit criteria: build a `DecompilerTypeSystem` over `System.Runtime` and a sample assembly; resolve
every `TypeRef` in the sample; member lookup returns the same entities the C# engine resolves.

Tests: `tests/Decompiler/TypeSystem/`, `tests/Decompiler/Semantics/`.

### Phase 3 -- ILAst instruction model and front end

Scope: `IL/` instruction model first: `ILInstruction`, the ~200 instruction classes (port
`Instructions.cs` per 5.2), `SlotInfo`, `InstructionFlags`, `ILVariable`, `ILFunction`,
`Block`/`BlockContainer`, `StackType`, `ILRange`, `ValidateChild`/`CheckInvariant`. Then the front
end: `ILReader` (stack simulation, worklist import, stack-type merging, union-find slot merge),
`BlockBuilder` (container structure, `CreateBlocks`, `ConnectBranches`), `ILParser`,
`ILTransformContext`. Defer the ~40 transforms to Phase 4.

Dependency: Phase 2.

Deliverable: a method body decodes to an `ILFunction` with nested containers, typed instruction
trees, stack-slot variables, explicit control flow (no fall-through), and a passing
`CheckInvariant`.

Exit criteria:
- `Greet`-class method (the architecture doc's running example) decodes to the documented ILAst
  shape (two `stloc S_0` arms, merged join, `leave` of the main container).
- A debug build runs `CheckInvariant` after the reader and passes.
- Dumping the ILAst text matches the C# `--il`/ILAst output for a set of methods.

Tests: `tests/Decompiler/IL/` -- per-instruction `Match...` helpers, reader stack simulation,
block builder nesting, invariant checks.

### Phase 4 -- IL transform pipeline (~40 transforms)

Scope: `IL/Transforms/` -- `SplitVariables`, `ILInlining`, `BlockILTransform`/`StatementTransform`
drivers (dominator-tree post-order), `LoopDetection`, `ConditionDetection`,
`AsyncAwaitDecompiler`, iterator state machines, `DelegateConstruction`, `LocalFunctionDecompiler`,
`TransformExpressionTrees`, `LockTransform`, `UsingTransform`, `ForeachTransform`, string
interpolation, deconstruction, pattern matching (`is`), `null`-conditional, tuple sugar, and the
rest. `FlowAnalysis/` and the symbolic analysis that can throw
(`SymbolicAnalysisFailedException`) and leave the method lower-level.

Dependency: Phase 3.

Deliverable: the full ordered pipeline raises an ILAst to "C#-shaped" form; the order constraints
("must run after inlining but before loop detection") are preserved verbatim.

Exit criteria:
- A curated set of methods (loops, lock, using, foreach, string interpolation, async, iterator,
  lambdas, local functions) transform to the expected raised ILAst, compared against the C# engine.
- `DecompilerSettings` flags correctly disable the matching transform (C# 1 vs Latest produces
  different raised shapes), matching the C# engine.

Tests: `tests/Decompiler/IL/Transforms/` -- one suite per transform (TDD: failing test first),
plus order-dependence regression tests. This is the largest test set after Phase 1.

### Phase 5 -- C# AST, resolver, and back end (hardest #2)

Scope: `CSharp/Syntax/` (port the generator-emitted AST per 5.2: `AstNode` hierarchy, `Role`,
visitors, `Match...` helpers), `CSharp/Resolver/` (name lookup, overload resolution, type
inference, conversions, nullability, lift ops, tuples/dynamic), `Semantics`, the back end
builders (`ExpressionBuilder`, `StatementBuilder`, `CallBuilder`, `TranslatedExpression/Statement`,
`TranslationContext`, `Annotations`), the ~15 AST transforms (`CSharp/Transforms/`), and the
output stage (`OutputVisitor`, `Output/` `ITextOutput`, `TokenWriter`, formatting). `RequiredNamespaceCollector`,
`SequencePointBuilder`, `AutoEventDecompiler`, `RecordDecompiler`, `CSharpLanguageVersion`.

Dependency: Phase 4.

Deliverable: an `ILFunction` translates to a C# `SyntaxTree`, runs AST transforms, and renders to
text with resolver-proven parentheses/qualifiers and sequence points.

Exit criteria:
- Decompiled text for a broad method set matches the C# engine's output within an agreed tolerance
  (whitespace/identical-tokens; not byte-exact initially).
- A cast/qualifier is emitted iff the resolver proves it necessary (round-trip correctness check on
  a fixture set).
- Sequence points are produced and align with the C# engine's PDB/sequence-point output.

Tests: `tests/Decompiler/CSharp/` -- resolver unit tests (lookup, overload resolution, inference,
conversions), per-AST-transform tests, output formatting tests, sequence-point tests.

This is where the embedded C# semantic engine is fully exercised; budget for the resolver to be
the long pole after the metadata reader.

### Phase 6 -- Disassembler (parallel back end)

Scope: `Disassembler/` (9 files, ~4.3k) and `ReflectionDisassembler`; the metadata/IL-to-text path
that shares only `ITextOutput`. Can be pulled forward as an early Phase 1 milestone ("the metadata
reader works") if desired.

Dependency: Phase 1 + Phase 2 (and a sliver of Phase 3 for IL byte disassembly).

Deliverable: `ilspycmd --il` style IL view and `--dump-table` style metadata dump to text/JSON.

Exit criteria: `--il` output for a method matches the C# `ReflectionDisassembler` output;
`--dump-table` JSON matches the C# `--json --dump-table` output.

Tests: `tests/Decompiler/Disassembler/`.

### Phase 7 -- Decompiler orchestration (public API)

Scope: root `Decompiler/` files -- `CSharpDecompiler`, `DecompilerSettings` (~150 flags),
`SetLanguageVersion`, `DecompileRun`, `DecompilationProgress`, `DecompilerException`,
`PartialTypeInfo`, `NRExtensions`, `NRTAttributes`, `SRMExtensions`, `SRMHacks`,
`SingleFileBundle`, plus `CSharp/ProjectDecompiler/` (whole-project), `Solution/`
(`.sln`/`.csproj` generation via nlohmann-json), and `Documentation/` (XML doc comments).

Dependency: Phases 1-5.

Deliverable: the `CSharpDecompiler` public API surface:
`DecompileWholeModuleAsString`, `DecompileType`, `DecompileTypes`, `Decompile(EntityHandle[])`,
`DecompileModuleAndAssemblyAttributes`, whole-project-to-folder, solution/project-file generation.

Exit criteria: the whole-module decompilation of a sample assembly matches the C# engine's
`ilspycmd` output within tolerance; `.sln`/`.csproj` generation round-trips.

Tests: `tests/Decompiler/` orchestration tests; whole-module golden-output comparisons.

### Phase 8 -- ILSpyX core subset

Scope: `ILSpyX/FileLoaders/` (PE, WebCIL, bundle, archive, Xamarin compressed, registry, load
result), `Abstractions/` (`ILanguage` and friends), `PdbProvider/` (Portable PDB via Phase 1
reader; Cecil bridge replaced), `Util/`, `Extensions/`, `Instrumentation/`, `Settings/` (CLI-relevant).
Drop `Mono.Cecil` and `System.Composition`. Defer `Analyzers/`, `Search/`, `TreeView/`,
`MermaidDiagrammer/`.

Dependency: Phase 1 (FileLoaders, PdbProvider), Phase 7 (Abstractions/ILanguage).

Deliverable: a file can be loaded through the loader registry and handed to `CSharpDecompiler`;
`-usepdb` pulls variable names; `-genpdb` writes a Portable PDB.

Exit criteria: the loader registry picks the right loader for PE/WebCIL/bundle/archive; Portable
PDB round-trips variable names and sequence points.

Tests: `tests/ILSpyX/FileLoaders/`, `tests/ILSpyX/PdbProvider/`.

### Phase 9 -- BamlDecompiler

Scope: `BamlDecompiler/` -- `Baml/` (record reader, `KnownThings` -- note `KnownThings.gen.cs` is
excluded from C# compile; port the generator's *output* or regenerate), `Handlers/{Blocks,Records}/`,
`Rewrite/`, `Xaml/`.

Dependency: Phase 7 (for the `--decompile-baml` integration).

Deliverable: a `.baml` resource decompiles to XAML matching the C# engine.

Exit criteria: `ilspycmd --resource <name>.baml` and `-p --decompile-baml` produce XAML matching the
C# engine within tolerance.

Tests: `tests/BamlDecompiler/` using existing BAML fixtures.

### Phase 10 -- ilspycmd CLI

Scope: `ILSpyCmd/` -- `IlspyCmdProgram` (port the ~40 options to cxxopts), `ProgramExitCodes`,
`TypesParser`, `ResourceExtensions`, `MetadataTableDumper` (uses Phase 6), `BamlAwareWholeProjectDecompiler`
(uses Phase 9), `ValidationAttributes`. Drop `DotNetToolUpdateChecker` (stub, disabled by default).
Skip `AsContainer/` (Dockerfiles, not C#).

Dependency: Phases 6-9.

Deliverable: a `ilspy_cli` executable with the option set: decompile to stdout/project, `-t`/`-m`,
`-il`/`--il-sequence-points`, `-genpdb`/`-usepdb`, `-l`, `--list-resources`, `--resource`,
`--decompile-baml`, `--dump-table` (+`--json`), `-lv`, `--ilspy-settingsfile`, `-ds`, `-r`,
`--no-dead-code/--no-dead-stores`, `-d/--dump-package`, `--nested-directories`, `--disable-updatecheck`.

Exit criteria: `ilspy_cli` decompiles the same assemblies the C# `ilspycmd` does, with matching
output within tolerance, on Windows and Linux.

Tests: `tests/ILSpyCmd/` -- option parsing, exit codes, end-to-end decompile of fixture assemblies.

### Phase 11 -- Integration, fixtures, and hardening

Scope: cross-phase golden-output harness, reuse of `TestFixtures.Resources/` and `ILSpy-tests/`
fixtures, CI on Windows + Linux, perf baselines, `-Werror` consideration, the deferred codegen
generator (5.2), and the optional items deferred above (Windows PDB, diagrammer, update check).

Deliverable: CI green on both platforms; golden-output parity within tolerance across the fixture
corpus; a documented delta list of any intentional C#-vs-C++ output differences.

Exit criteria: the fixture corpus decompiles with the same success rate as the C# engine; no
gratuitous differences; `ilspy_cli` is usable as a drop-in for the common `ilspycmd` workflows.

Tests: the golden-output harness is the test.

---

## 11. Test strategy

- **Framework**: gtest, per the mirror doc. Test files mirror source dirs (`cpp/Decompiler/Metadata/PE.cpp`
  -> `cpp/tests/Decompiler/Metadata/PE_Test.cpp`).
- **TDD**: every new behaviour gets a failing test first (per `CLAUDE.md`).
- **Fixtures**: reuse `TestFixtures.Resources/` and (when populated) `ILSpy-tests/` as **input
  assemblies only**. Do not port the .NET test projects. Build a C++ golden-output harness that
  decompiles each fixture and diffs against the C# engine's output captured at port time.
- **No silent passes**: when an expected component is missing, `FAIL()` (per `CLAUDE.md`); never
  `return` early to bypass an assertion.
- **Invariants**: the ported `CheckInvariant` runs in every debug test build; a test that produces a
  tree must also pass the invariant.
- **Robustness tests**: malformed/obfuscated IL must degrade gracefully, never crash; assert
  warning output, not absence of failure.
- **Parity tolerance**: define a comparator that normalizes whitespace and compares token streams;
  record intentional differences in a delta file. Byte-exact parity is a stretch goal, not a
  Phase 1-10 gate.

---

## 12. Effort and sequencing summary

Indicative sizes (C# lines to port) per phase, ignoring tests and the gap-addition overrun:

| Phase | Subsystem | C# lines (approx) | Risk |
|---|---|---:|---|
| 0 | Scaffolding/Util | ~6,300 (Util) | low |
| 1 | Metadata on winmd baseline + gap fills | ~7,700 surface (port) + ~5,800 winmd (vendored) + ~2-3k gap additions | high (de-risked vs from-scratch) |
| 2 | Type system + Semantics | ~22,300 | high |
| 3 | ILAst model + front end | ~25,000 (model + reader, minus transforms) | high |
| 4 | IL transforms | ~33,000 (incl. FlowAnalysis) | high (order-sensitive) |
| 5 | C# AST + resolver + back end | ~58,000 (incl. generated AST) | **highest** (resolver) |
| 6 | Disassembler | ~4,300 | medium |
| 7 | Orchestration/Solution/Doc | ~6,700 | medium |
| 8 | ILSpyX core subset | ~2,100 | low-medium (Cecil gap) |
| 9 | BamlDecompiler | ~9,500 | medium |
| 10 | ilspycmd | ~2,200 | low (once engine exists) |
| 11 | Integration/hardening | n/a | medium |

Sequencing notes:
- Phases 1 and 5 dominate the schedule; Phase 1 is de-risked by the winmd baseline but still
  carries the method-body/debug-table/WebCIL/bundle/ergonomics gap work, and Phase 5 holds the C#
  resolver long pole.
- Phase 6 can run in parallel with 3-5 once 1-2 are done, as an early milestone.
- Phase 4 and 5 each decompose into many small, independently testable transforms -- good for
  parallelisation across contributors once their dependencies are green.
- The codegen question (5.2) should be decided before Phase 3 starts; revisit after Phase 5.

---

## 13. Status tracking

Use the mirror doc's status legend on every file as it lands:
`Ported`, `Audit`, `NEW`, `C++-only`. Maintain a `cpp/STATUS.md` (NEW initially) listing each
mirrored file and its status, so progress is visible without diffing trees. `cpp/Decompiler/Metadata/Ecma335/`
files are `C++-only` from the start.

---

## 14. Open questions / audits needed before/during port

1. **winmd gap audit**: confirm the exact surface the engine needs by auditing every
   `System.Reflection.Metadata`/`MetadataReader` call site in `ICSharpCode.Decompiler`, then map
   each to either a winmd-provided row/view or a gap to fill (method bodies, debug tables, WebCIL,
   bundles, the `MetadataFile`/`PEFile`/handle ergonomics). Do this first in Phase 1. Also pin the
   winmd upstream commit in `cpp/Decompiler/Metadata/Ecma335/winmd/README`.
2. **Mono.Cecil surface**: audit every `Mono.Cecil.*` use (currently `LoadedAssemblyExtensions` and
   `MonoCecilDebugInfoProvider`) to confirm the Phase 1 reader + a small write path replaces it for
   the CLI subset; confirm no CLI path silently depends on Cecil.
3. **WebCIL compression**: confirm whether `WebCilFile` needs any decompression beyond what miniz
   (deflate) provides; the audit found no brotli in scope, so deflate (miniz) is the expected
   fallback.
4. **Generated AST extent**: quantify exactly which `CSharp/Syntax` files are
   generator-emitted vs hand-written, to size the port-the-output work in Phase 5.
5. **`KnownThings.gen.cs`**: confirm how the BAML known-things table is generated and decide
   port-the-output vs regenerate for Phase 9.
6. **Regex**: decided -- `std::regex` (5.12); revisit per-pattern only if a fixture needs RE2's
   ReDoS guarantees.
7. **Windows PDB**: confirm the fixture corpus's PDB mix; decide whether Windows-PDB support is
   needed for the parity harness or Portable PDB suffices.
8. **Parity tolerance**: agree the exact comparator (whitespace-normalized token diff) and the
   intentional-delta policy before Phase 11.

---

## 15. Decision log (to be maintained during the port)

A running record of the choices made against this plan, with rationale. Seed entries:

| # | Decision | Date | Rationale |
|---|---|---|---|
| D1 | Port generated *output* first; revisit codegen after Phase 5 | plan | fastest path to a working engine |
| D2 | UTF-8 internally; convert at boundaries | plan | simplest cross-platform story |
| D3 | Drop MEF/NuGet update-checker for CLI | plan | CLI does not compose analyzers; update-check optional |
| D4 | Portable PDB only initially; Windows PDB deferred | plan | Portable PDB is ECMA-335 (covered by reader); legacy PDB is a separate format |
| D5 | pe-parse for PE container; hand-write ECMA-335 metadata | plan | *superseded by D7*: winmd provides PE + ECMA-335, so pe-parse is dropped and the reader is not hand-written |
| D6 | C++17, no `-Werror` until Phase 11 | plan | no C++ analogue to `nullable` WarningsAsErrors |
| D7 | Adopt `microsoft/winmd` (MIT, vendored) as the ECMA-335 + PE baseline; drop `pe-parse` | plan | winmd covers PE container + full ECMA-335 tables + signatures + coded indices + custom attributes; RTCLI.Runtime has no metadata reader; from-scratch would duplicate ~5.8k mature lines |
| D8 | Use `cxxopts` (not CLI11) for CLI option parsing | plan | ilspycmd's flat ~40-option set needs only what cxxopts provides; lighter than CLI11 |
| D9 | Use `miniz` (not zlib) for deflate | plan | miniz covers deflate/inflate with compression levels (the full DeflateStream surface); smaller footprint |
| D10 | Use `std::regex` (not RE2); add no regex library | plan | core patterns are simple, no backreferences; winmd also uses std::regex |
| D11 | Keep `plog` for logging (not a minimal `std::clog` logger) | plan | preferred over `std::clog`; the C# uses no logging framework (only sparse `Debug.WriteLine`), but plog gives the C++ engine a structured, leveled, cross-platform log sink |
| D12 | Defer `miniz` from the default vcpkg manifest (optional `find_package`); make REQUIRED at Phase 7/8 | bring-up | miniz is unused until deflate (Portable PDB writes, `--dump-package`); its vcpkg source download hit an environment SSL/proxy error. Move it behind a `deflate` manifest feature so Phase 0-6 build cleanly |
| D13 | Use the Visual Studio 2026 generator (preset `windows-vs2026`) as the primary Windows build; keep Ninja as an alternative; retire VS 2017 | bring-up | VS 2026 Insiders (MSVC 14.51 / `cl` 19.51, Windows SDK 10.0.26100.0) is installed at `C:\Program Files\Microsoft Visual Studio\18\Insiders`. The VS 2026 generator produces a modern `ilspy.slnx` solution and picks the installed Windows 10 SDK itself (no dev-prompt needed), avoiding the VS 2017 generator's hard default to the missing Windows SDK 8.1 (MSB8036). Phase 0 verified green on VS 2026: 11/11 tests pass. The VS 2017 preset is kept only as a legacy fallback.
| D14 | Phase 0 (scaffolding + `Util` primitives) and the Phase 1 start (winmd vendored + `MetadataFile` adapter) are implemented and green | bring-up | 11/11 gtest cases pass (9 `Util_*` + 2 `Metadata_Smoke`, the latter parsing `mscorlib.dll` via winmd and gracefully rejecting a non-CLI file). Phases 2-11 remain per the plan |
| D15 | Phase 1 method-body decoder implemented on top of the winmd baseline | bring-up | Filled the first Phase 1 gap (winmd is metadata-only): `MetadataFile::GetMethodBody(rva)` + `MethodDefs()` decode ECMA-335 II.25.4 tiny/fat headers, IL bytes by RVA, local-var-sig token, and exception-handler clauses against `mscorlib.dll`. TDD via a throwaway raw-byte diagnostic confirmed the real layout: `CorILMethod_Sect_EHTable = 0x01` (not the 0x18 some references cite), `DataSize` INCLUDES the 4-byte section header, small clauses are 12 bytes with BYTE-sized `TryLength`/`HandlerLength`, fat clauses are 24-byte DWORDs. 14/14 gtest cases pass on VS 2026 |
| D16 | Phase 1/2 signature decoder + core type representation implemented | bring-up | Ported `TypeKind`, `KnownTypeCode` (full 60-entry table), `TopLevelTypeName`, `FullTypeName`, and a minimal `IType` hierarchy (`KnownType`/`SimpleType`/`ParameterizedType`/`ArrayType`/`ByReferenceType`/`PointerType`/`TypeParameter`/`SpecialType`). `SignatureDecoder` bridges winmd `TypeSig`/`MethodDefSig` to `IType`; `MetadataFile::GetMethodSignature(token)` decodes real `mscorlib` signatures (instance `Object.Equals(object):bool`, generic instantiations, arrays, primitives). 23/23 gtest cases pass. winmd includes routed through `Ecma335/WinmdInclude.hpp` to disable its WinMD-only `XLANG_ASSERT` (e.g. the TypedByRef-in-ParamSig assert) without editing vendored headers |
| D17 | IL opcode tables + minimal disassembler implemented | bring-up | Ported `OperandType` and `ILOpCodes` (the 287-entry operand-type and display-name tables extracted verbatim from the C# source via script, avoiding transcription errors). `ILDisassembler` walks a method body's IL bytes, decoding opcodes (single- and two-byte) and sizing operands (Switch read as n + n*4). Tested against `mscorlib`: the walk consumes exactly CodeSize bytes for >90% of bodies, and ret/call/ldstr/switch/newobj all appear. 25/25 gtest cases pass. This is the core of Phase 6's disassembler and a prerequisite for Phase 3's ILReader |
| D18 | Metadata entity surface (Phase 2 MetadataModule bridge) implemented | bring-up | `MetadataFile::TypeDefs()` (with resolved base types), `GetMethods/GetFields/GetProperties(typeToken)`, and `GetFieldSignature(fieldToken)` walk winmd's TypeDef member-list ranges (MethodList/FieldList/PropertyList) and resolve tokens. `DecodeFieldSignature` added. Tested against `mscorlib`: System.Object has no base, System.String derives from Object, Object's methods include Equals/ToString/GetType/MemberwiseClone, and String's char fields decode to System.Char. 29/29 gtest cases pass. Range iterators dereference to row values (`(*it).` not `it->`); empty coded_index (no base) detected via `explicit operator bool` |
| D19 | Custom attributes + pseudo-attribute flags implemented | bring-up | `MetadataFile::GetCustomAttributes(entityToken)` exposes winmd's custom_attribute decoder for TypeDef/MethodDef/Field/Property tokens (attribute type namespace+name; ctor/named-arg decoding deferred). `TypeDefInfo::Flags` exposes the raw TypeAttributes. TDD caught a real finding: `[Serializable]` is a pseudo-attribute -- the compiler sets `tdSerializable = 0x2000` (ECMA-335 II.23.1.15, matching System.Reflection.TypeAttributes.Serializable; winmd's bit 13 is correct, the 0x4000 I first assumed was wrong) in the TypeDef Flags, not a CustomAttribute row. Tests: [Serializable] flag on String, [AttributeUsage] CA on ObsoleteAttribute, [Extension] CA on Enumerable methods, graceful invalid-token. 33/33 pass |
| D20 | TypeKind derivation from TypeDef flags + base type implemented | bring-up | `DeriveTypeKind(flags, base, selfRefName)` ports the C# logic (MetadataTypeDefinition.cs + SRMExtensions.IsEnum/IsValueType/IsDelegate): Interface via flag 0x20; Enum if base is System.Enum; Struct if base is System.ValueType (self not System.Enum); Void for System.Void; Delegate if base is System.MulticastDelegate; else Class. `TypeDefInfo::Kind` populated. Tested against `mscorlib`: Object/String -> Class, Int32 -> Struct, Void -> Void, Enum/ValueType -> Class, IEnumerable -> Interface, Delegate/MulticastDelegate -> Class, Action -> Delegate, DayOfWeek -> Enum; kind distribution sane. 35/35 pass |
| D21 | IL text disassembler + CLI --il workflow implemented (Phase 6 milestone pulled forward) | bring-up | `MetadataFile::ResolveTokenToString(token)` resolves TypeDef/TypeRef/MethodDef/Field/MemberRef tokens to "Namespace.Type"/"Namespace.Type::Member" (TypeSpec/StandAloneSig/MethodSpec/UserString fall back to the raw hex token). `DisassembleILText` walks a method body and emits `IL_xxxx: mnemonic operand` lines with branch/switch targets as IL labels, inline constants, and resolved token operands. `ilspy_cli <assembly> --il [-t Type]` dumps real IL end-to-end (verified: System.Object::Equals/ToString/etc. with resolved calls, branches, constants). 39/39 gtest cases pass. This is the first user-facing output of the port and an end-to-end exerciser of the metadata + opcode + signature work; the token resolver is shared with the Phase 3 IL reader |
| D22 | ILAst instruction model foundation implemented (Phase 3 start) | bring-up | Ported `OpCode` (101 values, verbatim from the generated Instructions.cs), `InstructionFlags`, `SlotInfo`, `StackType`, `VariableKind`, and the `ILInstruction` abstract base with the strict-tree model: parents own children via `std::unique_ptr` slots with a non-owning `Parent` back-pointer; `SetChild` asserts a fresh child has no parent; `Flags` recompute bottom-up (caching deferred); `CheckInvariant` (debug) verifies parent/child/index/connectedness/flag consistency; `IsConnected` is recursive (reachable from an ILFunction root). Concrete nodes: `ILVariable`, `Block`, `BlockContainer` (+`ContainerKind`), `ILFunction`, and leaf instructions `Nop`/`LdcI4`/`LdLoc`/`StLoc`/`Branch`/`Leave`/`Call`, plus `StackTypeOf` (IType -> StackType). 45/45 gtest cases pass. Per D1 (port the generated output), the remaining ~90 instruction kinds port incrementally from Instructions.cs as the IL reader needs them |
| D23 | ILAst instruction batch: Simple/Unary/Binary bases + 11 common instructions | bring-up | Ported the `SimpleInstruction`/`UnaryInstruction`/`BinaryInstruction` abstract bases (DirectFlags None; Argument/Left/Right slots inlineable) and concrete instructions `LdStr`/`LdNull` (Simple), `CastClass`/`IsInst`/`Box`/`UnboxAny`/`Throw`/`LdLen`/`Conv` (Unary), `Comp`/`BinaryNumericInstruction` (Binary), with faithful DirectFlags from the generated Instructions.cs (CastClass/Throw/LdLen += MayThrow, UnboxAny += SideEffect|MayThrow, Throw += EndPointUnreachable) and the `ComparisonKind`/`BinaryNumericOperator` enums. A nested-tree test (stloc(x, unbox.any(int32, castclass(object, ldc.i4(0)))) + throw ldnull) verifies the invariant and that MayThrow/SideEffect propagate up to the ILFunction. 49/49 gtest cases pass. ~20 of ~100 instruction kinds now ported |
| D24 | Minimal straight-line IL reader (Phase 3 core seed) implemented | bring-up | `ReadStraightLineIL(file, methodToken, rva)` decodes a method body's IL bytes into an ILFunction tree via the stack simulation (expressionStack of pending trees + currentStack of committed slots), restricted to straight-line code (no branches/switch/exception handlers -- those bail to nullptr). Handles a curated opcode set: ldnull, ldc.i4*, ldarg*, ldloc*/stloc*, pop, call/callvirt/newobj (arg pop in reverse, `this` handling, void vs value return), ldstr (token placeholder), ret (void -> Leave(container); else Leave(container, Pop())), throw, nop/break. Parameter variables come from the decoded method signature (IsInstance -> `this` is arg 0). 53/53 gtest cases pass; 4665 mscorlib method bodies decode. `ilspy_cli <asm> --ilast [-t Type]` dumps real ILAst trees end-to-end (verified: System.Object::ToString -> `leave call System.Object::ToString(call System.Object::GetType(ldloc(this)))`). The full worklist/union-find/BlockBuilder reader is ported next |
| D25 | Extended IL reader opcode coverage | bring-up | Added LdcI8/LdcF4/LdcF8 (Simple) and LdLoca (address-of, result Ref) nodes, and grew the straight-line reader with: ldc.i8/ldc.r4/ldc.r8; ldloc.s/stloc.s/ldloca.s/ldarg/ldarga/starg(starg_s); binary arithmetic+bitwise+shift (add/sub/mul/div/rem/and/or/xor/shl/shr + _un + _ovf) -> BinaryNumericInstruction; comparisons ceq/cgt/clt (+_un) -> Comp; conv_i1..conv_r_un -> Conv; dup (store to a fresh StackSlot + load twice). Coverage rose from 4665 to 5329 mscorlib method bodies. 53/53 gtest cases pass; the ILAst invariant still holds on every decoded tree |
| D26 | Field access (ldfld/stfld/ldsfld/stsfld/ldflda/ldsflda) in the IL reader | bring-up | Ported the LdFlda/LdsFlda/LdObj/StObj nodes faithful to the generated Instructions.cs (LdObj/StObj: SideEffect|MayThrow; LdFlda: MayThrow unless DelayExceptions; LdObj result = StackTypeOf(Type), StObj result Void) and wired ldfld/stfld/ldsfld/stsfld/ldflda/ldsflda into the reader using the C# composition: ldfld = LdObj(LdFlda(target, field){DelayExceptions}, field.Type), stfld = StObj(LdFlda(target, field){DelayExceptions}, value, field.Type), ldsfld = LdObj(LdsFlda(field), field.Type), stsfld = StObj(LdsFlda(field), value, field.Type). Field type/name resolved via MetadataFile::GetFieldSignature/ResolveTokenToString. Coverage nearly doubled: 5329 -> 10298 mscorlib method bodies. Verified: System.String::get_FirstChar -> `leave ldobj(System.Char, ldflda(System.String::m_firstChar, ldloc(this)))`. 53/53 gtest cases pass |
| D27 | Array/cast/box/unbox opcodes in the IL reader | bring-up | Added NewArr and LdElema nodes (NewArr: MayThrow, result O; LdElema: MayThrow, result I/Ref) and MetadataFile::ResolveTypeToken(token) (TypeDef/TypeRef -> IType via ResolveTypeDefOrRef over a hand-built coded_index, since the type operand is the type itself, not its base). Wired castclass/isinst/box/unbox/unbox.any, newarr, ldlen, ldelema, ldelem.* (typed + primitive, as LdObj(LdElema(...))), stelem.* (as StObj(LdElema(...), value)), ldind.*/stind.*, neg/not. Coverage 10298 -> 11215. 57/57 gtest cases pass; ILAst invariant holds on every decoded tree |
| D28 | Branch-aware IL reader (Phase 3 control flow) implemented | bring-up | Extracted the opcode switch into a shared `DecodeOne` helper (returns Continue/Bail/Terminal/BranchInstr) so both readers reuse it. Added `IfInstruction` (Condition/TrueInst/FalseInst slots; DirectFlags=ControlFlow) and extended `Branch` to carry a target offset (the reader emits offset form; BlockBuilder resolves later). `ReadIL` pre-scans branch targets, splits the method into blocks at those offsets, emits `Branch(targetOffset)` for br/br.s and `IfInstruction(condition, Branch(target))` for brtrue/brfalse (with type-aware null/0 normalization) and beq..blt (+_s/_un) (as `IfInstruction(Comp(...), Branch(target))`), makes fall-through explicit, and resolves target offsets to Block pointers at the end. Switch and exception handlers still bail; branches taken with a non-empty evaluation stack bail (no stack merging yet -- conservative but correct). Coverage 11215 -> 14599 mscorlib method bodies. Verified: System.Object::Equals decodes to multi-block ILAst with `if (comp(ne.un, ldloc(arg_0), ldloc(arg_1))) br IL_0006`. 57/57 gtest cases pass; `ilspy_cli --ilast-all` dumps branch-aware trees |
| D29 | SwitchInstruction support in the IL reader | bring-up | Added `SwitchInstruction` (Value child inlineable + Sections collection; DirectFlags=ControlFlow, result Void) and `SwitchSection` (Labels set + Body child = a Branch; `SetBody` wires the child's parent/index) nodes faithful to the generated Instructions.cs. Wired `switch`: pop the value, read n relative targets, build N case sections (label = case index, body = Branch(target)) plus a default section (body = Branch(fall-through offset)). The block driver creates blocks for every switch target + the default fall-through. `ResolveBranches` resolves each section's Branch offset to a Block. Attempted a stack-merge/flush change to handle non-empty-stack branches but it regressed coverage (14599->14290) without the full union-find + re-import fixpoint, so it was reverted; that remains a separate slice. Coverage 14599 -> 14636. 57/57 gtest cases pass; real `switch ldloc(arg_0) { ... }` trees decode |
| D30 | Exception-handler ILAst nodes (TryCatch/TryCatchHandler/TryFinally/TryFault) ported | bring-up | Added `TryInstruction` base (TryBlock child; DirectFlags=ControlFlow, result Void) and the four EH nodes faithful to the generated Instructions.cs + TryInstruction.cs: `TryCatch` (TryBlock + Handlers collection), `TryCatchHandler` (optional Filter + Body + catch Variable; DirectFlags=ControlFlow|MayWriteLocals), `TryFinally` (TryBlock + FinallyBlock), `TryFault` (TryBlock + FaultBlock). Unit tests build each tree, check the invariant/flags/ResultType/dump, including a filter catch. 61/61 gtest cases pass. The full EH reader integration (the BlockBuilder that nests blocks into try/handler containers from the EH region table) is the next slice; the reader still bails on EH methods until then |
| D31 | IL reader: data-driven opcode coverage + pragmatic EH | bring-up | Instrumented DecodeOne's bail to find the top blocking opcodes, then added: ldarga.s, IL prefixes (volatile./constrained./readonly./unaligned./tail. as no-ops that consume their operand), ldobj/stobj/initobj (typed memory ops), ldftn/ldvirtftn/sizeof/ldtoken (minimal SimpleInstruction nodes in TokenInstructions.hpp), conv.ovf.* (22 overflow-checking conversions), localloc, mkrefany, ckfinite, arglist. Then removed the EH bail: leave/leave.s/endfinally emit Leave(function body) (semantically lossy but valid), catch/filter handler blocks are seeded with an ExceptionStackSlot variable on the input stack, handler/try/filter offsets added to branch targets. Coverage 14636 -> 16472 mscorlib method bodies (91% of methods with RVA). 61/61 gtest cases pass; the ILAst invariant holds on every decoded tree. The full BlockBuilder nesting and the union-find stack-merge remain |

| D32 | ILAst-to-C#-text seed implements the end-to-end IL -> ILAst -> C# pipeline (Phase 5 rung 1) | port | `ILAstToCSharp` (CSharp/, marked C++-only in the mirror: no single C# counterpart) walks an `ILFunction` and emits C#-ish text: blocks flatten into statement lists, branches/`if` become `goto IL_XXXX`, switch becomes case-goto, `stloc` declares a `var` on first store, calls/casts/isinst/box/conv/field (`ldflda`+`ldobj`)/array (`ldelema`)/`ldlen` use approximate C# syntax, `new` for ctor calls, try/catch/finally for hand-built EH trees. No resolver, no AST transforms, no type inference -- pulled forward like the IL disassembler (D21) to exercise the pipeline end-to-end before the real back end lands. `ilspy_cli <asm> --csharp [-t Type]` works (verified: System.Object methods decode end-to-end; the `get_FirstChar` getter becomes `return this.m_firstChar;`). 76/76 gtest cases pass (15 new) |

| D33 | BlockBuilder EH nesting + stfld/branch fixes: exceptions decode to nested region containers | port | Ported `BlockBuilder.cs`: flat offset-ordered blocks nest into `TryCatch`/`TryFinally`/`TryFault` containers (catch clauses sharing a try range group under one `TryCatch`; plain catch gets the constant `ldc.i4 1` filter; wrapper blocks get explicit fall-through branches instead of the C# InvalidBranch marker, since our Block requires a terminal). IL `leave` decodes as a plain Branch out of the region (matching `DecodeUnconditionalBranch(isLeave: true)`); `endfinally`/`endfilter` emit null-target Leaves resolved to the innermost container. Fixed three latent reader bugs the nesting exposed: handler/filter entry blocks were never decoded (seeded but unreachable), and their seed set `stackBase` past the exception slot making it un-poppable; `makeBlock` was not idempotent (duplicate blocks for the same offset); `IL_CBR` classified long-form conditional branches (0x3B-0x3F, 0x40-0x44) as short (`< 0x40` miscovered the 0x38-0x3F long forms) desyncing the operand read; `stfld` popped target before value (411 real setters showed the swap as `value.field = this`). `Block.StartILOffset` added (the ILRange slot the builder needs). Decode coverage 16472 -> 16684 mscorlib bodies; 555 EH methods fully nested; zero dangling branches. 82/82 gtest cases pass |

| D34 | Evaluation-stack merging across block boundaries (union-find slot merge) + rethrow | port | Ports the C# ILReader's stack-merge shape: at every control-flow edge (branches, conditional fall-throughs, switch targets, block boundaries) pending expressions flush into S_ stack slots in evaluation order and the carried stack merges into the target's recorded input stack; same-height/same-type slots unify via a representative map (first-recorded wins) and a final tree pass remaps merged variables. leave merges an EMPTY stack (ECMA-335 clears it). Handler/filter entry stacks are pre-recorded. Mismatched heights/types bail the method (insert-stack-adjustment conversions deferred). Blocks decode once; merges arriving late are handled by the final remap, no re-import fixpoint needed. `Rethrow` node ported (MayThrow\|EndPointUnreachable); reader emits it, the seed renders `throw;`. Fixed the exposed Terminal-time flush ordering bug (final's ChildIndex renumber). 387 methods now decode with merged S_ slots |
| D35 | GetMethodSignature handles MemberRef + MethodSpec (was silently decoding the wrong MethodDef row) | port | The token lookup ignored the table tag: any token's RID was read as a MethodDef row, so every call to a MemberRef/MethodSpec popped the wrong argument count (silently degrading decode coverage). Now: 0x06 MethodDef `Signature()`, 0x0A MemberRef `MethodSignature()`, 0x2B MethodSpec unwraps via the raw MethodDefOrRef coded index (winmd's MethodSpec row lacks public column accessors; `table_base::get_value` is public). Discriminator tests: `.ctor` MemberRefs must return void; `BinarySearch<T>` MethodSpec must decode 5 params with array first. Decode coverage 16684 -> 17829 mscorlib bodies (combined with D34). 88/88 gtest cases pass |

| D36 | newobj pushes the constructed object | port | The reader treated the constructor's declared void return as the call's result, so `throw new X(...)` / `var x = new X(...)` underflowed and bailed (roughly a third of the previously-failing bodies). newobj now always pushes with StackType O. Decode coverage 17829 -> 25214 (99.6% of mscorlib bodies). 89/89 gtest cases pass |

| D37 | Hand-rolled ECMA-335 signature blob decoder replaces winmd's TypeSig window | port | winmd's TypeSig only covers the WinRT subset and throws `Unrecognized ELEMENT_TYPE` on TypedByRef, vararg sentinels, fn-ptr, CModReqd/Opt, and Pinned -- framework method signatures carry all of these, so calls touching them bailed (`no sig`). A hand-rolled II.23.2 parser (`DecodeMethodSignatureBlob`/`DecodeFieldSignatureBlob`/`DecodeTypeSpecBlob` on raw blob bytes via public `table_base::get_value` + `database::get_blob`) handles the full element-type range; FnPtr parses but maps to UnknownType (no IType node yet). ResolveTypeToken now decodes TypeSpec operands (0x1B) directly. dup also reads committed stack-slot tops, and `RefAnyType` is ported. Decode coverage 25214 -> **25315 of 25315 mscorlib bodies (100%)**; all 1651 EH methods decode. 93/93 gtest cases pass |

| D38 | Phase 4 start: transform infrastructure + ControlFlowSimplification | port | `ILVariable` gains Load/Store/Address counts (parameters start at 1 store, C#'s usesInitialValue convention), `Block` gains `IncomingEdgeCount`; both are fresh-computed by `ControlFlow/VariableUsage` (C++-only) instead of maintained by reader/transform events -- simpler and deterministic at this scale. `ILInstruction::ReplaceWith` added. CFS ports branch-chain collapse, dead stack-slot stores (non-aggressive: IsSimple only; the aggressive path needs IsPure analysis), return-block inlining, branch-to-leave duplication, and single-edge block combining; skipped: nop removal (reader emits no Nops), aggressive move-return-block-into-try (needs scoped usage analysis), early SwitchDetection call. Model divergences handled: our Block splits the terminal out (C# counts it in) and blocks tombstone-by-moving into a graveyard vector (snapshot pointers must not dangle), with IncomingEdgeCount decrements mirroring what C#'s node destruction does. A stale-ChildIndex class of bug surfaced by erasing instructions behind a SetFinal is fixed by `Block::RenumberChildren()`/self-renumbering `Add`. 98/98 gtest cases pass (5 new, incl. an 8000-method post-transform sweep) |

| D39 | FlowAnalysis foundation: ControlFlowNode + Dominance + ControlFlowGraph | port | Ports ICSharpCode.Decompiler/FlowAnalysis/{ControlFlowNode,Dominance}.cs (Cooper-Harvey-Kennedy iterative dominators with `std::optional<std::vector<...>>` modeling the C# null/unreachable dominator-children state) and IL/ControlFlow/ControlFlowGraph.cs (per-BlockContainer CFG: branch edges + exits-out-of-container + HasReachableExit; leaves-to-the-function-body are not exits). The C# reader materializes a fall-through `Branch` after every non-unreachable block end; our reader relies on container order, so the CFG adds a positional fall-through edge after a conditional `IfInstruction` final instead. The `IfInstruction.Flags()` branch-combination bug the CFG exposed (`EndPointUnreachable` was unioning the TrueInst's `Branch` flag into the `if` rather than combining -- the endpoint is reachable if *any* path is) is fixed by virtualizing `ILInstruction::Flags()` and porting `SemanticHelper.CombineBranches`. `ILInstruction::IsDescendantOf` and `Block` `IsReachable` helpers round out the surface. 104/104 gtest cases pass (6 new) |

| D40 | ILInlining subset: single-use variable inlining + dead pure store removal | port | Ports the core of ILInlining.cs: `InlineAllInBlock` (reverse StLoc pass), `VariableCanBeUsedForInlining` (StoreCount==1, LoadCount+AddressCount==1), `FindLoadInNext` (subtree walk finding the single LdLoc(v) with MayReorder safety), and `InlineOne` (inline value into load site or remove dead pure store). Pulled forward before SplitVariables (which needs the reaching-definitions dataflow) because ILInlining works on the per-variable usage counts ComputeVariableUsage already provides. SemanticHelper.IsPure + MayReorder ported (flag-level approximation: pure = no side-effects/throws/branches/writes; reorder OK if either is pure and neither writes what the other reads). SlotInfo restrictions (CanInlineIntoSlot, SatisfiesSlotRestrictionForInlining) approximated as permissive (all slots allow inlining) -- sound for our seed purposes; the full SlotInfo system lands when the C# AST back end needs it. Dead variables pruned from fn->Variables (mirrors Variables.RemoveDead). The CLI runs CFS+ILInlining before the C# seed. 109/109 gtest cases pass (5 new) |

Append rows as decisions are taken during implementation.

| D42 | ConditionDetection subset + fall-through edge counting fix | port | Ports the core of ICSharpCode.Decompiler/IL/ControlFlow/ConditionDetection.cs (subset): inlines single-predecessor fall-through blocks into an IfInstruction's FalseInst (the `if (cond) goto X; [body]; X:` pattern becomes `if (cond) { goto X } else { body }`). Skipped: the full exit-point analysis, nested condition stacking, condition negation, and the BlockILTransform post-order driver. Also fixed a CFS bug: `RecomputeIncomingEdgeCounts` didn't count positional fall-through edges (only Branch edges), so CFS incorrectly combined multi-predecessor blocks. The fix adds `CountFallThroughEdges` which increments the next block's IncomingEdgeCount when the current block's final is not EndPointUnreachable. The CLI pipeline is now CFS + ILInlining + LoopDetection + ConditionDetection. 115/115 gtest cases pass (3 new) |
| D43 | InlineReturnTransform | port | Ports ICSharpCode.Decompiler/IL/Transforms/InlineReturnTransform.cs: duplicates a shared return block (`leave (ldloc V)`) so each `stloc V, expr; br retBlock` gets its own 1-predecessor return block. With 1 pred the block is moved into the store's container; with more it is cloned (a running count mirrors the C# incremental IncomingEdgeCount update). CFS (re-run after) then merges the 1-pred block and inlines the single-definition variable to `leave (expr)`. Only Local variables are handled (parameters skipped, matching C#). The CLI pipeline adds InlineReturnTransform + a 2nd CFS after ILInlining. 119/119 gtest cases pass (4 new) |
| D44 | ConditionDetection if-exit inversion + condition negation | port | Extends ConditionDetection with the `InvertIf` exit pattern: `if (cond) goto X else { exit }` (X is the next block, exit is EndPointUnreachable) inverts to `if (!cond) { exit }` with fall-through to X, eliminating the goto. Adds `NegateCondition` (folds `logic.not(comp(op))` into `comp(op.Negate)`, unwraps double negation, swaps if-branches, else wraps as `comp(x == 0)`) and `NegateComparison` on ComparisonKind. Our model diverges from C#: the IfInstruction is the block's final, so the goto can only be dropped (not moved to the block tail) when X is the next block -- the C# keeps `br X` as the block final and lets CFS remove it. This handles the sequential if-throw early-exit chain (e.g. System.Version..ctor) but not multi-pred intermediate join blocks (needs DetectExitPoints + MergeCommonBranches). 121/121 gtest cases pass (5 ConditionDetection) |
| D45 | Metadata parameter names | port | Adds `MetadataFile::GetParameterNames(methodToken)` reading the Param table (winmd `MethodDef::ParamList` -> `Param::Name`/`Sequence`): index 0 is the first declared parameter (the implicit `this` is absent; Sequence 0 is the return-value row). `ReadIL`/`InitParameters` now use real names, falling back to `arg_N` when the Param row is absent or the name is empty. The CLI `--csharp` `paramDecl` uses the same names so the signature and body agree (was `arg_N` in the signature but `value` in the body). 122/122 gtest cases pass (1 new) |
| D46 | Local signature decoding + AssignVariableNames (subset) | port | Adds `DecodeLocalSignatureBlob` (ECMA-335 LOCAL_SIG: 0x07 marker + count + types, with 0x45 PINNED handling) and `MetadataFile::GetLocalTypes(localVarSigToken)` reading the StandAloneSig table (0x11). `ReadIL` now decodes the method body's `LocalVarSigToken` and assigns types to locals, so the rest of the pipeline can infer names. Ports a subset of `AssignVariableNames.cs`: renames compiler-generated locals/stack-slots to type-inferred names (System.Int32 -> num, System.String -> text, System.Boolean -> flag, ...; other types -> lowercased short name), disambiguated against parameter names and earlier locals (num, num_1, num_2). Parameters keep metadata names. Skipped: Humanizer pluralisation, the scope system, loop-counter detection, display-class handling, C#-keyword avoidance. The CLI pipeline adds AssignVariableNames last. 127/127 gtest cases pass (5 new) |
| D47 | User string (#US heap) decoding | port | `ldstr` operands now resolve to the actual string literal instead of a raw hex token. winmd keeps #Strings/#Blob/#GUID/#~ but discards #US, so `PeImage::LocateUsHeap` re-parses the CLI metadata root's stream headers (handling both PE32 and PE32+ optional-header layouts for the COM-descriptor data directory) to find the #US stream, then `PeImage::GetUserString` reads the compressed length and decodes the UTF-16LE characters (dropping the trailing flag byte and the high byte of each code unit for the ASCII subset). `MetadataFile::ResolveTokenToString` handles table 0x70 via the new `GetUserString`. 128/128 gtest cases pass (1 new) |
| D48 | RemoveRedundantReturn (subset) | port | Ports a subset of `RemoveRedundantReturn.cs`: a void function's trailing `return;` (a value-less `Leave` of the body container on the last block) is made implicit by dropping the final (the block falls through; the seed renders nothing for a null final). Skipped: the recursive descent into try/lock/using bodies and the iterator case. The CLI pipeline runs it after AssignVariableNames. 132/132 gtest cases pass (4 new) |
| D49 | ILAstToCSharp base-constructor rendering | port | The C# seed now renders a `.ctor` call used as a statement (void, in a block) whose first arg is `this` as `base(args)` (dropping the implicit `this`), instead of `new BaseType(this)`. A `newobj` (which pushes the new object) is an expression and still renders as `new Type(args)` via CallText; a `.ctor` statement without a `this` first arg falls back to `new Type(args)`. The real base-vs-this and constructor-initializer placement is a Phase 5 (C# AST) concern; this is a seed-level approximation. 133/133 gtest cases pass (1 new) |

| D41 | LoopDetection: back-edge loop containerization | port | Ports the core of ICSharpCode.Decompiler/IL/ControlFlow/LoopDetection.cs (subset): detects back edges (branches to a block that dominates the source), collects the natural loop, and wraps it in a BlockContainer(Kind=Loop) with the header's instructions moved to a new entry point, body blocks moved in, and the exit branch rewritten to Leave(loopContainer). Skipped: ExtendLoop (exit-point minimization), SwitchDetection integration, the full BlockILTransform post-order driver. Runs as a standalone IILTransform: processes each Normal-kind container in one pass (reverse-post-order over the original CFG), skips Loop-kind containers to avoid re-detecting the loop just constructed (the C# avoids this because the newly created entry point has no CFG node). Fix for the use-after-free crash the rewrite caused: collect Branch pointers first, mutate second (ReplaceWith destroys the Branch mid-traversal). The CLI runs CFS+ILInlining+LoopDetection before the C# seed. 112/112 gtest cases pass (3 new) |
