# microsoft/winmd (vendored)

Vendored snapshot of [microsoft/winmd](https://github.com/microsoft/winmd), the
MIT-licensed, header-only C++17 ECMA-335 reader that powers C++/WinRT. It is the
baseline for the port's metadata layer (PORT_PLAN.md section 5.1).

- **Upstream commit:** `a363c851a65c4e15743d6589acc1723e9e21b143`
- **License:** MIT (see `LICENSE`); copyright (c) Microsoft Corporation.
- **Scope:** PE container (`pe.h`), the full ECMA-335 table set, coded indices,
  signature decoders, custom-attribute decoding.

These files are **C++-only** (no C# counterpart) and are kept verbatim from
upstream. Do not edit them in place; if a fix is needed, patch upstream and
re-vendor a new pinned commit, recording the new hash here.

## Gaps filled by the port (live alongside this directory, not in winmd/)

winmd targets WinMD (metadata-only) files, so it omits what the decompiler
needs on top. These are added by Phase 1 of `PORT_PLAN.md`, in
`Decompiler/Metadata/` and its `Ecma335/` subdirectory:

- Method-body / IL reading (fat + tiny headers, IL bytes by RVA, local-var
  sigs, exception regions).
- Portable PDB debug tables (Document, MethodDebugInformation, LocalScope, ...).
- WebCIL and single-file bundle extraction (miniz/lz4).
- `.deps.json` / `.runtimeconfig.json` parsing (nlohmann-json) for the
  assembly resolver.
- The ILSpy `MetadataFile` / `PEFile` ergonomics adapter that the type system,
  IL reader, and back end consume.

The first of those, `Decompiler/Metadata/MetadataFile.{hpp,cpp}`, wraps winmd's
`database` behind a pimpl so that `<windows.h>` (included by winmd on Windows)
stays out of the public header and the rest of the codebase.

## Local accommodations (no vendored files edited)

`Decompiler/Metadata/Ecma335/WinmdInclude.hpp` is the single include point for
the winmd headers. winmd's `XLANG_ASSERT` (active in `_DEBUG` builds) encodes
WinMD-only invariants that do not hold for general .NET assemblies (e.g.
`signature.h` asserts that `ElementType::TypedByRef` never appears in a
`ParamSig`, but `mscorlib` has `System.TypedReference` parameters). The wrapper
disables `XLANG_ASSERT` by undefining `_DEBUG` around the winmd include (after
pre-including the standard-library headers with `_DEBUG` so the debug CRT ABI
is unaffected), then restores `_DEBUG`. This keeps the vendored headers
untouched; if winmd ever fixes these asserts upstream, the wrapper can be
simplified.
