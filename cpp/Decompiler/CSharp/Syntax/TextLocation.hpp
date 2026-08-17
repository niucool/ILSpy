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

// TextLocation: a line/column position in a source file, faithful to the
// `TextLocation` struct in ICSharpCode.Decompiler/CSharp/Syntax/TextLocation.cs.
// Text editor lines and columns are counted from one (MinLine == MinColumn == 1);
// `Empty` is the (0, 0) sentinel for "no location". The struct is the foundation
// for the C# AST's source-location tracking (every AstNode carries a
// StartLocation/EndLocation pair) and the output visitor's span recording, so it
// is the first piece of Phase 5 (the C# AST + resolver + back end) to land.
//
// The C# `TextLocationConverter` (a `System.ComponentModel.TypeConverter` for
// design-time / property-grid string<->TextLocation conversion) is NOT ported:
// it is a GUI / design-surface concern and the CLI scope excludes the GUI
// (PORT_PLAN.md section 1). The C# AST and output stages use the value type only.
//
// This is a header-only value type: a trivially-copyable struct with no
// out-of-line code, so it needs no CMake source listing (matching the
// Util/BitSet.hpp / Util/LongSet.hpp header-only precedent).

#pragma once

#include <cstdint>
#include <string>

namespace ILSpy::Decompiler::CSharp::Syntax {

// A line/column position. Lines and columns are 1-based; (0, 0) is Empty.
struct TextLocation {
    static constexpr int MinLine = 1;
    static constexpr int MinColumn = 1;

    static const TextLocation Empty;

    int Line;
    int Column;

    TextLocation() : Line(0), Column(0) {}

    TextLocation(int line, int column) : Line(line), Column(column) {}

    bool IsEmpty() const {
        return Column < MinLine && Line < MinColumn;
    }

    // Matches the C# `string.Format("(Line {1}, Col {0})", column, line)`.
    std::string ToString() const;

    // The C# `unchecked(191 * column.GetHashCode() ^ line.GetHashCode())`;
    // int.GetHashCode() is the value, and the arithmetic is done through
    // unsigned to replicate `unchecked` (signed overflow is UB in C++).
    int GetHashCode() const {
        std::uint32_t h = 191u * static_cast<std::uint32_t>(Column);
        h ^= static_cast<std::uint32_t>(Line);
        return static_cast<int>(h);
    }

    int CompareTo(const TextLocation& other) const {
        if (*this == other)
            return 0;
        return *this < other ? -1 : 1;
    }

    friend bool operator==(const TextLocation& left, const TextLocation& right) {
        return left.Column == right.Column && left.Line == right.Line;
    }

    friend bool operator!=(const TextLocation& left, const TextLocation& right) {
        return !(left == right);
    }

    // Line-major, then column (the C# operator order).
    friend bool operator<(const TextLocation& left, const TextLocation& right) {
        if (left.Line < right.Line)
            return true;
        if (left.Line == right.Line)
            return left.Column < right.Column;
        return false;
    }

    friend bool operator>(const TextLocation& left, const TextLocation& right) {
        if (left.Line > right.Line)
            return true;
        if (left.Line == right.Line)
            return left.Column > right.Column;
        return false;
    }

    friend bool operator<=(const TextLocation& left, const TextLocation& right) {
        return !(left > right);
    }

    friend bool operator>=(const TextLocation& left, const TextLocation& right) {
        return !(left < right);
    }
};

inline const TextLocation TextLocation::Empty{0, 0};

inline std::string TextLocation::ToString() const {
    return "(Line " + std::to_string(Line) + ", Col " + std::to_string(Column) + ")";
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
