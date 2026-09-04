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

// IlspyCmdProgram.cpp -- see IlspyCmdProgram.hpp for the port contract.

#include "ILSpyCmd/IlspyCmdProgram.hpp"

#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "ILSpyX/PdbProvider/DebugInfoUtils.hpp"
#include "ILSpyCmd/ResourceExtensions.hpp"

namespace ILSpy::ILSpyCmd {

// The C# `IDebugInfoProvider TryLoadPDB(PEFile module)` (IlspyCmdProgram.cs):
// the InputPDBFile dispatch -- the bare form's PDB discovery, the valued
// form's explicit PDB, no flag no debug info.
std::unique_ptr<Decompiler::DebugInfo::IDebugInfoProvider> TryLoadPDB(
    const Decompiler::Metadata::MetadataFile& module,
    const InputPDBFile& pdbFile)
{
    if (pdbFile.IsSet) {
        if (!pdbFile.Value)
            return ILSpyX::PdbProvider::LoadSymbols(module);
        return ILSpyX::PdbProvider::FromFile(module, *pdbFile.Value);
    }
    return nullptr;
}

// The C# `int ShowIL(string assemblyFileName, TextWriter output)`
// (IlspyCmdProgram.cs): the header line straight to the writer, then the
// ReflectionDisassembler over a PlainTextOutput wrapping it, DebugInfo from
// TryLoadPDB and ShowSequencePoints from the --il-sequence-points flag,
// rendering WriteModuleContents. Returns 0 (the C# return value).
int ShowIL(const std::string& assemblyFileName, std::ostringstream& output,
    bool showILSequencePoints, const InputPDBFile& pdbFile)
{
    // The C# `var module = new PEFile(assemblyFileName)` -- the port's
    // never-throwing MetadataFile (the unparseable-file divergence the
    // header documents: the bare header line, no contents, rc 0).
    Decompiler::Metadata::MetadataFile module(assemblyFileName);
    // The C# `output.WriteLine($"// IL code: {module.Name}")` -- written
    // before the PlainTextOutput wraps the writer (Environment.NewLine --
    // the port's kNewLine "\r\n" convention).
    output << "// IL code: " << module.Name() << "\r\n";
    // The C# object initializer's property sets: DebugInfo then
    // ShowSequencePoints. The provider must outlive the disassembler's
    // WriteModuleContents call (the caller-owned raw pointer).
    std::unique_ptr<Decompiler::DebugInfo::IDebugInfoProvider> debugInfo =
        TryLoadPDB(module, pdbFile);
    Decompiler::Output::PlainTextOutput textOutput(output);
    Decompiler::Disassembler::ReflectionDisassembler disassembler(textOutput);
    disassembler.DebugInfo(debugInfo.get());
    disassembler.ShowSequencePoints(showILSequencePoints);
    disassembler.WriteModuleContents(module);
    return 0;
}

namespace {

using ILSpy::Decompiler::TypeSystem::TypeKind;

// The C# `$"{type.Kind} {type.FullTypeName.ReflectionName}"` interpolation:
// the TypeKind enum member name (Enum.ToString). Every ported kind is a
// named member, so the .NET decimal fallback for an unnamed value is
// unreachable.
const char* TypeKindName(TypeKind kind) {
    switch (kind) {
        case TypeKind::Other: return "Other";
        case TypeKind::Class: return "Class";
        case TypeKind::Interface: return "Interface";
        case TypeKind::Struct: return "Struct";
        case TypeKind::Delegate: return "Delegate";
        case TypeKind::Enum: return "Enum";
        case TypeKind::Void: return "Void";
        case TypeKind::Unknown: return "Unknown";
        case TypeKind::Null: return "Null";
        case TypeKind::None: return "None";
        case TypeKind::Dynamic: return "Dynamic";
        case TypeKind::UnboundTypeArgument: return "UnboundTypeArgument";
        case TypeKind::TypeParameter: return "TypeParameter";
        case TypeKind::Array: return "Array";
        case TypeKind::Pointer: return "Pointer";
        case TypeKind::ByReference: return "ByReference";
        case TypeKind::Intersection: return "Intersection";
        case TypeKind::ArgList: return "ArgList";
        case TypeKind::Tuple: return "Tuple";
        case TypeKind::ModOpt: return "ModOpt";
        case TypeKind::ModReq: return "ModReq";
        case TypeKind::NInt: return "NInt";
        case TypeKind::NUInt: return "NUInt";
        case TypeKind::FunctionPointer: return "FunctionPointer";
    }
    return "";  // unreachable: every enum member is named above
}

}  // namespace

// The C# `int ListContent(string assemblyFileName, TextWriter output,
// ISet<TypeKind> kinds)` (IlspyCmdProgram.cs): the -l/--list render --
// every type definition in the TypeDef table's row order whose kind is
// selected, as `{Kind} {ReflectionName}` lines.
int ListContent(const std::string& assemblyFileName, std::ostringstream& output,
    const std::set<ILSpy::Decompiler::TypeSystem::TypeKind>& kinds)
{
    // The C# `var decompiler = GetDecompiler(assemblyFileName)` +
    // `decompiler.TypeSystem.MainModule.TypeDefinitions`: MetadataModule
    // iterates metadata.TypeDefinitions -- the TypeDef table in row order
    // (<Module> included, nested types at their physical rows) -- the port's
    // TypeDefs() walk. The C# type-system wrapper contributes nothing to
    // this render beyond the metadata walk (no resolver is consulted).
    ILSpy::Decompiler::Metadata::MetadataFile module(assemblyFileName);
    for (const auto& t : module.TypeDefs()) {
        if (kinds.find(t.Kind) == kinds.end())
            continue;
        // The C# `output.WriteLine($"{type.Kind} {type.FullTypeName.ReflectionName}")`:
        // the TypeKind enum name, a space, the GetFullTypeName declaring-chain
        // reflection name (the `n arity suffix, the '+' nesting separators).
        // TextWriter.WriteLine uses Environment.NewLine -- the port's "\r\n"
        // convention (PlainTextOutput hardcodes the Windows value).
        output << TypeKindName(t.Kind) << ' '
              << ILSpy::Decompiler::Metadata::GetFullTypeNameFromDefinition(module, t.Token)
                     .ReflectionName()
              << "\r\n";
    }
    return 0;
}

// The C# `int ListResources(string assemblyFileName, TextWriter output)`
// (IlspyCmdProgram.cs): the --list-resources render.
int ListResources(const std::string& assemblyFileName, std::ostringstream& output)
{
    // The C# `var module = new PEFile(assemblyFileName)` then one
    // WriteLine per EnumerateResourcePaths path. The port's MetadataFile
    // never throws -- an unparseable file has no resources and renders
    // nothing (main.cpp gates IsValid before dispatching).
    ILSpy::Decompiler::Metadata::MetadataFile module(assemblyFileName);
    for (const auto& path : EnumerateResourcePaths(module))
        output << path << "\r\n";
    return 0;
}

}  // namespace ILSpy::ILSpyCmd
