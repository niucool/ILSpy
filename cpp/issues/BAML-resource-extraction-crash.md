# BAML resource extraction crashes: `--resource <x>.baml` (vector out-of-bounds → heap corruption in Release)

Tracking ticket for a memory-safety bug in the C++ port's BAML path.
Filed from the `cpp/` tree; no upstream issue tracker is reachable from this
checkout (the Gitea remote requires auth), so this file is the record until it
is mirrored to one.

* **Component:** `cpp/BamlDecompiler/` on the `ilspycmd` path
  (`cpp/ILSpyCmd/IlspyCmdProgram.cpp` `ExtractResource` →
  `ResourceExtensions.cpp` `DecompileBaml` → `BamlDecompilerTypeSystem` /
  `XamlDecompiler`).
* **Severity:** crash / memory corruption (untrusted input reaches it — a
  resource name in any .NET assembly). Not a graceful "unsupported" path.
* **Status:** open, reproducible.

## Steps to reproduce

```bat
:: Release build (crash)
cpp\build\windows-vs2026\ILSpyCmd\Release\ilspy_cli.exe ^
  --resource "PresentationFramework.Aero.g.resources/themes/aero.normalcolor.baml" ^
  "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\WPF\PresentationFramework.Aero.dll"

:: Debug build (assertion + abort)
cpp\build\windows-vs2026\ILSpyCmd\Debug\ilspy_cli.exe ^
  --resource "PresentationFramework.Aero.g.resources/themes/aero.normalcolor.baml" ^
  "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\WPF\PresentationFramework.Aero.dll"
```

The resource name is printed by `--list-resources` (grep for `baml`); any real
WPF `.baml` tried behaves the same (`PresentationFramework.Classic.dll`,
`System.Windows.Controls.Ribbon.dll`).

## Observed

**Release** — no output, process dies (bash exit 139; a fatal heap failure, not
a clean error). `cdb -g -G -c "g; kb; q"` shows the failure raised **inside the
heap allocator**, i.e. the corruption is detected on a later allocation:

```
ntdll!RtlReportFatalFailure
ntdll!RtlpAnalyzeHeapFailure
ntdll!RtlpAllocateHeap
...
ucrtbase!malloc_base
ilspy_cli+0x29e13
ilspy_cli+0x20753e
ilspy_cli+0x19cfd4
ilspy_cli+0x196d65
ilspy_cli+0x203c02
...
```

The Release exe has no PDB next to it, so frames are offsets only.

**Debug** (same source, has PDB) — the real cause, an out-of-bounds
`std::vector` access, caught by the iterator-debug check:

```
Debug Assertion Failed!
Program: ...\ILSpy\cpp\build\windows-vs2026\ILSpyCmd\Debug\ilspy_cli.exe
File: ...\VC\Tools\MSVC\14.51.36231\include\vector
Line: 1939
Expression: vector subscript out of range
```

then `abort()` (process exit code 3). In Release the same access is
unchecked and corrupts the heap instead.

## Expected

Emit the XAML for the resource (as `--resource` does for every non-BAML
resource), or fail with a clean diagnostic — never corrupt the heap.

## Not affected

* `--resource <non-baml>` works in both builds (e.g. `--resource split.cur
  PresentationFramework.dll` → exit 0).
* `--list-resources`, `--il`, `--ilast`, `-l`, `--dump-table` are unaffected.

## Environment

* Repo: `C:\Projects\github\ILSpy`, branch `gnhf/read-port-md-and-con-33f626`.
* HEAD at report time: `d8ef02c76ca801e5547346b643d25d5688b0e7ab` (gnhf 182).
* Binaries tested (predate HEAD by one iteration):

  | build | path | sha256 (first 16) |
  |---|---|---|
  | Release | `cpp/build/windows-vs2026/ILSpyCmd/Release/ilspy_cli.exe` | `ba6501904fe88951` |
  | Debug | `cpp/build/windows-vs2026/ILSpyCmd/Debug/ilspy_cli.exe` | `0a25ec3301f72438` |

* Toolset: VS 18 Insiders MSVC `14.51.36231`, x64, Windows.
* Sample: `PresentationFramework.Aero.dll` from
  `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\WPF` (any framework WPF
  assembly reproduces).

## Notes for the fixer

* Reproduce under the **Debug** build to get the assertion (the Release
  manifestation gives no usable stack); then step out of the failed
  `std::vector::operator[]` to the BAML reader/decompiler frame.
* The failing access is in the BAML→XAML path, not in metadata/IL handling:
  `DecompileBaml` builds a `BamlDecompilerTypeSystem` and an `XamlDecompiler`
  over the resource bytes. Prime suspects are the record/string-table lookups
  in `cpp/BamlDecompiler/` that index a vector by an id read from the BAML
  stream without a range check.
* TDD note: a regression test wants a small `.baml` fixture (or a synthetic
  reader test over a truncated/forged record stream), so this currently
  depends on `ILSpy-tests` fixtures or a new hand-built one.
* `--resource` takes the resource name from the caller; treat a bad/oversized
  record id as `InvalidDataException` (the documented BamlReader rejection
  path — `EX_SOFTWARE`), which the CLI already has an arm for.
