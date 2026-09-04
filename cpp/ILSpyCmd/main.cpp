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
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectExitPoints.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/ControlFlow/RemoveUnreachableBlocks.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ILReader.hpp"
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
#include <iostream>
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
            cxxopts::value<std::string>()->implicit_value(""));
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
    // The C# `if (JsonOutputFlag && DumpTableName == null)` usage check
    // (IlspyCmdProgram.cs): --json alone is rejected before any file opens.
    if (wantJson && dumpTable.empty()) {
        std::cerr << "The --json option is currently only supported together with --dump-table.\n";
        return 64;  // ProgramExitCodes.EX_USAGE
    }

    if (!wantIl && !wantIlSequencePoints && !wantIlAst && !wantIlAstAll && !wantCSharp && listValues.empty() && !wantListResources && dumpTable.empty() && resourceName.empty()) {
        std::cout << "ilspycmd: see --help for available options (--il, --il-sequence-points, --ilast, --ilast-all, --csharp, --list, --list-resources, --resource, --dump-table).\n";
        return 0;
    }

    ILSpy::Decompiler::Metadata::MetadataFile file(asmPath);
    if (!file.IsValid()) {
        std::cerr << "ilspycmd: could not open '" << asmPath << "' as a CLI assembly\n";
        return 1;
    }

    // The C# EntityTypes arm (IlspyCmdProgram.cs PerformPerFileAction, the
    // `if (EntityTypes.Any())` branch): the option values split on ',' and
    // ';' (the OnExecuteAsync SelectMany), the TypesParser.ParseSelection
    // kinds selection, then the ListContent render -- every type definition
    // in the TypeDef table's row order (<Module> included) whose kind is
    // selected, as `{Kind} {ReflectionName}` lines. The -t type filter is
    // NOT applied here (the C# ListContent ignores TypeName); the -o writer
    // branch (the <name>.list.txt file) is deferred with the project output
    // paths. The per-kind scaffold this replaces matched single characters
    // anywhere in the value and printed bare Namespace.Name forms.
    if (!listValues.empty()) {
        std::set<ILSpy::Decompiler::TypeSystem::TypeKind> kinds =
            ILSpy::ILSpyCmd::ParseSelection(ILSpy::ILSpyCmd::SplitEntityTypeValues(listValues));
        std::ostringstream buffer;
        int rc = ILSpy::ILSpyCmd::ListContent(asmPath, buffer, kinds);
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
        int rc = ILSpy::ILSpyCmd::ShowIL(asmPath, buffer, wantIlSequencePoints, pdbFile);
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
    // EnumerateResourcePaths). The -o per-file writer branch is deferred
    // with the project output paths (the port CLI has no --outputdir yet;
    // the C# writes <name>.resources.txt there).
    if (wantListResources) {
        std::ostringstream buffer;
        int rc = ILSpy::ILSpyCmd::ListResources(asmPath, buffer);
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
    // through ResourceExtensions.TryGetResource -- a byte[] value written
    // raw to stdout (the Console.OpenStandardOutput binary write), any other
    // value written as its ToString() text (no trailing newline), and the
    // not-found arm rendering the available-resources listing to stderr
    // with EX_DATAERR. The byte buffer may hold arbitrary bytes (including
    // embedded NULs), so the block is written in binary mode; the error
    // listing carries the CRLF TextWriter convention. The C# global catch
    // (the `catch (Exception ex) { app.Error.WriteLine(ex.ToString());
    // return EX_SOFTWARE; }` around PerformPerFileAction) covers the value
    // decode's BadImageFormatException for a malformed container entry --
    // the port renders the message only (no managed stack trace), same
    // exit code. The -o outputDirectory branches are deferred with the
    // project output paths.
    if (!resourceName.empty()) {
        std::ostringstream buffer;
        std::ostringstream errorBuffer;
        int rc;
        try {
            rc = ILSpy::ILSpyCmd::ExtractResource(
                asmPath, resourceName, buffer, errorBuffer);
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
        // the `else if (DumpTableName != null)` branch -- it sits AFTER the
        // EntityTypes, ShowIL, CreateDebugInfo, DumpPackage, ListResources and
        // ResourceName arms, so a command line naming several of those flags
        // runs the earlier action, not the table dump): the table-name parse
        // (an unknown name is a usage error -- the two stderr lines and
        // EX_USAGE, the ProgramExitCodes port) then the whole-table dump
        // through the MetadataTableDumper port (the aligned console table,
        // or the JSON document with --json). The -o per-file writer branch
        // is deferred with the project output paths (the port CLI has no
        // --outputdir yet; the C# writes <name>.<table>.txt/.json there).
        ILSpy::Decompiler::Metadata::CorTableIndex table;
        if (!ILSpy::ILSpyCmd::TryParseTableName(dumpTable, table)) {
            std::cerr << "Unknown metadata table '" << dumpTable << "'.\n";
            std::cerr << "Supported tables: "
                      << ILSpy::ILSpyCmd::SupportedTableNames() << '\n';
            return 64;  // ProgramExitCodes.EX_USAGE
        }
        std::ostringstream buffer;
        int rc = ILSpy::ILSpyCmd::DumpTable(asmPath, buffer, table, wantJson);
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

    auto typeMatch = [&](const std::string& ns, const std::string& name) {
        if (typeFilter.empty()) return true;
        return (ns.empty() ? name : ns + "." + name) == typeFilter;
    };

    int methodsPrinted = 0;
    for (const auto& t : file.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        if (!typeMatch(t.Namespace, t.Name)) continue;
        auto methods = file.GetMethods(t.Token);
        for (const auto& m : methods) {
            if (m.RVA == 0) continue;  // abstract/extern/pinvoke-only
            if (wantCSharp) {
                // IL -> ILAst -> C#-ish text, end to end. The ILAst goes
                // through the pipeline's first transform
                // (ControlFlowSimplification) before rendering.
                auto fn = ILSpy::Decompiler::IL::ReadIL(file, m.Token, m.RVA);
                if (!fn) continue;
                ILSpy::Decompiler::IL::ILTransformContext transformContext;
                ILSpy::Decompiler::IL::ControlFlowSimplification().Run(*fn, transformContext);
                ILSpy::Decompiler::IL::StObjToStLoc().Run(*fn, transformContext);
                ILSpy::Decompiler::IL::ILInlining().Run(*fn, transformContext);
                ILSpy::Decompiler::IL::InlineReturnTransform().Run(*fn, transformContext);
                // Remove infeasible paths: a block that stores a known constant
                // to a stack slot and branches to a multi-pred test block skips
                // the test (redirected to the feasible exit; dead store dropped).
                ILSpy::Decompiler::IL::RemoveInfeasiblePathTransform().Run(*fn, transformContext);
                // Detect pinned regions (`fixed` blocks): must run after inlining
                // and before loop detection (per the C# GetILTransforms() order).
                ILSpy::Decompiler::IL::DetectPinnedRegions().Run(*fn, transformContext);
                // Detect catch-when filter entry points: a `catch (T e) when (...)`
                // filter starts with a redundant isinst type test (the catch is
                // already typed T); drop it so the entry branches straight to the
                // when-condition block. Must run after inlining and before loop
                // detection (per the C# GetILTransforms() order).
                ILSpy::Decompiler::IL::DetectCatchWhenConditionBlocks().Run(*fn, transformContext);
                // ldloca; dup; initobj (Roslyn >= 2 codegen for `var v = default;`
                // + a use of &v): rewrite `stloc s(ldloca v); stobj(ldloc s, default T)`
                // to `stloc v(default T); stloc s(ldloca v)` so `s` can be inlined into
                // its subsequent uses. Runs after DetectCatchWhenConditionBlocks (the
                // deferred DetectExitPoints would sit here in the C# order) and before
                // the second CFS, per GetILTransforms().
                ILSpy::Decompiler::IL::LdLocaDupInitObjTransform().Run(*fn, transformContext);
                // Early expression-level rewrites the rest of the pipeline
                // depends on: stobj(ldloca V, ..) -> stloc V, .., ldobj(ldloca V)
                // -> ldloc V (so ILInlining can fold them), and comparison-kind
                // normalization against ldnull (gt/le -> ne/eq, lt/ge on the left;
                // box T(arg) ==/!= ldnull -> arg ==/!= ldnull for a type parameter T).
                // Runs after LdLocaDupInitObjTransform, before the second CFS (per
                // GetILTransforms()).
                ILSpy::Decompiler::IL::EarlyExpressionTransforms().Run(*fn, transformContext);
                // Remove dead stores to never-read variables: a variable flagged
                // RemoveIfRedundant (by RemoveInfeasiblePath) or under the
                // RemoveDeadStores setting, with no loads or addresses, has its
                // stores dropped. Runs after EarlyExpressionTransforms (so
                // stobj(ldloca V, ..) has collapsed to stloc V, ..) and before
                // the second CFS, per GetILTransforms().
                ILSpy::Decompiler::IL::RemoveDeadVariableInit().Run(*fn, transformContext);
                // Re-run CFS so the duplicated 1-pred return blocks merge and
                // the single-definition variable inlines to `leave (expr)`.
                ILSpy::Decompiler::IL::ControlFlowSimplification().Run(*fn, transformContext);
                // SwitchDetection: reconstruct a C# switch compiled to a sequence
                // of if-statements (non-contiguous case labels) as a single
                // SwitchInstruction, and run SimplifySwitchInstruction as the 2nd
                // pass on the SwitchInstructions the reader emits. Runs after the
                // second CFS and before LoopDetection (per GetILTransforms()), so
                // loops are still flat back-edges the continue/break analysis walks.
                ILSpy::Decompiler::IL::SwitchDetection().Run(*fn, transformContext);
                // SwitchOnNullable: fold the C# compiler's switch-on-
                // Nullable<T> shapes (legacy csc and Roslyn) into a lifted
                // SwitchInstruction with an explicit `case null:` arm. Runs
                // after SwitchDetection and before LoopDetection (per
                // GetILTransforms()), so ifs are still block finals with
                // positional fall-through. Gated on LiftNullables (default true).
                ILSpy::Decompiler::IL::SwitchOnNullableTransform().Run(*fn, transformContext);
                ILSpy::Decompiler::IL::LoopDetection().Run(*fn, transformContext);
                // PatternMatching: detect the C# 7.0 `is` patterns Roslyn emits
                // (a type test plus a variable capture) and rewrite the isinst +
                // null-test block tail into a single MatchInstruction condition
                // (`if (expr is T x) ...`). Runs after LoopDetection and before
                // ConditionDetection (per GetILTransforms()), so ifs are still
                // block finals with positional fall-through. Gated on the
                // PatternMatching setting (default true). The recursive property
                // sub-patterns (`expr is C { P: var x }`) are deferred.
                ILSpy::Decompiler::IL::PatternMatchingTransform().Run(*fn, transformContext);
                // DetectExitPoints: replace inner Branch-to-loop-exit with
                // Leave(loop) so the following ConditionDetection can restructure
                // `if (cond) leave` patterns (invert to `if (!cond) { body }`).
                // Mirrors the C# DetectExitPoints (runs before ConditionDetection).
                ILSpy::Decompiler::IL::DetectExitPoints().Run(*fn, transformContext);
                ILSpy::Decompiler::IL::ConditionDetection().Run(*fn, transformContext);
                // LockTransform: detect the Monitor.Enter/Exit try/finally pattern
                // and fold it into a `lock (expr) { body }`. Runs after
                // ConditionDetection in the BlockILTransform post-order set (per
                // GetILTransforms()), by which point CFS has merged the EH wrapper
                // block (TryFinally alone) with the preceding block (stloc + call
                // Enter), so the stloc/call/TryFinally sit consecutively in one
                // block. Gated on the LockStatement setting (default true). This
                // iteration ports the no-flag MCS/V2 shapes; the flag-based V4 /
                // Roslyn shapes are deferred.
                ILSpy::Decompiler::IL::LockTransform().Run(*fn, transformContext);
                // UsingTransform: detect the IDisposable try/finally pattern and
                // fold it into a `using (resource) { body }`. Runs after
                // ConditionDetection and LockTransform in the BlockILTransform
                // post-order set (per GetILTransforms()). Gated on the
                // UsingStatement setting (default true). This iteration ports the
                // reference-type two-block null-check shape (dominant), the
                // struct one-block ldloca shape, and the isinst-temp two-block
                // shape, all in the preceding-block stloc placement (the dominant
                // mscorlib case); the VB / async / NullableOfT shapes are deferred.
                ILSpy::Decompiler::IL::UsingTransform().Run(*fn, transformContext);
                // CachedDelegateInitialization: collapse the lazy delegate
                // cache (`if (v == null) v = new Delegate(...)`) into the
                // unconditional init. Runs after ConditionDetection /
                // LockTransform / UsingTransform in the BlockILTransform
                // post-order set (per GetILTransforms()), before the
                // StatementTransform. This iteration ports the local-cache
                // (WithLocal) shape; the field-cached / Roslyn / VB shapes
                // (needing IField metadata) are deferred. Gated on the
                // AnonymousMethods setting (default true).
                ILSpy::Decompiler::IL::CachedDelegateInitialization().Run(*fn, transformContext);
                // CachedReadOnlySpanInitialization: collapse the lazy
                // ReadOnlySpan<T>-from-array-literal cache Roslyn emits on
                // frameworks without RuntimeHelpers.CreateSpan (`stloc
                // V(ldobj ldsflda cache); if (V == null) { stloc V(init);
                // stobj(ldsflda cache, ldloc V) }`) into the unconditional init,
                // so a later array-initializer transform recovers the literal
                // and the escaped <PrivateImplementationDetails> cache field
                // disappears. Runs right after CachedDelegateInitialization in
                // the BlockILTransform post-order set (per GetILTransforms()).
                // Gated on the ArrayInitializers setting (default true). ReadOnlySpan
                // is absent from the .NET Framework 4 mscorlib corpus, so this
                // fires 0 times on it (it fires on Roslyn-compiled / modern .NET
                // with System.Memory); ported for faithfulness.
                ILSpy::Decompiler::IL::CachedReadOnlySpanInitialization().Run(*fn, transformContext);
                // StatementTransform: the BlockILTransform post-order set's final
                // member, a per-statement driver that runs the interleaved
                // statement transforms (ILInlining, ExpressionTransforms,
                // TransformAssignment, ...) statement-by-statement with rerun
                // mechanics (per GetILTransforms()). This iteration ports the
                // orchestration and wires the first child, ILInlining (the C#
                // pipeline's second inlining pass); the remaining per-statement
                // transforms (ExpressionTransforms, TransformAssignment,
                // NullCoalescingTransform, ...) and the ILInlining
                // AllowInliningOfLdloca option (the ldloca-into-addressof path)
                // are deferred. Running ILInlining again here folds the
                // single-use variables the intervening transforms
                // (ConditionDetection / Lock / Using / CachedDelegate /
                // CachedReadOnlySpan) created, the point of the C# second pass.
                {
                    ILSpy::Decompiler::IL::StatementTransform statementTransform;
                    statementTransform.AddChild(
                        std::make_unique<ILSpy::Decompiler::IL::ILInlining>());
                    // ExpressionTransforms: the second per-statement child (the
                    // C# GetILTransforms() order), a recursive visitor that folds
                    // simple expression patterns -- `logic.not(comp op)` ->
                    // `comp(op.Negate)` (push negation into the comparison),
                    // `comp(x != 0)` -> `x` (drop the redundant comparison
                    // against 0), and `comp.unsigned(left > 0)` / `<= 0` ->
                    // `comp(left != 0)` / `== 0`. This subset of the C#
                    // ExpressionTransforms is the self-contained VisitComp piece;
                    // the rest (Conv/Box/Call/NewObj/IfInstruction/SwitchExpression/
                    // ...) is deferred. Like ILInlining it does not request
                    // reruns, so it runs after ILInlining without triggering a
                    // re-run of it.
                    statementTransform.AddChild(
                        std::make_unique<ILSpy::Decompiler::IL::ExpressionTransforms>());
                    // TransformAssignment: the inline- and compound-assignment
                    // folds (the next per-statement child in the C# GetILTransforms()
                    // order, after the deferred DynamicIsEventAssignmentTransform).
                    // This iteration ports the self-contained
                    // TransformPostIncDecOperatorWithInlineStore binary case
                    // (the local/StLoc post-increment/decrement fold
                    //   stloc target(binary.add(stloc tmp(ldloc target), ldc.i4 1))
                    //   -> stloc tmp(compound.assign.add.old(ldloca target, ldc.i4 1))
                    //   = `tmp = target++`), the simplest wired fold the D131
                    // IsCompoundStore / IsMatchingCompoundLoad / ValidateCompound-
                    // Assign helpers unblock. It operates on a single non-terminal
                    // block.Instructions[pos] (no if-as-final block-model
                    // adaptation); the operator-call (op_Increment/op_Decrement)
                    // case (needs UserDefinedCompoundAssign + Call.IsLifted) and
                    // the TransformInlineAssignment* / TransformPostIncDecOperator
                    // / TransformPreIncDecOperatorWithInlineStore StObj/Call cases
                    // (need InferType / IsSameMember / IMethod) are deferred. Gated
                    // on IntroduceIncrementAndDecrement (default true). Fires on
                    // the .NET Framework 4 legacy-csc corpus for local post-increment
                    // expression uses (a `V++` whose old value is captured into a
                    // temp).
                    statementTransform.AddChild(
                        std::make_unique<ILSpy::Decompiler::IL::TransformAssignment>());
                    // NullCoalescingTransform: the reference-type `??` fold
                    // (the next per-statement child in the C# GetILTransforms()
                    // order, after the deferred DynamicIsEventAssignmentTransform /
                    // TransformAssignment). Constructs a NullCoalescingInstruction
                    // (`if.notnull(value, fallback)`, the C# `??`) from the
                    //   stloc s(value); if (comp(ldloc s == ldnull)) { stloc s(fallback) }
                    // block tail, then ILInlining folds the single-use `s` into
                    // its load. Adapted to the if-as-final block model (the if is
                    // the block's FinalInstruction, not Instructions[pos+1]); the
                    // if-final is replaced with a Branch to the next block (the
                    // fall-through the if's null FalseInst represented). The
                    // throw-expression cases are deferred (need the ThrowExpressions
                    // setting + a mutable Throw ResultType). Fires 0 times on the
                    // .NET Framework 4 legacy-csc corpus (the `??` reference-type
                    // lowering is a Roslyn-era codegen pattern); ported for
                    // faithfulness.
                    statementTransform.AddChild(
                        std::make_unique<ILSpy::Decompiler::IL::NullCoalescingTransform>());
                    // NullableLiftingStatementTransform: the block-tail nullable
                    // expression lift (the next per-statement child in the C#
                    // GetILTransforms() order, after NullCoalescingTransform and
                    // before NullPropagationStatementTransform). Lifts
                    //   if (!condition) { leave(default(Nullable<T>)) }; leave(newobj Nullable<T>(expr))
                    // into a single leave carrying the lifted value, by calling the
                    // shared Lift (ExpressionTransforms::LiftNullableCore) on the two
                    // leaves' values. Adapted to this port's post-ConditionDetection
                    // shape (ConditionDetection inverts the early-exit pattern: the
                    // if is the block's FinalInstruction, the newobj-leave is in the
                    // if's TrueInst Block, the default-leave is the next block's
                    // FinalInstruction). The LiftNullables / NullPropagation gates are
                    // checked inside LiftNullableCore. Fires 0 times on the .NET
                    // Framework 4 legacy-csc mscorlib corpus (a Roslyn-era Nullable<T>
                    // expression-lift codegen pattern); ported for faithfulness.
                    statementTransform.AddChild(
                        std::make_unique<ILSpy::Decompiler::IL::NullableLiftingStatementTransform>());
                    // NullPropagationStatementTransform: the void-call `?.`
                    // statement form (the next per-statement child in the C#
                    // GetILTransforms() order, after NullCoalescingTransform /
                    // NullableLiftingStatementTransform). Folds
                    //   if (testedVar != null) { testedVar.AccessChain(); }
                    // into `testedVar?.AccessChain();` (a void NullableRewrap,
                    // the `?.` statement whose value is discarded). The if is the
                    // block's FinalInstruction (this port's if-as-final model);
                    // the TrueInst is a Block with one instruction, the FalseInst
                    // is null (no else). The if-final is replaced with the void
                    // NullableRewrap as a non-terminal + a Branch to the next
                    // block (the fall-through the if's null FalseInst represented).
                    // The UnconstrainedType mode and the
                    // TransformNullPropagationOnUnconstrainedGenericExpression
                    // pattern (a 5-instruction block sequence) are deferred. Gated
                    // on the NullPropagation setting (default true). Fires 0 times
                    // on the .NET Framework 4 legacy-csc mscorlib corpus (the `?.`
                    // operator is C# 6.0 / Roslyn-era); ported for faithfulness.
                    statementTransform.AddChild(
                        std::make_unique<ILSpy::Decompiler::IL::NullPropagationStatementTransform>());
                    // UserDefinedLogicTransform: the user-defined short-circuiting
                    // `&&` / `||` operator fold (the next per-statement child in
                    // the C# GetILTransforms() order, after the deferred
                    // TransformArrayInitializers / TransformCollectionAndObject-
                    // Initializers / TransformExpressionTrees / IndexRangeTransform /
                    // DeconstructionTransform / NamedArgumentTransform /
                    // RemoveUnconstrainedGenericReferenceTypeCheck and before
                    // InterpolatedStringTransform). This iteration ports the
                    // LegacyPattern (the legacy-csc shape) and the shared
                    // MatchCondition / MatchBitwiseCall helpers, adapted to the
                    // if-as-final block model. The RoslynOptimized pattern (the
                    // "in combination with return statement" shape) is deferred.
                    // The .NET Framework 4 legacy-csc mscorlib corpus carries no
                    // op_True / op_False operator definitions, so the fold fires 0
                    // times on it (faithfulness-only, matching the DetectCatchWhen-
                    // ConditionBlocks / LdLocaDupInitObj / SwitchOnNullable precedent);
                    // ported for the future real back end and Roslyn-compiled corpora.
                    statementTransform.AddChild(
                        std::make_unique<ILSpy::Decompiler::IL::UserDefinedLogicTransform>());
                    // InterpolatedStringTransform: the C# 10/.NET 6 `$"..."`
                    // via DefaultInterpolatedStringHandler fold (the last
                    // per-statement child in the C# GetILTransforms() order,
                    // after UserDefinedLogicTransform). Folds the
                    //   stloc v(newobj DefaultInterpolatedStringHandler(..))
                    //   call AppendLiteral/AppendFormatted(ldloca v, ...)
                    //   ...
                    //   call ToStringAndClear(ldloca v)
                    // sequence into a single Block(InterpolatedString) whose
                    // FinalInstruction is the ToStringAndClear call, so the
                    // back end can render it as `$"literal{expr}..."`. Gated on
                    // the StringInterpolation setting (default true).
                    // DefaultInterpolatedStringHandler is a .NET 6+ type, so the
                    // fold fires 0 times on the .NET Framework 4 legacy-csc
                    // mscorlib corpus (the handler-construction codegen is
                    // absent from it); it fires on Roslyn-compiled / modern .NET.
                    // Ported for faithfulness (matching the
                    // DetectCatchWhenConditionBlocks / LdLocaDupInitObj /
                    // SwitchOnNullable / NullCoalescingTransform precedent).
                    statementTransform.AddChild(
                        std::make_unique<ILSpy::Decompiler::IL::InterpolatedStringTransform>());
                    statementTransform.Run(*fn, transformContext);
                }
                // HighLevelLoopTransform: turn the `while (true)` + break
                // structure LoopDetection+ConditionDetection produced into a
                // `while (cond)` container. MatchWhileLoop subset (MatchForLoop
                // and MatchDoWhileLoop are deferred). Runs after the
                // StatementTransform and before AssignVariableNames (per
                // GetILTransforms()).
                ILSpy::Decompiler::IL::HighLevelLoopTransform::Run(*fn, transformContext);
                // FixRemainingIncrements: handles the user-defined
                // `op_Increment`/`op_Decrement` calls that TransformAssignment's
                // inc/dec folds did NOT fold -- the cases where the variable-
                // being-incremented was optimized out by Roslyn
                //   stloc V(call op_Increment(expr))  ->  stloc V(expr); ++V
                // (a UserDefinedCompoundAssign EvaluatesToNewValue, Address
                // target, inserted after the store). Runs after the
                // StatementTransform + HighLevelLoopTransform and before
                // CopyPropagation (per GetILTransforms: ProxyCallReplacer,
                // FixRemainingIncrements, CopyPropagation; ProxyCallReplacer is
                // deferred -- needs the full IMethod/type-system/IL-reader
                // context). This iteration ports the primary branch (the call
                // is a StLoc's Value and the StLoc is a non-terminal in a Block);
                // the else branch (needs ILInstruction.Extract) is deferred.
                // Decimal is skipped (handled in the C# ReplaceMethodCallsWith-
                // Operators, a resolver path this port does not model). Fires 0
                // times on the .NET Framework 4 legacy-csc mscorlib corpus
                // (no non-Decimal op_Increment/op_Decrement + the Roslyn
                // optimized-out-variable codegen is absent); ported for
                // faithfulness.
                ILSpy::Decompiler::IL::FixRemainingIncrements().Run(*fn, transformContext);
                // CopyPropagation: drop dead stores to stack slots and propagate
                // single-def stack slots assigned from never-assigned parameters
                // (the argument-to-local copy csc emits). Runs late, after the
                // StatementTransform + HighLevelLoopTransform (per GetILTransforms:
                // ProxyCallReplacer, FixRemainingIncrements, CopyPropagation).
                ILSpy::Decompiler::IL::CopyPropagation().Run(*fn, transformContext);
                ILSpy::Decompiler::IL::AssignVariableNames().Run(*fn, transformContext);
                // ReduceNestingTransform: EliminateRedundantTryFinally (the
                // redundant try-finally the C# compiler wraps a `fixed` block
                // in, once DetectPinnedRegions has formed the PinnedRegion) +
                // ImproveILOrdering (the IL-order-gated InvertIf that re-inverts
                // ConditionDetection's inversion when the IL order is wrong).
                // Runs after HighLevelLoopTransform (per GetILTransforms: the C#
                // order is HighLevelLoopTransform, ReduceNestingTransform,
                // RemoveRedundantReturn). The ReduceNesting /
                // ReduceSwitchNesting / ExtractElseBlock folds (the rest of the
                // C# `Visit`) are deferred (need the full CanDuplicateExit /
                // EnsureEndPointUnreachable / ExtractElseBlock helpers + the
                // dominator analysis); the trailing-leave handling
                // (CanDuplicateExit) is deferred.
                ILSpy::Decompiler::IL::ReduceNestingTransform().Run(*fn, transformContext);
                // RemoveUnreachableBlocks: drop blocks with no reachable path
                // from the container entry. The structure-changing transforms
                // above (LoopDetection, ConditionDetection,
                // HighLevelLoopTransform, ReduceNesting) can leave dead blocks
                // behind -- a loop body that branches back to the header leaves
                // its fall-through successor unreachable; an inlined
                // fall-through leaves the original next block unreachable; a
                // `try/finally` that always returns leaves an empty trailing
                // dead block after the return. The seed would otherwise render
                // these as dead code after a `return;`/`continue;`/`throw`.
                // Mirrors the C# BlockContainer.SortBlocks(deleteUnreachableBlocks:
                // true) subset. Runs BEFORE RemoveRedundantReturn so a trailing
                // empty dead block does not shadow the real last reachable
                // block's `return;` (RemoveRedundantReturn reads the literal last
                // block; a trailing dead block would make it bail).
                ILSpy::Decompiler::IL::RemoveUnreachableBlocks().Run(*fn, transformContext);
                ILSpy::Decompiler::IL::RemoveRedundantReturn().Run(*fn, transformContext);
                fn->CheckInvariant(ILSpy::Decompiler::IL::ILPhase::Normal);
                std::string returnType = "void";
                std::string paramDecl;
                if (auto sig = file.GetMethodSignature(m.Token)) {
                    if (sig->ReturnType && sig->ReturnType->ReflectionName() != "System.Void")
                        returnType = ILSpy::Decompiler::IL::CSharpTypeName(sig->ReturnType);
                    auto paramNames = file.GetParameterNames(m.Token);
                    int base_ = sig->IsInstance ? 1 : 0;
                    for (std::size_t i = 0; i < sig->ParameterTypes.size(); ++i) {
                        if (i) paramDecl += ", ";
                        paramDecl += ILSpy::Decompiler::IL::CSharpTypeName(sig->ParameterTypes[i]);
                        paramDecl += ' ';
                        if (i < paramNames.size() && !paramNames[i].empty())
                            paramDecl += paramNames[i];
                        else
                            paramDecl += "arg_" + std::to_string(base_ + static_cast<int>(i));
                    }
                }
                std::cout << "// " << t.Namespace << "." << t.Name << "\n"
                          << ILSpy::Decompiler::IL::ILAstToCSharp(*fn, returnType, m.Name, paramDecl)
                          << '\n';
                ++methodsPrinted;
                continue;
            }
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
#endif
