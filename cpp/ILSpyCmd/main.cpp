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

// ilspycmd (C++ port) entry point. Phase 10 fills in the full option set; this
// implements the IL-disassembly workflow first (the plan allows Phase 6 to run
// early as a milestone): given an assembly, dump the IL of every method with a
// body, optionally restricted to one type with -t. Token operands are resolved
// to "Namespace.Type::Member" / "Namespace.Type" names.

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
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
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Metadata/ILTextEmitter.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <cxxopts.hpp>

#include <cstdint>
#include <cstdio>
#include <cctype>
#include <iostream>
#include <string>

using namespace ILSpy::Decompiler::Metadata;

int main(int argc, char** argv) {
    cxxopts::Options options("ilspycmd",
        "C++ port of the ILSpy command-line decompiler (core + CLI)");
    options.allow_unrecognised_options();
    options.add_options()
        ("h,help", "Print help")
        ("v,version", "Print version")
        ("assembly", "Assembly file to decompile", cxxopts::value<std::string>())
        ("il,ilcode", "Show IL for the assembly's methods", cxxopts::value<bool>()
            ->default_value("false")->implicit_value("true"))
        ("ilast", "Decode straight-line method bodies into an ILAst tree and dump it",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("ilast-all", "Decode method bodies (branch-aware) into an ILAst tree and dump it",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("csharp", "Translate method bodies to C#-ish text (Phase 5 seed: gotos for control flow, var locals, approximate casts/names -- the real resolver back end lands in Phase 5)",
            cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("l,list", "List types of the given kind(s): c(lass), i(nterface), s(truct), d(elegate), e(num)",
            cxxopts::value<std::string>()->default_value(""))
        ("dump-table", "Dump a metadata table (row count + key fields). Table name: TypeDef, MethodDef, Field, Property, Assembly, AssemblyRef, etc.",
            cxxopts::value<std::string>()->default_value(""))
        ("t,type", "Restrict --il/--ilast to a single type by full name (Namespace.Type)",
            cxxopts::value<std::string>());
    options.parse_positional({ "assembly" });

    auto parsed = options.parse(argc, argv);
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
    bool wantIlAst = parsed.count("ilast") != 0 && parsed["ilast"].as<bool>();
    bool wantIlAstAll = parsed.count("ilast-all") != 0 && parsed["ilast-all"].as<bool>();
    bool wantCSharp = parsed.count("csharp") != 0 && parsed["csharp"].as<bool>();
    std::string listKinds = parsed.count("list") != 0 ? parsed["list"].as<std::string>() : "";
    std::string dumpTable = parsed.count("dump-table") != 0 ? parsed["dump-table"].as<std::string>() : "";
    std::string typeFilter = parsed.count("type") != 0 ? parsed["type"].as<std::string>() : "";

    if (!wantIl && !wantIlAst && !wantIlAstAll && !wantCSharp && listKinds.empty() && dumpTable.empty()) {
        std::cout << "ilspycmd: see --help for available options (--il, --ilast, --ilast-all, --csharp, --list, --dump-table).\n";
        return 0;
    }

    ILSpy::Decompiler::Metadata::MetadataFile file(asmPath);
    if (!file.IsValid()) {
        std::cerr << "ilspycmd: could not open '" << asmPath << "' as a CLI assembly\n";
        return 1;
    }

    if (!listKinds.empty()) {
        // List types of the given kinds. Maps the kind chars to TypeKind values.
        auto kindMatch = [&](ILSpy::Decompiler::TypeSystem::TypeKind k) {
            char c = '\0';
            switch (k) {
                case ILSpy::Decompiler::TypeSystem::TypeKind::Class: c = 'c'; break;
                case ILSpy::Decompiler::TypeSystem::TypeKind::Interface: c = 'i'; break;
                case ILSpy::Decompiler::TypeSystem::TypeKind::Struct: c = 's'; break;
                case ILSpy::Decompiler::TypeSystem::TypeKind::Delegate: c = 'd'; break;
                case ILSpy::Decompiler::TypeSystem::TypeKind::Enum: c = 'e'; break;
                default: return false;
            }
            return listKinds.find(c) != std::string::npos;
        };
        for (const auto& t : file.TypeDefs()) {
            if (t.Name == "<Module>") continue;
            if (!kindMatch(t.Kind)) continue;
            std::string fullName = t.Namespace.empty() ? t.Name : t.Namespace + "." + t.Name;
            if (!typeFilter.empty() && fullName != typeFilter) continue;
            std::cout << fullName << '\n';
        }
        return 0;
    }

    if (!dumpTable.empty()) {
        // Dump a metadata table: row count + key fields for the tables we expose.
        // winmd gives typed table access; we print a useful subset.
        auto toLower = [](std::string s) { for (auto& c : s) c = (char)tolower(c); return s; };
        std::string tn = toLower(dumpTable);
        if (tn == "typedef") {
            auto types = file.TypeDefs();
            std::cout << "TypeDef table: " << types.size() << " rows\n";
            std::cout << "RID  Token    Kind        Namespace.Name\n";
            for (std::size_t i = 0; i < types.size(); ++i) {
                const auto& t = types[i];
                if (i >= 100 && types.size() > 200) { std::cout << "... (" << (types.size() - 100) << " more)\n"; break; }
                const char* kindStr = "?";
                switch (t.Kind) {
                    case ILSpy::Decompiler::TypeSystem::TypeKind::Class: kindStr = "Class"; break;
                    case ILSpy::Decompiler::TypeSystem::TypeKind::Interface: kindStr = "Interface"; break;
                    case ILSpy::Decompiler::TypeSystem::TypeKind::Struct: kindStr = "Struct"; break;
                    case ILSpy::Decompiler::TypeSystem::TypeKind::Enum: kindStr = "Enum"; break;
                    case ILSpy::Decompiler::TypeSystem::TypeKind::Delegate: kindStr = "Delegate"; break;
                    case ILSpy::Decompiler::TypeSystem::TypeKind::Void: kindStr = "Void"; break;
                    default: break;
                }
                char tok[16]; std::snprintf(tok, sizeof(tok), "0x%08X", t.Token);
                std::cout << (i + 1) << "  " << tok << "  " << kindStr << "        "
                          << (t.Namespace.empty() ? "" : (t.Namespace + ".")) << t.Name << "\n";
            }
        } else if (tn == "methoddef") {
            auto methods = file.MethodDefs();
            std::cout << "MethodDef table: " << methods.size() << " rows\n";
            for (std::size_t i = 0; i < methods.size() && i < 100; ++i) {
                char tok[16]; std::snprintf(tok, sizeof(tok), "0x%08X", methods[i].Token);
                std::cout << (i + 1) << "  " << tok << "  RVA=0x" << std::hex << methods[i].RVA << std::dec
                          << "  " << methods[i].Name << "\n";
            }
        } else {
            std::cout << "dump-table: table '" << dumpTable << "' not supported. Supported: TypeDef, MethodDef.\n";
            return 1;
        }
        return 0;
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
                    statementTransform.Run(*fn, transformContext);
                }
                ILSpy::Decompiler::IL::AssignVariableNames().Run(*fn, transformContext);
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
                          << "  (ILAst, branch-aware)\n" << fn->ToString() << "\n\n";
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
                          << "  (ILAst)\n" << fn->ToString() << "\n\n";
                ++methodsPrinted;
                continue;
            }
            auto body = file.GetMethodBody(m.RVA);
            if (!body.IsValid()) continue;
            std::cout << ".method " << t.Namespace << "." << t.Name << "::" << m.Name
                      << "  (maxstack " << body.MaxStack() << ", code size "
                      << body.CodeSize() << ")\n";
            std::cout << DisassembleILText(body.IL(),
                [&](std::uint32_t tok) { return file.ResolveTokenToString(tok); });
            std::cout << '\n';
            ++methodsPrinted;
        }
    }
    if (methodsPrinted == 0) {
        std::cerr << "ilspycmd: no method bodies found"
                  << (typeFilter.empty() ? "" : " for type '" + typeFilter + "'") << '\n';
        return 1;
    }
    return 0;
}
