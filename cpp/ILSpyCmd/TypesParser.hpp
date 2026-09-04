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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of ICSharpCode.ILSpyCmd/TypesParser.cs (the -l/--list option's
// entity-type selection) plus the option-value preprocessing the C#
// performs at the call site (IlspyCmdProgram.OnExecuteAsync's
// `EntityTypes.SelectMany(v => v.Split(',', ';'))` -- main.cpp is not a
// testable library unit, so the split lives here beside its consumer).
//
// The C# ParseSelection contract:
//  * `possibleValues` is a case-insensitive class/struct/interface/enum/
//    delegate dictionary.
//  * A SINGLE value that does not BEGIN (case-insensitively) with one of
//    the dictionary keys is a soup of kind characters, each matched
//    case-SENSITIVELY ('c' class, 'i' interface, 's' struct, 'd' delegate,
//    'e' enum; anything else is ignored): "cis" selects Class+Interface+
//    Struct while "C" selects nothing.
//  * Otherwise (several values, or a single value beginning with a key)
//    every value is trimmed from the END until it is a dictionary key;
//    a value that trims past its start is silently ignored ("classX" and
//    "classes" both select Class, "c" and "i" select nothing -- "c" has no
//    key prefix and trims to the empty string).

#pragma once

#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <set>
#include <string>
#include <vector>

namespace ILSpy::ILSpyCmd {

// The C# `public static HashSet<TypeKind> ParseSelection(string[] values)`
// (TypesParser.cs). The values are the SPLIT option values (the
// SplitEntityTypeValues output), not the raw per-occurrence strings.
std::set<ILSpy::Decompiler::TypeSystem::TypeKind> ParseSelection(
    const std::vector<std::string>& values);

// The C# OnExecuteAsync `EntityTypes.SelectMany(v => v.Split(',', ';'))`:
// every -l/--list occurrence's value split on ',' and ';' at once and
// flattened across occurrences, in order. String.Split(char[]) defaults
// to StringSplitOptions.None, so consecutive, leading and trailing
// delimiters yield EMPTY entries ("c," splits to {"c", ""}) -- and the
// empty entries are load-bearing: `values.Length == 1` is the gate that
// routes a single value to the kind-character path, so "c," (two split
// entries) selects NOTHING while "c" (one entry) selects Class.
std::vector<std::string> SplitEntityTypeValues(const std::vector<std::string>& rawValues);

}  // namespace ILSpy::ILSpyCmd
