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

// Port of the `public static class Tokens` in
// ICSharpCode.Decompiler/CSharp/Syntax/Tokens.cs -- the predefined `const string` punctuation
// and keyword token literals the `CSharpOutputVisitor` (and the back end) write via
// `WriteToken`/`WriteKeyword`. A C# `static class` of `public const string` fields ports as a
// namespace of `inline constexpr const char*` constants (the established static-class -> namespace
// convention; the `FormattingOptionsFactory` D317 precedent). Each points at a string literal
// with static storage duration, so the pointer is valid for the program lifetime and binds
// directly to the `std::string_view` parameters of `TokenWriter::WriteToken`/`WriteKeyword`
// without an allocation (zero-copy, the established read-only-string convention).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_TOKENS_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_TOKENS_HPP

namespace ILSpy::Decompiler::CSharp::Syntax::Tokens {

// The predefined punctuation constants (the C# `public const string` fields).
inline constexpr const char* LPar = "(";
inline constexpr const char* RPar = ")";
inline constexpr const char* LBracket = "[";
inline constexpr const char* RBracket = "]";
inline constexpr const char* LBrace = "{";
inline constexpr const char* RBrace = "}";
inline constexpr const char* LChevron = "<";
inline constexpr const char* RChevron = ">";
inline constexpr const char* Comma = ",";
inline constexpr const char* Dot = ".";
inline constexpr const char* Semicolon = ";";
inline constexpr const char* Assign = "=";
inline constexpr const char* Colon = ":";
inline constexpr const char* DoubleColon = "::";
inline constexpr const char* Arrow = "=>";

// The predefined keyword constants.
inline constexpr const char* WhereKeyword = "where";
inline constexpr const char* DelegateKeyword = "delegate";
inline constexpr const char* ExternKeyword = "extern";
inline constexpr const char* AliasKeyword = "alias";
inline constexpr const char* NamespaceKeyword = "namespace";

inline constexpr const char* EnumKeyword = "enum";
inline constexpr const char* InterfaceKeyword = "interface";
inline constexpr const char* StructKeyword = "struct";
inline constexpr const char* ClassKeyword = "class";
inline constexpr const char* RecordKeyword = "record";
inline constexpr const char* RecordStructKeyword = "record";

} // namespace ILSpy::Decompiler::CSharp::Syntax::Tokens

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_TOKENS_HPP
