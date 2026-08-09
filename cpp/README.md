# ilspy (C++ port)

C++17 port of the ILSpy decompiler **core and CLI** (the `ICSharpCode.Decompiler`,
`ICSharpCode.ILSpyX`, `ICSharpCode.BamlDecompiler`, and `ICSharpCode.ILSpyCmd`
projects). The GUI, AddIns, Installer, and PowerShell front-ends are out of scope.

The *what and why* of the port lives in [`../PORT_PLAN.md`](../PORT_PLAN.md); the
*how* (file/name/namespace mirroring, C#-to-C++ construct mapping) lives in
[`../MIRROR_LAYOUT_REORGANIZATION.md`](../MIRROR_LAYOUT_REORGANIZATION.md).

## Status

Phase 0, the bulk of Phase 1 (metadata), the start of Phase 2 (type system),
and the start of Phase 3 (the ILAst model + a straight-line IL reader) are
implemented and green here. Everything else follows the phase plan in
`PORT_PLAN.md`:

- **Phase 0** -- build system, `Util/` primitives (UTF-8/16, `Span`, `ImmutableStack`),
  the three targets, and a Google Test driver. DONE.
- **Phase 1** -- `Decompiler/Metadata/` on the vendored `microsoft/winmd` ECMA-335
  baseline: method-body decoding (ECMA-335 II.25.4 tiny/fat + exception handlers),
  signature decoding (Type/Method/Field -> IType), the entity surface (TypeDef/
  Method/Field/Property + base types + custom attributes + TypeKind derivation),
  token resolution, and an IL text disassembler. DONE except: Portable PDB debug
  tables, WebCIL, single-file bundles, the assembly resolver (`.deps.json`).
- **Phase 2** -- `Decompiler/TypeSystem/`: naming primitives (`TopLevelTypeName`,
  `FullTypeName`), `KnownTypeCode` (full 60-entry table), the `IType` hierarchy
  (`KnownType`/`SimpleType`/`ParameterizedType`/`ArrayType`/`ByReferenceType`/
  `PointerType`/`TypeParameter`/`SpecialType`), `DeriveTypeKind`. STARTED (the full
  entity layer, `ICompilation`/`MetadataModule`, interning remain).
- **Phase 3** -- `Decompiler/IL/`: the ILAst instruction model (`OpCode` (101,
  verbatim from the generated `Instructions.cs`), `InstructionFlags`, `SlotInfo`,
  `StackType`, `ILVariable`, the `ILInstruction` strict-tree base with
  `CheckInvariant`, `Block`/`BlockContainer`/`ILFunction`, ~30 instruction kinds)
  and a branch- and EH-aware IL reader (`ReadIL`): blocks decode flat, then the
  `BlockBuilder` nests try/handler/filter regions into
  `TryCatch`/`TryFinally`/`TryFault` containers (IL `leave` decodes as a plain
  branch out of the region; `endfinally`/`endfilter` leaves get the innermost
  container). Evaluation-stack values crossing a boundary flush to S_ stack
  slots that unify across predecessors via a merge-representative map.
  **25315 of 25315** mscorlib method bodies decode (100%; all 1651 EH methods
  nested; merged stack slots for cross-block values), zero dangling branches.
  Signatures decode via a hand-rolled ECMA-335 II.23.2 blob parser covering the
  full element-type range (TypedReference, vararg sentinels, fn-ptr handled
  approximately, custom modifiers), which winmd's WinRT-profile TypeSig rejects.
  The remaining instruction kinds (and the ~40 IL transforms) follow Phase 4.
- **Phase 4 (in progress)** -- the ILAst transform pipeline: `IILTransform` /
  `ILTransformContext` (Transforms/), variable/block usage analysis
  (`ControlFlow/VariableUsage`), the pipeline's first transform
  `ControlFlowSimplification` (branch-chain collapse, dead stack-slot store
  removal, debug return-block inlining, branch-to-leave folding, single-edge
  block merging), `ILInlining` (single-use variable inlining + dead pure
  store removal), `InlineReturnTransform` (duplicate shared return blocks so
  each `stloc V; br ret` gets a 1-pred return block CFS then merges), and the
  FlowAnalysis foundation (`ControlFlowNode`, `Dominance` --
  Cooper-Harvey-Kennedy dominators, `ControlFlowGraph` -- per-container CFG).
  `LoopDetection` wraps back edges in loop containers; `ConditionDetection`
  inlines single-pred fall-through into if/else and inverts `if (cond) goto X
  else { exit }` to `if (!cond) { exit }` (early-exit, condition negation),
  turning sequential if-throw chains (e.g. `System.Version..ctor`) into clean
  `if (arg < 0) { throw }` with no gotos. `InlineReturnTransform` duplicates
  shared return blocks; `StObjToStLoc` turns `*(&V) = value` into `V = value`;
  `AssignVariableNames` renames `V_0` to type-inferred names (`num`, `text`,
  ...); `RemoveRedundantReturn` drops trailing `return;`. Parameter names and
  string literals (`ldstr`) now come from the metadata (Param table / #US heap).
  9 of ~40 transforms ported. The CLI applies CFS + StObjToStLoc + ILInlining +
  InlineReturnTransform + CFS + LoopDetection + ConditionDetection +
  AssignVariableNames + RemoveRedundantReturn before the C# seed. Next per
  `GetILTransforms()`: SplitVariables (needs reaching-definitions dataflow),
  DetectExitPoints + the full ConditionDetection (multi-pred join blocks),
  HighLevelLoopTransform (while/for), TransformAssignment, the
  async/iterator state machines, ...
- **Phase 5 (seed)** -- `Decompiler/CSharp/ILAstToCSharp`: an ILAst -> C#-text
  walker that closes the IL -> ILAst -> text pipeline end-to-end ahead of the
  real back end. It now produces readable C#: real parameter names (Param
  table) and string literals (#US heap), type-inferred local names (`num`,
  `text`, `array`) declared with C# keywords (`int num`, `double x`),
  `if/else` for fall-through + early-exit if-throw chains + shared-tail merges
  with condition negation, `while (true) { ... }` loops with `continue`/`break`,
  `base(args)` for base-ctor calls, `receiver.Method(args)` for instance
  calls, compound assignments (`V++`, `V += expr`), `value == null` for object
  null checks, `arr.Length` (no redundant `(int)` cast), `-x` for `0 - x`,
  `*(this)`/`*(byref)` rendered as the bare name, and generic `newobj` with
  its type. No trailing `return;` / no duplicate loop labels. Remaining gaps
  vs the real back end: gotos for multi-pred join blocks and loop-internal
  condition/increment jumps (needs DetectExitPoints + HighLevelLoopTransform),
  full type names (no `using` directives), and overload-resolved casts --
  these land with the Phase 5 C# AST + resolver.
- Phases 4-11 (IL transforms, C# AST + resolver + output, disassembler output,
  orchestration, ILSpyX, BamlDecompiler, the full `ilspycmd`, integration) -- per
  `PORT_PLAN.md`.

The CLI does `<assembly> --il [-t Type]` (IL text disassembly),
`<assembly> --ilast[-all] [-t Type]` (decode bodies to an ILAst tree and dump
it), and `<assembly> --csharp [-t Type]` (translate decodable bodies to
C#-ish text via the Phase 5 seed) end-to-end today; real C# output is Phase 5.

## Prerequisites

- CMake >= 3.15 (4.x works; the VS 2026 and Ninja generators are supported).
- A C++17 compiler: **Visual Studio 2026** (MSVC 14.5x, recommended), or
  GCC 9+ / Clang 10+ on Linux.
- [vcpkg](https://github.com/microsoft/vcpkg), with the `VCPKG_ROOT` environment
  variable pointing at your checkout. Dependencies (see `vcpkg.json`):
  `nlohmann-json`, `plog`, `lz4`, `cxxopts`, `gtest`.

## Build

Set `VCPKG_ROOT` to your vcpkg checkout first.

```sh
# Windows (Visual Studio 2026 generator -> ilspy.slnx solution). Recommended;
# the generator finds VS 2026 and the Windows 10 SDK itself, no dev-prompt
# needed:
cmake --preset windows-vs2026
cmake --build build/windows-vs2026 --config Debug
ctest --test-dir build/windows-vs2026 -C Debug --output-on-failure

# Windows (Ninja, VS dev environment) -- run from a Developer Command Prompt
# or source vcvarsall.bat x64 first so cl/ninja are on PATH:
cmake --preset windows-ninja
cmake --build build/windows-ninja
ctest --test-dir build/windows-ninja --output-on-failure

# Linux/macOS (Ninja, run from a shell with a compiler on PATH):
cmake --preset linux-ninja
cmake --build build/linux-ninja
```

The VS 2026 generator writes `build/windows-vs2026/ilspy.slnx` -- the modern
XML solution format VS 2026 defaults to (the successor to legacy `.sln`).
vcpkg restores the manifest dependencies into `build/<preset>/vcpkg_installed/`
on the first configure. `miniz` is optional during Phase 0-6 (see `vcpkg.json`
`deflate` feature and PORT_PLAN.md decision D12); it becomes required at Phase 7/8.

## Targets

| Target       | Type        | Description                                   |
|--------------|-------------|-----------------------------------------------|
| `ilspy`      | static lib  | The aggregate core library.                   |
| `ilspy_cli`  | executable | The `ilspycmd` port (Phase 10 fills it in).   |
| `ilspy_tests`| executable | Google Test driver; `ctest` runs it.          |

```sh
ctest --test-dir build/<preset> --output-on-failure
# or run the driver directly to use --gtest_filter
./build/<preset>/tests/ilspy_tests --gtest_filter='Util_*'
```

## Conventions (C# -> C++; see PORT_PLAN.md section 9 for the full table)

- **Encoding**: UTF-8 (`std::string`) everywhere internally; convert at the
  `#US`-heap / output boundaries via `Decompiler/Util/Utf.hpp`.
- **Ownership**: `std::unique_ptr` for exclusive ownership; `std::shared_ptr`
  only where sharing is real and cycles are provably absent; raw `T*`/`T&` for
  non-owning observation. No bare `new`/`delete`.
- **Nullability**: `std::optional<T>` for value-like nullable returns; raw `T*`
  may be null only where documented; references are never null.
- **Immutability**: `std::vector` is immutable-by-convention after construction
  where the C# used `ImmutableArray<T>`; `Decompiler/Util/ImmutableStack.hpp`
  is the persistent stack the IL reader needs for evaluation-stack snapshots.
- **Invariants**: the ported `CheckInvariant` runs in debug builds (`NDEBUG`
  off) after every transform, exactly as in C# -- it is the safety net for the
  loss of C# nullable-reference-type checking.
- **No `-Werror`** until Phase 11 (decision D6).

## Layout

```
cpp/
  Decompiler/            <- ICSharpCode.Decompiler
    Util/                <- Util primitives (UTF-8/16, Span, ImmutableStack)
    Metadata/            <- Metadata layer (Phase 1)
      Ecma335/winmd/     <- vendored microsoft/winmd headers (MIT; pinned commit)
  ILSpyX/                <- ICSharpCode.ILSpyX core subset (Phase 8)
  BamlDecompiler/        <- ICSharpCode.BamlDecompiler (Phase 9)
  ILSpyCmd/              <- ICSharpCode.ILSpyCmd CLI (Phase 10)
  tests/                 <- gtest, mirroring source dirs
```
