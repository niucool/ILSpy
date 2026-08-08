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
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
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
                ILSpy::Decompiler::IL::ILInlining().Run(*fn, transformContext);
                ILSpy::Decompiler::IL::LoopDetection().Run(*fn, transformContext);
                fn->CheckInvariant(ILSpy::Decompiler::IL::ILPhase::Normal);
                std::string returnType = "void";
                std::string paramDecl;
                if (auto sig = file.GetMethodSignature(m.Token)) {
                    if (sig->ReturnType && sig->ReturnType->ReflectionName() != "System.Void")
                        returnType = sig->ReturnType->ReflectionName();
                    int base_ = sig->IsInstance ? 1 : 0;
                    for (std::size_t i = 0; i < sig->ParameterTypes.size(); ++i) {
                        if (i) paramDecl += ", ";
                        paramDecl += sig->ParameterTypes[i]->ReflectionName();
                        paramDecl += " arg_";
                        paramDecl += std::to_string(base_ + static_cast<int>(i));
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
