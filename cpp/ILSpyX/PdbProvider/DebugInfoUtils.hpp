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

// Port of ICSharpCode.ILSpyX/PdbProvider/DebugInfoUtils.cs: the PDB
// discovery the CLI's -usepdb/--il-sequence-points wiring routes through
// (the IlspyCmdProgram.TryLoadPDB caller) -- LoadSymbols walks the PE
// debug directory for a portable-CodeView entry (the associated/embedded
// discovery), falls back to the adjacent <module>.pdb file for a legacy
// CodeView entry, and FromFile opens an explicitly named PDB.
//
// C#-to-C++ porting decisions:
//  * The C# `public static class DebugInfoUtils` ports as free functions
//    (the SRMExtensions free-function convention for static C# classes);
//    the private TryOpenPortablePdb/OpenStream members stay file-local in
//    the .cpp.
//  * The C# `PEFile module` ports as the MetadataFile (the merged
//    PEFile/MetadataReader stand-in); the module's own path comes from the
//    new FileName() accessor (the C# PEFile.FileName property).
//  * The C# `MetadataReaderProvider?` out parameter of TryOpenPortablePdb
//    ports as the PortablePdb value (the port's eager-parse reader stand-in
//    -- an invalid value is the C# null provider whose deferred parse
//    failed, which the PortableDebugInfoProvider reports through its error
//    state).
//  * The C# `string? pdbFileName` (null for an embedded PDB) ports as the
//    empty string in the out parameter, mapped to a disengaged optional at
//    the PortableDebugInfoProvider construction (the port's null analogue
//    for that parameter, the established convention).
//  * The legacy-PDB prefix check ("Microsoft C/C++ MSF 7.00" -- the native
//    Windows PDB header) ports as a size-and-memcmp over the first 24
//    bytes: the C# Stream.Read must return the full prefix length, so a
//    file shorter than the prefix never matches (it falls through to the
//    portable parse, whose deferred failure surfaces through the provider
//    error state).
//  * The C# catch (BadImageFormatException) of LoadSymbols -- which
//    discards the PDB load error and yields null -- ports as a catch of
//    std::out_of_range (the port's BadImageFormatException analogue; the
//    associated-PDB parse failures throw it). The C# COMException arm of
//    the catch has no port analogue: the port's PE reads are plain byte
//    accesses with no COM surface, and every malformed-image path of this
//    chain throws the out_of_range arms.
//  * DEFERRED: the MonoCecilDebugInfoProvider (the Mono.Cecil bridge both
//    the LoadSymbols fallback and the FromFile legacy-PDB arm construct).
//    PORT_PLAN.md section 5.7 scopes the Cecil bridge and the legacy
//    Windows-PDB reader out of the initial port: the port supports
//    Portable PDB only. Where the C# returns a MonoCecil provider (an
//    adjacent <module>.pdb whose bytes carry the native MSF prefix, an
//    adjacent file with no CodeView entry at all, or a FromFile path with
//    the native prefix), the port returns null -- no debug info, the
//    documented Windows-PDB gap. (The real C# over a garbage MSF file
//    throws a PdbException out of LoadSymbols instead, the eager Cecil
//    read the port does not have.)

#pragma once

#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <memory>
#include <string>

namespace ILSpy::ILSpyX::PdbProvider {

// The C# `public static IDebugInfoProvider? LoadSymbols(PEFile module)`:
// the module's PDB discovered through its PE debug directory -- the
// portable-CodeView entry's associated or embedded portable PDB, or the
// adjacent <module>.pdb file a legacy CodeView entry names. Null when no
// PDB was found (or the found one failed to parse -- the swallowed
// BadImageFormatException arm) and for the deferred MonoCecil arms above.
std::unique_ptr<Decompiler::DebugInfo::IDebugInfoProvider> LoadSymbols(
    const Decompiler::Metadata::MetadataFile& module);

// The C# `public static IDebugInfoProvider? FromFile(PEFile module, string
// pdbFileName)`: the explicitly named PDB (the -usepdb <file> path). A
// portable PDB yields the PortableDebugInfoProvider over it (a file that
// does not parse still yields the provider, whose error state the
// Description reports -- the C# deferred parse); the native-MSF-prefixed
// file the deferred MonoCecil arm; null for an empty name, a file that
// does not exist, and the deferred arm.
std::unique_ptr<Decompiler::DebugInfo::IDebugInfoProvider> FromFile(
    const Decompiler::Metadata::MetadataFile& module,
    const std::string& pdbFileName);

}  // namespace ILSpy::ILSpyX::PdbProvider
