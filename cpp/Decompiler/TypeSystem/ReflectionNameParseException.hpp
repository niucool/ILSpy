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

// Port of `ICSharpCode.Decompiler/TypeSystem/ReflectionNameParseException.cs` -- the
// exception `ReflectionHelper.ParseReflectionName` throws when a reflection name does
// not parse (and the exception `ReadTypeParameterCount` throws when a type-parameter
// count is expected but absent). The C# class carries the parse `position` alongside
// the standard exception message; `ParseReflectionName` always throws at position 0
// (the `System.Reflection.Metadata` `TypeName.TryParse` failure carries no position),
// while `ReadTypeParameterCount` throws at the position its scan stopped at.
//
// The port models the C# exception-hierarchy member the consumers observe -- the
// `Exception` base's `Message` and the class's own `Position` -- as a
// `std::runtime_error` subclass (the XmlException / CSharpPrimitiveCast exception
// precedent: the C# `Exception` base becomes `std::runtime_error`, custom members stay
// on the subclass). The serialization ctor (`SerializationInfo`/`StreamingContext`) is
// .NET-remoting machinery with no C++ counterpart (the documented N/A surface), and
// the `ReflectionNameParseException(int position, string message, Exception
// innerException)` chained ctor is unused by the ported call sites
// (`ParseReflectionName` / `ReadTypeParameterCount` throw the two-arg form; the
// one-arg form has no ported consumer either but lands for completeness, mirroring the
// C# public surface).

#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `public class ReflectionNameParseException : Exception` -- the reflection-name
// parse error. `Position` is the zero-based index into the reflection name where the
// parse failed (always 0 from `ParseReflectionName`; the scan position from
// `ReadTypeParameterCount`).
class ReflectionNameParseException : public std::runtime_error {
public:
    // The C# `ReflectionNameParseException(int position)` -- an unnamed parse error.
    // The parameterless-`Exception()` message: "Exception of type
    // 'ICSharpCode.Decompiler.TypeSystem.ReflectionNameParseException' was thrown.".
    explicit ReflectionNameParseException(int position)
        : std::runtime_error(
              "Exception of type 'ICSharpCode.Decompiler.TypeSystem."
              "ReflectionNameParseException' was thrown."),
          position_(position) {}

    // The C# `ReflectionNameParseException(int position, string message)` -- the
    // parse error with its message ("Invalid type name: <name>" from
    // `ParseReflectionName`; "Expected type parameter count" from
    // `ReadTypeParameterCount`).
    ReflectionNameParseException(int position, std::string message)
        : std::runtime_error(std::move(message)), position_(position) {}

    // The C# `int Position { get; }` -- the failure position.
    int Position() const noexcept { return position_; }

private:
    int position_;
};

} // namespace ILSpy::Decompiler::TypeSystem
