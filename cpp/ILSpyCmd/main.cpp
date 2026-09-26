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
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// ilspycmd (C++ port) entry point. Phase 10 fills in the full option set;
// this wires the options the ported phases expose. The -il/
// --il-sequence-points path renders the whole-module IL through the
// ReflectionDisassembler (IlspyCmdProgram.cpp: the IlspyCmdProgram ShowIL/
// TryLoadPDB port), with -usepdb loading variable names/sequence points from
// the module's PDB; the --ilast/--ilast-all paths dump method bodies as
// ILAst trees; --csharp translates them through the transform pipeline.

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectExitPoints.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/ControlFlow/RemoveUnreachableBlocks.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/TransformAssignment.hpp"
#include "Decompiler/IL/Transforms/UserDefinedLogicTransform.hpp"
#include "Decompiler/IL/Transforms/InterpolatedStringTransform.hpp"
#include "Decompiler/IL/Transforms/FixRemainingIncrements.hpp"
#include "Decompiler/IL/Transforms/NullCoalescingTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/Transforms/NullPropagationTransform.hpp"
#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"
#include "Decompiler/IL/Transforms/CopyPropagation.hpp"
#include "Decompiler/IL/Transforms/ReduceNestingTransform.hpp"
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "ILSpyCmd/IlspyCmdProgram.hpp"
#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "ILSpyCmd/MetadataTableDumper.hpp"
#include "ILSpyCmd/TypesParser.hpp"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

// cxxopts' vector options split every value on ',' by default
// (CXXOPTS_VECTOR_DELIMITER). The -l/--list values must reach the port's
// SplitEntityTypeValues UNsplit: the C# String.Split(',', ';') preserves
// the empty entries a trailing delimiter produces ("c," is two values,
// which changes the ParseSelection path -- the single-value gate), and
// ';' is not a cxxopts delimiter at all. Disabling the delimiter makes the
// vector accumulate one entry per occurrence, whole, the faithful input
// shape for the port's own split.
#define CXXOPTS_VECTOR_DELIMITER '\0'
#include <cxxopts.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::Metadata;

int RunMain(int argc, char** argv) {
    cxxopts::Options options("ilspycmd",
        "C++ port of the ILSpy command-line decompiler (core + CLI)");
    options.allow_unrecognised_options();
    options.add_options()
        ("h,help", "Print help")
        ("v,version", "Print version")
        ("assembly", "Assembly file to decompile", cxxopts::value<std::string>())
        ("il,ilcode", "Show IL code.", cxxopts::value<bool>()
            ->default_value("false")->implicit_value("true"))
        ("ilast", "Decode straight-line method bodies into an ILAst tree and dump it",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("ilast-all", "Decode method bodies (branch-aware) into an ILAst tree and dump it",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("csharp", "Translate method bodies to C#-ish text (Phase 5 seed: gotos for control flow, var locals, approximate casts/names -- the real resolver back end lands in Phase 5)",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("l,list", "Lists all entities of the specified type(s). Valid types: c(lass), i(nterface), s(truct), d(elegate), e(num)",
            cxxopts::value<std::vector<std::string>>())
        ("list-resources", "Lists all embedded resources in the assembly. Entries inside .resources containers are listed individually as '<container>/<entry>'.",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("resource", "Extract a single resource by name (as printed by --list-resources). Resources whose name ends with '.baml' are decompiled to XAML.",
            cxxopts::value<std::string>()->default_value(""))
        ("dump-table", "Dump a metadata table: prints RID, token, names, heap offsets and coded indexes of every row. <table> is the ECMA-335 table name (e.g. TypeDef, Property, MethodSemantics; case-insensitive) or table number (decimal or 0x-prefixed hex, e.g. 0x17).",
            cxxopts::value<std::string>()->default_value(""))
        ("json", "Output as JSON. Currently only supported together with --dump-table.",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("t,type", "Restrict --ilast/--ilast-all/--csharp to a single type by full name (Namespace.Type)",
            cxxopts::value<std::string>())
        ("il-sequence-points", "Show IL with sequence points. Implies -il.",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("usepdb,use-varnames-from-pdb", "Use variable names from PDB. With a value (--usepdb=<file>), the PDB file to use; without, the PDB is discovered from the assembly's debug directory.",
            cxxopts::value<std::string>()->implicit_value(""))
        ("o,outputdir", "The output directory, if omitted decompiler output is written to standard out.",
            cxxopts::value<std::string>())
        ("r,referencepath", "Path to a directory containing dependencies of the assembly that is being decompiled.",
            cxxopts::value<std::vector<std::string>>())
        ("d,dump-package", "Dump package assemblies into a folder. This requires the output directory option.",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"));
    options.parse_positional({ "assembly" });

    // A malformed option form must not crash the tool: cxxopts throws
    // (e.g. `-il`, which cxxopts parses as the short group -i -l and rejects
    // with a missing argument for l -- the real ilspycmd's McMaster parser
    // accepts single-dash long options, cxxopts does not, so the port's
    // long options take the --option form; a documented CLI-parse
    // divergence). Print the error and exit instead of letting the
    // exception terminate the process (which the MSVC CRT reports as a
    // fail-fast abort, or a blocked abort dialog in a Debug build).
    cxxopts::ParseResult parsed = [&]() {
        try {
            return options.parse(argc, argv);
        } catch (const cxxopts::exceptions::exception& ex) {
            std::cerr << "ilspycmd: " << ex.what() << '\n'
                      << "See --help for the option syntax (long options take the --option form).\n";
            std::exit(1);
        }
    }();
    if (parsed.count("help") != 0) {
        std::cout << options.help() << '\n';
        return 0;
    }
    if (parsed.count("version") != 0) {
        std::cout << "ilspycmd (C++ port) 1.0.0\n";
        return 0;
    }
    if (parsed.count("assembly") == 0) {
        std::cout << "ilspycmd: no input assembly given. See --help.\n"
                     "(Decompilation to C# is Phase 10 of PORT_PLAN.md; --il works now.)\n";
        return 0;
    }

    std::string asmPath = parsed["assembly"].as<std::string>();
    bool wantIl = parsed.count("il") != 0 && parsed["il"].as<bool>();
    bool wantIlSequencePoints = parsed.count("il-sequence-points") != 0
        && parsed["il-sequence-points"].as<bool>();
    bool wantIlAst = parsed.count("ilast") != 0 && parsed["ilast"].as<bool>();
    bool wantIlAstAll = parsed.count("ilast-all") != 0 && parsed["ilast-all"].as<bool>();
    bool wantCSharp = parsed.count("csharp") != 0 && parsed["csharp"].as<bool>();
    // The C# `-l|--list <entity-type(s)>` (CommandOptionType.MultipleValue):
    // one value per occurrence (cxxopts accumulates the vector; the ','
    // delimiter is disabled above so the values reach the port's split
    // unsplit).
    std::vector<std::string> listValues;
    if (parsed.count("list") != 0)
        listValues = parsed["list"].as<std::vector<std::string>>();
    std::string dumpTable = parsed.count("dump-table") != 0 ? parsed["dump-table"].as<std::string>() : "";
    std::string typeFilter = parsed.count("type") != 0 ? parsed["type"].as<std::string>() : "";
    // The C# `(bool IsSet, string Value) InputPDBFile` -- the -usepdb option
    // (SingleOrNoValue). The cxxopts implicit_value shape cannot distinguish
    // the bare form from an explicitly empty value, so both map to the C#
    // null Value (the LoadSymbols discovery); a non-empty value names the
    // PDB (the FromFile path).
    ILSpy::ILSpyCmd::InputPDBFile pdbFile;
    if (parsed.count("usepdb") != 0) {
        pdbFile.IsSet = true;
        std::string pdbValue = parsed["usepdb"].as<std::string>();
        if (!pdbValue.empty())
            pdbFile.Value = pdbValue;
    }

    bool wantListResources = parsed.count("list-resources") != 0
        && parsed["list-resources"].as<bool>();
    std::string resourceName = parsed.count("resource") != 0
        ? parsed["resource"].as<std::string>() : "";
    bool wantJson = parsed.count("json") != 0 && parsed["json"].as<bool>();
    bool wantDumpPackage = parsed.count("dump-package") != 0
        && parsed["dump-package"].as<bool>();
    // The C# `string outputDirectory = ResolveOutputDirectory(OutputDirectory)`
    // (IlspyCmdProgram.cs OnExecuteAsync): resolved BEFORE any action
    // dispatch, and created when set (Directory.CreateDirectory) -- the
    // side effect happens whatever action the other flags select. The C#
    // calls Directory.CreateDirectory OUTSIDE its try block, so a path that
    // cannot be created (an existing file, an unwritable parent) is an
    // UNHANDLED IOException that crashes the tool; the port's throwing
    // std::filesystem::create_directories call reproduces the same
    // uncaught-crash shape (a terminate instead of a managed stack trace
    // -- the observable exit code differs, 0xC0000409 vs 0xE0434352).
    std::optional<std::string> outputDirectory;
    if (parsed.count("outputdir") != 0) {
        outputDirectory = ILSpy::ILSpyCmd::ResolveOutputDirectory(
            parsed["outputdir"].as<std::string>());
        if (outputDirectory.has_value())
            std::filesystem::create_directories(
                ILSpy::ILSpyCmd::ToNativePath(*outputDirectory));
    }
    // The C# `string[] ReferencePaths` option (the -r/--referencepath
    // MultipleValue): one vector entry per occurrence, consumed by the
    // --resource BAML arm (the C# loops over the static Options property;
    // the port passes the values through).
    std::vector<std::string> referencePaths;
    if (parsed.count("referencepath") != 0) {
        for (const auto& p : parsed["referencepath"].as<
                 std::vector<std::string>>())
            referencePaths.push_back(p);
    }
    // The C# `if (JsonOutputFlag && DumpTableName == null)` usage check
    // (IlspyCmdProgram.cs): --json alone is rejected before any file opens.
    if (wantJson && dumpTable.empty()) {
        std::cerr << "The --json option is currently only supported together with --dump-table.\n";
        return 64;  // ProgramExitCodes.EX_USAGE
    }

    // The C# -o writer branches' file arm (the `output = File.CreateText(
    // Path.Combine(outputDirectory, outputName) + extension)` assignments
    // in PerformPerFileAction): the finished render goes to the per-action
    // output file (File.CreateText + the finally `output.Close()` flush),
    // nothing to stdout. A create failure is the C# IOException escaping to
    // the OnExecuteAsync global catch -- the message to stderr and
    // EX_SOFTWARE (the port renders no managed stack trace). Note the C#
    // replaces the shared writer per input file and closes it only at the
    // end of the run (a multi-assembly tail-loss quirk); the port CLI takes
    // a single assembly, so the at-once buffer write is lossless.
    auto WriteActionOutput = [](const std::string& path,
                                 const std::string& contents) -> bool {
        try {
            ILSpy::ILSpyCmd::WriteOutputFile(path, contents);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << '\n';
            return false;
        }
        return true;
    };

    if (!wantIl && !wantIlSequencePoints && !wantIlAst && !wantIlAstAll && !wantCSharp && listValues.empty() && !wantListResources && dumpTable.empty() && resourceName.empty() && !wantDumpPackage) {
        std::cout << "ilspycmd: see --help for available options (--il, --il-sequence-points, --ilast, --ilast-all, --csharp, --list, --list-resources, --resource, --dump-table, -d).\n";
        return 0;
    }

    // The C# DumpPackage arm (IlspyCmdProgram.cs PerformPerFileAction, the
    // `else if (DumpPackageFlag)` branch -- it sits AFTER the EntityTypes,
    // ShowIL and CreateDebugInfo arms and BEFORE the ListResources,
    // ResourceName and DumpTableName arms, so the C# runs it only when none
    // of those later-ordered flags is set; -genpdb is unported, and the
    // operative condition below encodes the same precedence). The port must
    // also run it BEFORE the MetadataFile open below: a single-file bundle
    // is not a CLI metadata image, and the C# opens it as raw bytes
    // (MemoryMappedFile), never as a PEFile.
    if (wantDumpPackage && listValues.empty() && !wantIl && !wantIlSequencePoints
        && !wantListResources && resourceName.empty() && dumpTable.empty()) {
        // The C# [FilesExist] validation over the assembly argument (the
        // McMaster option attribute on InputAssemblyNames): a missing input
        // file is rejected before any action runs, with the validation
        // message and exit code -- the port's other actions keep their own
        // MetadataFile gate (rc 1 with the could-not-open line).
        if (!std::filesystem::exists(asmPath)) {
            std::cerr << "File '" << asmPath << "' does not exist!\n"
                         "Specify --help for a list of available options and commands.\n";
            return 1;
        }
        std::ostringstream errorBuffer;
        int rc;
        try {
            rc = ILSpy::ILSpyCmd::DumpPackage(asmPath, outputDirectory, errorBuffer);
        } catch (const std::exception& ex) {
            // The C# global catch (`catch (Exception ex) { app.Error.WriteLine(ex.ToString());
            // return EX_SOFTWARE; }` around the OnExecuteAsync action
            // dispatch): the manifest validations and the Path.Combine null
            // argument escape here. The port renders the message (no managed
            // stack trace) with the same exit code.
            std::cerr << ex.what() << '\n';
            return 70;  // ProgramExitCodes.EX_SOFTWARE
        }
        // The error lines carry the CRLF TextWriter convention; stderr's
        // default text mode would translate every \n again (the same
        // \r\r\n doubling the --resource path fixed).
#if defined(_WIN32)
        int stderrFd = _fileno(stderr);
        int oldStderrMode = _setmode(stderrFd, _O_BINARY);
        std::cerr << errorBuffer.str();
        std::cerr.flush();
        if (oldStderrMode != -1)
            _setmode(stderrFd, oldStderrMode);
#else
        std::cerr << errorBuffer.str();
#endif
        return rc;
    }

    ILSpy::Decompiler::Metadata::MetadataFile file(asmPath);
    if (!file.IsValid()) {
        // The C# IlspyCmdProgram load-failure arms (ClassifyCliOpenFailure):
        // the exception's first line without the managed stack trace, with
        // the C# exit code; the missing-file arm adds the stdout usage
        // hint. The previously rendered "could not open ... as a CLI
        // assembly" line stays only for the unclassifiable arm.
        ILSpy::ILSpyCmd::CliOpenFailure failure =
            ILSpy::ILSpyCmd::ClassifyCliOpenFailure(asmPath);
        if (!failure.stdoutLine.empty())
            std::cout << failure.stdoutLine << '\n';
        std::cerr << failure.errorLine << '\n';
        return failure.exitCode;
    }

    // The C# EntityTypes arm (IlspyCmdProgram.cs PerformPerFileAction, the
    // `if (EntityTypes.Any())` branch): the option values split on ',' and
    // ';' (the OnExecuteAsync SelectMany), the TypesParser.ParseSelection
    // kinds selection, then the ListContent render -- every type definition
    // in the TypeDef table's row order (<Module> included) whose kind is
    // selected, as `{Kind} {ReflectionName}` lines. The -t type filter is
    // NOT applied here (the C# ListContent ignores TypeName). The per-kind
    // scaffold this replaces matched single characters anywhere in the
    // value and printed bare Namespace.Name forms.
    if (!listValues.empty()) {
        std::set<ILSpy::Decompiler::TypeSystem::TypeKind> kinds =
            ILSpy::ILSpyCmd::ParseSelection(ILSpy::ILSpyCmd::SplitEntityTypeValues(listValues));
        std::ostringstream buffer;
        int rc = ILSpy::ILSpyCmd::ListContent(asmPath, buffer, kinds);
        // The C# -o branch (`if (outputDirectory != null) output =
        // File.CreateText(Path.Combine(outputDirectory, outputName) +
        // ".list.txt")`): the render goes to the per-action output file,
        // nothing to stdout. The buffer carries the final CRLF text (the
        // TextWriter.WriteLine Environment.NewLine convention -- the same
        // bytes the C# file receives; File.CreateText encodes UTF-8 without
        // a BOM, so the port writes the buffer verbatim).
        if (outputDirectory.has_value()) {
            if (!WriteActionOutput(
                    ILSpy::ILSpyCmd::OutputFilePath(*outputDirectory, asmPath, ".list.txt"),
                    buffer.str()))
                return 70;  // ProgramExitCodes.EX_SOFTWARE
            return rc;
        }
        // The buffer carries the final CRLF text (the TextWriter.WriteLine
        // Environment.NewLine convention -- the same bytes the C# Console.Out
        // writes); the block is written in binary mode (the ShowIL pattern).
#if defined(_WIN32)
        int stdoutFd = _fileno(stdout);
        int oldMode = _setmode(stdoutFd, _O_BINARY);
        std::cout << buffer.str();
        std::cout.flush();
        if (oldMode != -1)
            _setmode(stdoutFd, oldMode);
#else
        std::cout << buffer.str();
#endif
        return rc;
    }

    // The C# ShowIL branch (IlspyCmdProgram.cs PerformPerFileAction: the
    // `ShowILCodeFlag || ShowILSequencePointsFlag` arm): the whole-module IL
    // render through the ReflectionDisassembler WriteModuleContents walk,
    // with the -usepdb PDB (variable names, and sequence points when
    // --il-sequence-points is set). The per-method DisassembleILText scaffold
    // this replaces was the pre-Phase-6 stand-in.
    if (wantIl || wantIlSequencePoints) {
        std::ostringstream buffer;
        int rc = 0;
        // The C# global catch around PerformPerFileAction covers the
        // disassembler's BadImageFormatException (the ReflectionDisassembler
        // event/type-token throws for a malformed row); the port renders the
        // message only (no managed stack trace) and exits EX_SOFTWARE.
        try {
            rc = ILSpy::ILSpyCmd::ShowIL(asmPath, buffer, wantIlSequencePoints, pdbFile);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << '\n';
            return 70;  // ProgramExitCodes.EX_SOFTWARE
        }
        // The C# -o branch (`output = File.CreateText(Path.Combine(
        // outputDirectory, outputName) + ".il")`): the render goes to the
        // per-action output file, nothing to stdout.
        if (outputDirectory.has_value()) {
            if (!WriteActionOutput(
                    ILSpy::ILSpyCmd::OutputFilePath(*outputDirectory, asmPath, ".il"),
                    buffer.str()))
                return 70;  // ProgramExitCodes.EX_SOFTWARE
            return rc;
        }
        // The buffer carries the final CRLF text (PlainTextOutput's kNewLine
        // convention -- the same bytes the C# Console.Out writes). stdout's
        // default text mode would translate every \n again (\r\n -> \r\r\n),
        // so the block is written in binary mode (restored after -- the other
        // paths print plain \n and rely on the text-mode translation).
#if defined(_WIN32)
        int stdoutFd = _fileno(stdout);
        int oldMode = _setmode(stdoutFd, _O_BINARY);
        std::cout << buffer.str();
        std::cout.flush();
        if (oldMode != -1)
            _setmode(stdoutFd, oldMode);
#else
        std::cout << buffer.str();
#endif
        return rc;
    }

    // The C# ListResources arm (IlspyCmdProgram.cs PerformPerFileAction,
    // the `else if (ListResourcesFlag)` branch -- it sits AFTER the
    // EntityTypes, ShowIL, CreateDebugInfo and DumpPackage arms and
    // BEFORE the ResourceName and DumpTableName arms, so a command line
    // naming several of those flags runs the earlier action): one line per
    // embedded manifest resource, with .resources containers expanded to
    // their '<container>/<entry>' entries (the ResourceExtensions port's
    // EnumerateResourcePaths).
    if (wantListResources) {
        std::ostringstream buffer;
        int rc = ILSpy::ILSpyCmd::ListResources(asmPath, buffer);
        // The C# -o branch (`output = File.CreateText(Path.Combine(
        // outputDirectory, outputName) + ".resources.txt")`): the render
        // goes to the per-action output file, nothing to stdout.
        if (outputDirectory.has_value()) {
            if (!WriteActionOutput(
                    ILSpy::ILSpyCmd::OutputFilePath(
                        *outputDirectory, asmPath, ".resources.txt"),
                    buffer.str()))
                return 70;  // ProgramExitCodes.EX_SOFTWARE
            return rc;
        }
        // The buffer carries the final CRLF text (the same TextWriter
        // convention ShowIL/ListContent render); stdout's default text
        // mode would translate every \n again, so the block is written in
        // binary mode (the ShowIL pattern).
#if defined(_WIN32)
        int stdoutFd = _fileno(stdout);
        int oldMode = _setmode(stdoutFd, _O_BINARY);
        std::cout << buffer.str();
        std::cout.flush();
        if (oldMode != -1)
            _setmode(stdoutFd, oldMode);
#else
        std::cout << buffer.str();
#endif
        return rc;
    }

    // The C# ResourceName arm (IlspyCmdProgram.cs PerformPerFileAction,
    // the `else if (ResourceName != null)` branch -- it sits AFTER the
    // EntityTypes, ShowIL, CreateDebugInfo, DumpPackage and ListResources
    // arms and BEFORE the DumpTableName arm, so a command line naming
    // several of those flags runs the earlier action): the resource lookup
    // through ResourceExtensions.TryGetResource -- with no -o a byte[]
    // value written raw to stdout (the Console.OpenStandardOutput binary
    // write), any other value written as its ToString() text (no trailing
    // newline), and the not-found arm rendering the available-resources
    // listing to stderr with EX_DATAERR. The byte buffer may hold
    // arbitrary bytes (including embedded NULs), so the block is written in
    // binary mode; the error listing carries the CRLF TextWriter convention.
    // The C# global catch (the `catch (Exception ex) { app.Error.WriteLine(
    // ex.ToString()); return EX_SOFTWARE; }` around PerformPerFileAction)
    // covers the value decode's BadImageFormatException for a malformed
    // container entry -- the port renders the message only (no managed
    // stack trace), same exit code. With -o set the value goes to the
    // output file instead (the SanitizeFileName-named extraction file the
    // ExtractResource port writes) and nothing to stdout.
    if (!resourceName.empty()) {
        std::ostringstream buffer;
        std::ostringstream errorBuffer;
        int rc;
        try {
            rc = ILSpy::ILSpyCmd::ExtractResource(
                asmPath, resourceName, buffer, errorBuffer, outputDirectory,
                referencePaths);
        } catch (const std::exception& ex) {
            // The C# global handler: the exception render and
            // EX_SOFTWARE (the port carries no managed stack trace).
            std::cerr << ex.what() << '\n';
            return 70;  // ProgramExitCodes.EX_SOFTWARE
        }
        if (!errorBuffer.str().empty()) {
            // The error listing carries the CRLF TextWriter convention;
            // stderr's default text mode would translate every \n again
            // (\r\n -> \r\r\n), so the block is written in binary mode
            // (the stdout pattern).
#if defined(_WIN32)
            int stderrFd = _fileno(stderr);
            int oldErrMode = _setmode(stderrFd, _O_BINARY);
            std::cerr << errorBuffer.str();
            std::cerr.flush();
            if (oldErrMode != -1)
                _setmode(stderrFd, oldErrMode);
#else
            std::cerr << errorBuffer.str();
#endif
        }
#if defined(_WIN32)
        int stdoutFd = _fileno(stdout);
        int oldMode = _setmode(stdoutFd, _O_BINARY);
        std::cout << buffer.str();
        std::cout.flush();
        if (oldMode != -1)
            _setmode(stdoutFd, oldMode);
#else
        std::cout << buffer.str();
#endif
        return rc;
    }

    if (!dumpTable.empty()) {
        // The C# DumpTableName arm (IlspyCmdProgram.cs PerformPerFileAction,
        // `else if (DumpTableName != null)` branch -- it sits AFTER the
        // EntityTypes, ShowIL, CreateDebugInfo, DumpPackage, ListResources and
        // ResourceName arms, so a command line naming several of those flags
        // runs the earlier action, not the table dump): the table-name parse
        // (an unknown name is a usage error -- the two stderr lines and
        // EX_USAGE, the ProgramExitCodes port) then the whole-table dump
        // through the MetadataTableDumper port (the aligned console table,
        // or the JSON document with --json).
        ILSpy::Decompiler::Metadata::CorTableIndex table;
        if (!ILSpy::ILSpyCmd::TryParseTableName(dumpTable, table)) {
            std::cerr << "Unknown metadata table '" << dumpTable << "'.\n";
            std::cerr << "Supported tables: "
                      << ILSpy::ILSpyCmd::SupportedTableNames() << '\n';
            return 64;  // ProgramExitCodes.EX_USAGE
        }
        std::ostringstream buffer;
        int rc = ILSpy::ILSpyCmd::DumpTable(asmPath, buffer, table, wantJson);
        // The C# -o branch (the per-file `using var tableOutput =
        // File.CreateText(Path.Combine(outputDirectory, outputName) +
        // $".{table}.{(JsonOutputFlag ? \"json\" : \"txt\")}")`: the
        // table-name parse runs first, so an unknown table creates no file;
        // the name comes from the TableIndex enum's ToString ("TypeDef"),
        // not the input spelling -- `--dump-table typedef` and `--dump-table
        // 0x02` both write <name>.TypeDef.txt/.json). The render goes to the
        // per-action output file, nothing to stdout.
        if (outputDirectory.has_value()) {
            std::string extension = std::string(".")
                + ILSpy::ILSpyCmd::TableName(table)
                + (wantJson ? ".json" : ".txt");
            if (!WriteActionOutput(
                    ILSpy::ILSpyCmd::OutputFilePath(
                        *outputDirectory, asmPath, extension),
                    buffer.str()))
                return 70;  // ProgramExitCodes.EX_SOFTWARE
            return rc;
        }
        // The buffer carries the final CRLF text (the same TextWriter
        // convention ShowIL renders); stdout's default text mode would
        // translate every \n again, so the block is written in binary mode
        // (the ShowIL pattern).
#if defined(_WIN32)
        int stdoutFd = _fileno(stdout);
        int oldMode = _setmode(stdoutFd, _O_BINARY);
        std::cout << buffer.str();
        std::cout.flush();
        if (oldMode != -1)
            _setmode(stdoutFd, oldMode);
#else
        std::cout << buffer.str();
#endif
        return rc;
    }

    // The C# default branch's -o writer (`output = File.CreateText(
    // Path.Combine(outputDirectory, outputName) + ".decompiled.cs")`, or
    // the -t TypeName-based name) is still a documented divergence: with -o
    // set the port writes to stdout. The --csharp render itself now rides
    // the facade's type-level entries (this slice).
    auto typeMatch = [&](const std::string& ns, const std::string& name) {
        if (typeFilter.empty()) return true;
        return (ns.empty() ? name : ns + "." + name) == typeFilter;
    };

    if (wantCSharp) {
        // The C# `new CSharpDecompiler(module, settings)` per input file
        // (IlspyCmdProgram.cs Decompile): one instance wires the type
        // system once and owns the partial-types registry; the renders go
        // through it. No -t renders the whole module (the C#
        // `output.Write(decompiler.DecompileWholeModuleAsString())`, line
        // 658); the -t filter renders the matched types through the
        // type-level entry (the C# DecompileTypes path -- no
        // module/assembly attribute sections).
        ::ILSpy::Decompiler::DecompilerSettings decompilerSettings;
        ::ILSpy::Decompiler::CSharp::CSharpDecompiler decompiler(
            file, decompilerSettings);
        std::string text;
        if (typeFilter.empty()) {
            text = decompiler.DecompileWholeModuleToString();
        } else {
            int typesPrinted = 0;
            for (const auto& t : file.TypeDefs()) {
                if (t.Name == "<Module>") continue;
                if (!typeMatch(t.Namespace, t.Name)) continue;
                std::string typeText;
                if (decompiler.DecompileTypeToString(t.Token, typeText,
                                                    /*wrapNamespace=*/true)) {
                    // The blank separator rides BETWEEN the matched types
                    // (the C# tree's inter-declaration spacing); the last
                    // render ends at its closing brace like the C# output.
                    if (typesPrinted > 0)
                        text += '\n';
                    text += typeText;
                    ++typesPrinted;
                }
            }
            if (typesPrinted == 0) {
                std::cerr << "ilspycmd: no members found for type '"
                          << typeFilter << "'\n";
                return 1;
            }
        }
        // The C# -o writer branch (IlspyCmdProgram.cs lines 406-411):
        // `File.CreateText(Path.Combine(outputDirectory,
        // (string.IsNullOrEmpty(TypeName) ? outputName : TypeName)) +
        // ".decompiled.cs")` -- the render goes to the per-file output,
        // nothing to stdout.
        if (outputDirectory.has_value()) {
            if (!WriteActionOutput(
                    ILSpy::ILSpyCmd::DecompiledOutputFilePath(
                        *outputDirectory, asmPath, typeFilter),
                    text))
                return 70;  // ProgramExitCodes.EX_SOFTWARE
            return 0;
        }
        std::cout << text;
        return 0;
    }

    int methodsPrinted = 0;
    for (const auto& t : file.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        if (!typeMatch(t.Namespace, t.Name)) continue;
        auto methods = file.GetMethods(t.Token);
        for (const auto& m : methods) {
            if (m.RVA == 0) continue;  // abstract/extern/pinvoke-only
            if (wantIlAstAll) {
                auto fn = ILSpy::Decompiler::IL::ReadIL(file, m.Token, m.RVA);
                if (!fn) continue;
                std::cout << ".method " << t.Namespace << "." << t.Name << "::" << m.Name
                          << "  (ILAst, branch-aware)\n";
                // Guard against a transform-induced cycle: WriteTo is a
                // recursive, unbounded walker, so a Parent-pointer cycle would
                // emit a repeated token (e.g. `lock (...)`) ad infinitum and
                // OOM the process. Detect the cycle first (generic walk) and
                // emit a marker instead of the runaway dump.
                if (fn->HasCycle()) {
                    std::cout << "  /* dump skipped: ILAst cycle detected "
                                 "(possible transform bug) */\n\n";
                } else {
                    std::cout << fn->ToString() << "\n\n";
                }
                ++methodsPrinted;
                continue;
            }
            if (wantIlAst) {
                // Decode the method body into an ILAst tree and dump it. Only
                // straight-line bodies (no branches/switch/exception handlers)
                // decode for now; others are skipped with a note.
                auto fn = ILSpy::Decompiler::IL::ReadStraightLineIL(file, m.Token, m.RVA);
                if (!fn) continue;
                std::cout << ".method " << t.Namespace << "." << t.Name << "::" << m.Name
                          << "  (ILAst)\n";
                if (fn->HasCycle()) {
                    std::cout << "  /* dump skipped: ILAst cycle detected "
                                 "(possible transform bug) */\n\n";
                } else {
                    std::cout << fn->ToString() << "\n\n";
                }
                ++methodsPrinted;
                continue;
            }
        }
    }
    if (methodsPrinted == 0) {
        std::cerr << "ilspycmd: no method bodies found"
                  << (typeFilter.empty() ? "" : " for type '" + typeFilter + "'") << '\n';
        return 1;
    }
    return 0;
}

#ifndef ILSPY_CMD_AS_LIB
// ILSPY_CMD_AS_LIB (the ilspy_cmd_lib target, the bennu umbrella's
// in-process dispatch surface — DOTNET_PLAN.md Phase B, mirroring hada_lib):
// the TU contributes RunMain (and DecompileLib.cpp's ilspy:: / ilspycmd::
// entries) but NOT a main symbol, so the umbrella can link this lib without
// a main collision. The guard wraps the whole win/nix entry pair.
#if defined(_WIN32)
// The Windows entry point: the Unicode command line converted to UTF-8
// before the option parse (the C runtime's narrow argv would transcode
// through the console's ANSI code page and mangle every non-ASCII option
// value -- a resource name like 'v2.resources/Unicode.Name.中文' must
// reach the lookup as the UTF-8 bytes the resource names carry; the real
// tool reads the same Unicode command line through its managed argv).
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> utf8Args;
    utf8Args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0,
            nullptr, nullptr);
        std::string arg;
        if (len > 1) {
            arg.resize(static_cast<std::size_t>(len - 1));
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), len,
                nullptr, nullptr);
        }
        utf8Args.push_back(std::move(arg));
    }
    std::vector<char*> utf8Argv;
    utf8Argv.reserve(utf8Args.size());
    for (auto& a : utf8Args)
        utf8Argv.push_back(a.data());
    return RunMain(argc, utf8Argv.data());
}
#else
int main(int argc, char** argv) {
    return RunMain(argc, argv);
}
#endif  // _WIN32
#endif  // ILSPY_CMD_AS_LIB
