// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR ANY OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the IlspyCmdProgram members the CLI's -il path consumes
// (ICSharpCode.ILSpyCmd/IlspyCmdProgram.cs): ShowIL (the whole-module IL
// render the -il/--il-sequence-points flags dispatch to) and TryLoadPDB
// (the -usepdb dispatch over DebugInfoUtils). The rest of the program class
// (the McMaster CommandLineUtils option attributes, the host wiring, and
// every other action) is Phase-10 later work; main.cpp parses the options
// with cxxopts (the plan's McMaster replacement) and calls these.
//
// C#-to-C++ porting decisions:
//  * The C# instance members port as free functions taking the parsed flag
//    state as parameters (the SRMExtensions free-function convention): the
//    C# reads the ShowILSequencePointsFlag/InputPDBFile instance fields.
//  * The C# `public (bool IsSet, string Value) InputPDBFile` (the
//    SingleOrNoValue option shape) ports as the InputPDBFile struct: IsSet
//    carries the flag's presence and Value the PDB file name, with nullopt
//    as the C# null Value the bare `-usepdb` form parses to. The cxxopts
//    parse cannot distinguish the bare form from an explicitly empty value
//    (`-usepdb ""`), so main.cpp maps BOTH to nullopt -- where the real C#
//    takes the FromFile("") path (whose empty-name guard yields no debug
//    info), the port takes the LoadSymbols discovery. A documented
//    CLI-parse-layer divergence (the plan's implicit_value choice).
//  * The C# `output.WriteLine($"// IL code: {module.Name}")` writes the
//    header line straight to the TextWriter the PlainTextOutput then wraps;
//    the port writes it to the ostringstream first. WriteLine uses
//    Environment.NewLine -- the port's kNewLine "\r\n" convention
//    (PlainTextOutput hardcodes the Windows value).
//  * The C# `new PEFile(assemblyFileName)` throws for a missing/unparseable
//    file (the CLI's catch renders the exception); the port's MetadataFile
//    never throws -- ShowIL over an unparseable file renders the header
//    line with an empty name and no contents and returns 0 (main.cpp gates
//    IsValid before dispatching).

#pragma once

#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>

namespace ILSpy::ILSpyCmd {

// The C# `public (bool IsSet, string Value) InputPDBFile` -- the parsed
// -usepdb option: IsSet is the flag's presence, Value the PDB file name
// (nullopt = the C# null of the bare `-usepdb` form).
struct InputPDBFile {
    bool IsSet = false;
    std::optional<std::string> Value;
};

// The C# `IDebugInfoProvider TryLoadPDB(PEFile module)`: the -usepdb
// dispatch -- the bare form discovers the module's PDB through the PE debug
// directory (LoadSymbols), the valued form opens the named PDB (FromFile),
// and an unset flag yields no debug info (null).
std::unique_ptr<Decompiler::DebugInfo::IDebugInfoProvider> TryLoadPDB(
    const Decompiler::Metadata::MetadataFile& module,
    const InputPDBFile& pdbFile);

// The C# `int ShowIL(string assemblyFileName, TextWriter output)`: the
// whole-module IL render -- the `// IL code: <name>` header line, then the
// ReflectionDisassembler over a PlainTextOutput wrapping the output with
// DebugInfo set from TryLoadPDB and ShowSequencePoints from the
// --il-sequence-points flag, rendering WriteModuleContents. Returns 0 (the
// C# return value).
int ShowIL(const std::string& assemblyFileName, std::ostringstream& output,
    bool showILSequencePoints, const InputPDBFile& pdbFile);

// The C# `int ListContent(string assemblyFileName, TextWriter output,
// ISet<TypeKind> kinds)` (IlspyCmdProgram.cs): the -l/--list render -- every
// type definition in the TypeDef table's ROW order (the C#
// `decompiler.TypeSystem.MainModule.TypeDefinitions` walk -- MetadataModule
// iterates metadata.TypeDefinitions, so <Module> is included and nested
// types appear at their physical rows) whose kind is selected, as
// `{Kind} {FullTypeName.ReflectionName}` lines: the TypeKind enum member
// name (Enum.ToString) and the SRMExtensions GetFullTypeName declaring-chain
// reflection name (the `n arity suffix and the '+' nesting separators).
// Returns 0 (the C# return value; a kinds set that selects nothing prints
// nothing). The -o writer branch (the <name>.list.txt file) is deferred with
// the project output paths.
int ListContent(const std::string& assemblyFileName, std::ostringstream& output,
    const std::set<ILSpy::Decompiler::TypeSystem::TypeKind>& kinds);

// The C# `int ListResources(string assemblyFileName, TextWriter output)`
// (IlspyCmdProgram.cs): the --list-resources render -- every embedded
// manifest resource as one line, with .resources containers expanded to
// their '<container>/<entry>' entries (the EnumerateResourcePaths port,
// ResourceExtensions.hpp). WriteLine uses Environment.NewLine -- the
// port's kNewLine "\r\n" convention (PlainTextOutput hardcodes the
// Windows value). Returns 0 (the C# return value; an assembly with no
// embedded resources prints nothing). The -o writer branch (the
// <name>.resources.txt file) is deferred with the project output paths.
int ListResources(const std::string& assemblyFileName, std::ostringstream& output);

// The C# `int ExtractResource(string assemblyFileName, string resourceName,
// TextWriter output, string outputDirectory, CommandLineApplication app)`
// (IlspyCmdProgram.cs): the --resource extraction -- the resource lookup
// through TryGetResource with the '<name>' not-found error (the two stderr
// lines plus the available-resources listing, rendered to errorOutput for
// the caller to flush) and EX_DATAERR (65); a byte[] value written raw into
// the output (the C# Console.OpenStandardOutput binary write -- the port's
// ostringstream holds arbitrary bytes and main.cpp writes the block in
// binary mode), and any other value written as its text (the C# ToString()
// -- the port's invariant-culture render, matching the real tool whose
// runtimeconfig sets System.Globalization.Invariant). The -o
// outputDirectory branches are deferred with the project output paths.
// The .baml arm (isBaml && a byte[] value -> DecompileBaml) is deferred with
// the Phase-9 BamlDecompiler: the port prints a not-yet-supported line to
// errorOutput and returns EX_SOFTWARE (70) -- the same exit code the real
// tool's own failure over an unparseable BAML stream produces.
int ExtractResource(const std::string& assemblyFileName,
    const std::string& resourceName, std::ostringstream& output,
    std::ostringstream& errorOutput);

// The C# `static string ResolveOutputDirectory(string outputDirectory)`
// (IlspyCmdProgram.cs): the -o/--outputdir value resolved BEFORE any action
// dispatch -- nullopt for an unset/whitespace value (the C# null, which the
// actions read as "write to standard out"), otherwise the .NET
// Path.GetFullPath shape: resolved against the current directory and
// normalized for '.'/'..' components. The port uses std::filesystem's
// absolute + lexically_normal pair (no '~' expansion -- Path.GetFullPath
// has none either on Windows).
std::optional<std::string> ResolveOutputDirectory(const std::string& outputDirectory);

// The C# `int DumpPackageAssemblies(string packageFileName, string
// outputDirectory, CommandLineApplication app)` (IlspyCmdProgram.cs): the
// -d/--dump-package action -- the whole file read into memory (the C#
// memory-maps it; the port's whole-buffer read is the established file
// convention), the SingleFileBundle.IsBundle check (the exact "Cannot dump
// assembiles for ..." stderr line -- the C# misspelling preserved -- plus
// EX_DATAERR), the manifest read, then one output file per entry: entries
// whose RelativePath escapes the output directory (a "../" component after
// the backslash-to-slash normalization, or a rooted path) are skipped with
// the "Skipping single-file entry ..." stderr line, uncompressed entries
// copy their bundle bytes, compressed entries inflate through the raw
// deflate decoder (the miniz tinfl machinery behind the embedded-PDB
// decode) with the produced-length check -- a mismatch renders the
// "Corrupted single-file entry ..." stderr line and EX_DATAERR, a corrupt
// deflate stream throws (the C# DeflateStream InvalidDataException
// escaping to the global catch: the caller renders the message and returns
// EX_SOFTWARE). A missing outputDirectory throws the C# ArgumentNullException
// shape (Path.Combine(null, ...) -- the global catch renders it with
// EX_SOFTWARE). The stdout convention does not apply: every diagnostic goes
// to errorOutput for the caller to flush, and files are written through
// UTF-16 paths so non-ASCII RelativePaths resolve correctly on Windows
// (the C# writes through System.IO's Unicode paths).
int DumpPackage(const std::string& packageFileName,
    const std::optional<std::string>& outputDirectory,
    std::ostringstream& errorOutput);

}  // namespace ILSpy::ILSpyCmd
