// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.ILSpyCmd/MetadataTableDumper.cs -- the --dump-table
// mode: every row of a metadata table (RID, token, resolved names, heap
// offsets and coded indexes) as an aligned console table or as JSON, over
// the same 39 Cor tables the GUI's metadata view supports (EnC and
// Portable-PDB tables are out of scope).
//
// The C# reads the raw rows through the .NET 10 System.Reflection.Metadata
// public table surface (GetTableRowCount/GetTableMetadataOffset/
// GetTableRowSize) plus the ICSharpCode.Decompiler MetadataExtensions row
// walkers (GetTypeDefListColumns, GetPtrRows, GetClassLayouts, ...) that
// hand-decode the raw column bytes. The port reaches the same raw values
// through MetadataFile's Cor-table row surface (the winmd database's
// open-time column layout implements the same II.24.2.6 width rules), so
// the width derivations the C# extensions spell out are not re-implemented
// -- the loaders read each column by its ECMA-335 declaration-order index.
//
// C#-to-C++ porting decisions:
//  * The enum columns the C# boxes into FormatValue's `case Enum e: return
//    e.ToString()` render through the .NET 10 Enum.ToString semantics,
//    decompiled from System.Private.CoreLib 10.0.8 System.Enum
//    (GetSingleFlagsEnumNameForValue / TryFindFlagsNames /
//    WriteMultipleFoundFlagsNames): a [Flags] enum renders the exact member
//    name when one equals the value (the last of equal-valued members),
//    otherwise the descending-value union of members whose bits are all
//    present (each consumed member's bits are cleared; the names join in
//    ascending value order), with a leftover bit discarding the names and
//    rendering the full value in decimal. A plain enum renders the exact
//    member name or the decimal. The member tables below carry the
//    Enum.GetValues order -- ascending by value, ties in the order the
//    runtime's Array.Sort left them (probed against the real .NET 10).
//  * The columns of every table are spelled out explicitly in ECMA-335
//    declaration order (the C# comment: reflecting over the SRM row structs
//    would tie the output to runtime internals).
//  * The *Ptr indirection tables carry no winmd model (a file with rows
//    there fails to open), so they dump as "0 rows" -- the only value any
//    port-openable file can carry, matching the C# for every such file.
//  * A row cell holds either the numeric RID (the JSON writer's number arm)
//    or the already-formatted string (FormatValue runs at row-build time,
//    the C# BuildRow shape); the console writer renders both through their
//    string form.
//  * DumpTable writes the final CRLF text (the C# TextWriter.WriteLine
//    convention the CLI's Console.Out carries); the CLI writes the buffer
//    in binary mode (the iteration-19 stdout contract).

#pragma once

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace ILSpy::ILSpyCmd {

// The C# MetadataTableDumper consumes System.Reflection.Metadata types; the
// port's stand-ins live in ILSpy::Decompiler::Metadata.
namespace Metadata = ::ILSpy::Decompiler::Metadata;

// The supported-table ids in the C# declaration order (the
// SupportedTableNames join order).
const std::vector<Metadata::CorTableIndex>& SupportedTables();

// The C# `SupportedTableNames`: "Module (0x00), TypeRef (0x01), ...".
std::string SupportedTableNames();

// The C# `TryParseTableName(string, out TableIndex)`: the ECMA-335 table
// name (case-insensitive) or the table number (decimal, or 0x-prefixed hex)
// over the supported set. False for anything else.
bool TryParseTableName(const std::string& name, Metadata::CorTableIndex& table);

// The table's enum name ("Module", "TypeDef", ...) -- the C#
// `table.ToString()` (the supported ids all carry named members). The CLI's
// -o branch composes the output file name from it: `<name>.<table>.txt` /
// `.json` (the C# `$".{table}.{...}"` interpolation).
const char* TableName(Metadata::CorTableIndex table);

// The C# `DumpTable(string assemblyFileName, TextWriter output, TableIndex
// table, bool asJson)`: the row loads plus the aligned console table
// (asJson false) or the JSON document (asJson true), returning 0. An invalid
// file dumps its (empty) tables without a throw -- the CLI validates the
// file before dispatching, and the C# PEFile constructor throw is the
// caller's concern.
int DumpTable(const std::string& assemblyFileName, std::ostream& output,
    Metadata::CorTableIndex table, bool asJson);

} // namespace ILSpy::ILSpyCmd
