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

// Port of the `CSharpOutputVisitor.IsKeyword(string identifier, AstNode? context = null)`
// static helper in ICSharpCode.Decompiler/CSharp/OutputVisitor/CSharpOutputVisitor.cs (the
// `#region IsKeyword Test` block). The helper decides whether an identifier token must be
// rendered with a leading `@` (the verbatim-identifier prefix) because its name is a reserved
// C# keyword in the identifier's context. It is consumed by `TextWriterTokenWriter.WriteIdentifier`
// (the concrete token writer that writes the tokens to an `std::ostream`, not yet ported) and by
// the location-setting decorator's identifier handling; it is factored out here as a free function
// so the concrete `TextWriterTokenWriter` can land before the 97k-line `CSharpOutputVisitor`
// pretty-printer (the D317 plan's "port IsKeyword as a free helper" resolution of the tangle where
// `TextWriterTokenWriter.WriteIdentifier` otherwise references the not-yet-ported
// `CSharpOutputVisitor.IsKeyword`). When `CSharpOutputVisitor` lands it can expose a static
// `IsKeyword` that forwards to this free function (the canonical implementation), or the call sites
// can be redirected here; the behaviour is identical either way.
//
// The helper consults three keyword classes:
//  * the 77 UNCONDITIONAL keywords (reserved in every context -- `abstract` .. `while`);
//  * the 13 CONTEXTUAL query keywords (`from`/`where`/`join`/.../`by`), reserved only inside a
//    query expression (a `QueryExpression` ancestor); with a null context they are treated as
//    unconditional (the C# `context == null || context.Ancestors.Any(a => a is QueryExpression)`);
//  * the contextual `await` keyword, reserved only inside an async lambda / anonymous method /
//    async member declaration (a `LambdaExpression`/`AnonymousMethodExpression` ancestor whose
//    `IsAsync` is set, or an `EntityDeclaration` ancestor whose `Modifiers` carries `Async`); with
//    a null context `await` is unconditional.
//
// Every dependency the helper consults is already ported: `AstNode::Ancestors()` (the parent
// chain, D221), `QueryExpression` (D294/D310), `LambdaExpression.IsAsync` (D305),
// `AnonymousMethodExpression.IsAsync` (D306), `EntityDeclaration.Modifiers()` (D272), and the
// `Modifiers::Async` flag (D270).
//
// C#-to-C++ porting decisions:
//  * the C# `static readonly HashSet<string>` keyword tables port as function-local `static const
//    std::unordered_set<std::string_view>` initialized from string-literal views. The literals
//    have static storage duration, so the views the set stores remain valid for the program
//    lifetime; a lookup with a `std::string_view` key (an identifier's `Name`) is zero-allocation
//    and zero-copy (C++17 has `std::hash<std::string_view>`, so `unordered_set<string_view>`
//    hashes the view directly, unlike an `unordered_set<std::string>` which would allocate on
//    every lookup). This is the faithful equivalent of the C# `HashSet<string>.Contains`.
//  * the C# `static readonly int maxKeywordLength = unconditionalKeywords.Concat(queryKeywords)
//    .Max(s => s.Length)` ports as a function-local `static const int` computed once by iterating
//    both sets (the same computation, lazily initialized on first call -- thread-safe under the
//    C++11+ function-local static init rule).
//  * the C# `string identifier` parameter ports as `std::string_view` (read-only, zero-copy).
//  * the C# `AstNode? context = null` parameter ports as `AstNode* context = nullptr`.
//  * the C# `identifier[0] < 'a'` gate ports with an `unsigned char` cast: the C# `char` is an
//    unsigned 16-bit code unit, so a non-ASCII first byte (a negative `signed char` in C++) would
//    wrongly trip the `< 'a'` gate without the cast. The observable result is the same either way
//    (a non-ASCII-starting identifier is never a keyword), but the cast keeps the gate's semantics
//    faithful to the C# unsigned comparison.
//  * the C# `context.Ancestors.Any(ancestor => ancestor is QueryExpression)` ports as a walk over
//    the ported `AstNode::Ancestors()` vector with a `dynamic_cast<QueryExpression*>` is-a test
//    (the faithful C# `is`).

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPKEYWORDCHECK_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPKEYWORDCHECK_HPP

#include <algorithm>
#include <string_view>
#include <unordered_set>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"                          // AstNode, Ancestors()
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"               // EntityDeclaration, Modifiers()
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"                        // Modifiers::Async
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"  // AnonymousMethodExpression, IsAsync()
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"    // LambdaExpression, IsAsync()
#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"      // QueryExpression

namespace ILSpy::Decompiler::CSharp {

// The `Syntax` node types the helper consults, brought into the `OutputVisitor` namespace so the
// signature and the body read unqualified.
using Syntax::AstNode;
using Syntax::EntityDeclaration;
using Syntax::LambdaExpression;
using Syntax::AnonymousMethodExpression;
using Syntax::QueryExpression;
using Syntax::Modifiers;

namespace OutputVisitor {

// Determines whether the given identifier is a C# keyword in the given context and so must be
// rendered with a leading `@` (the verbatim-identifier prefix). With a null `context` every
// keyword (unconditional, query, and `await`) is treated as unconditional -- the C# default
// argument and the `context == null` short-circuits.
inline bool IsKeyword(std::string_view identifier, AstNode* context = nullptr)
{
	// The C# keyword tables (`static readonly HashSet<string>`). Stored as `string_view`s over
	// string literals (static storage duration), so the views are valid for the program lifetime
	// and the lookups are zero-allocation. Function-local statics initialize once (thread-safe).
	static const std::unordered_set<std::string_view> unconditionalKeywords = {
		"abstract", "as", "base", "bool", "break", "byte", "case", "catch",
		"char", "checked", "class", "const", "continue", "decimal", "default", "delegate",
		"do", "double", "else", "enum", "event", "explicit", "extern", "false",
		"finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit",
		"in", "int", "interface", "internal", "is", "lock", "long", "namespace",
		"new", "null", "object", "operator", "out", "override", "params", "private",
		"protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
		"sizeof", "stackalloc", "static", "string", "struct", "switch", "this", "throw",
		"true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort",
		"using", "virtual", "void", "volatile", "while"
	};
	static const std::unordered_set<std::string_view> queryKeywords = {
		"from", "where", "join", "on", "equals", "into", "let", "orderby",
		"ascending", "descending", "select", "group", "by"
	};
	// The C# `Max(s => s.Length)` over both tables -- computed once on first call.
	static const int maxKeywordLength = [] {
		int m = 0;
		for (auto s : unconditionalKeywords)
			m = std::max(m, static_cast<int>(s.size()));
		for (auto s : queryKeywords)
			m = std::max(m, static_cast<int>(s.size()));
		return m;
	}();

	// only 2..maxKeywordLength-char lower-case identifiers can be keywords. The `size() < 2`
	// check short-circuits before `identifier[0]` is evaluated, so an empty view is safe. The
	// `unsigned char` cast reproduces the C# unsigned-`char` comparison for a non-ASCII first
	// byte (a negative `signed char` would otherwise trip the `< 'a'` gate).
	if (identifier.size() > maxKeywordLength
		|| identifier.size() < 2
		|| static_cast<unsigned char>(identifier[0]) < 'a')
	{
		return false;
	}

	if (unconditionalKeywords.count(identifier) != 0)
	{
		return true;
	}

	if (queryKeywords.count(identifier) != 0)
	{
		// The C# `context == null || context.Ancestors.Any(a => a is QueryExpression)`.
		if (context == nullptr)
			return true;
		for (AstNode* ancestor : context->Ancestors())
		{
			if (dynamic_cast<QueryExpression*>(ancestor) != nullptr)
				return true;
		}
		return false;
	}

	if (identifier == "await")
	{
		// The C# `if (context == null) return true;` then a walk up the ancestors: the first
		// lambda / anonymous method / member declaration decides (an async lambda/anonymous
		// method makes `await` a keyword; a member declaration's `Modifiers & Async` decides;
		// the first such ancestor encountered wins).
		if (context == nullptr)
			return true;
		for (AstNode* ancestor : context->Ancestors())
		{
			if (auto* lambda = dynamic_cast<LambdaExpression*>(ancestor))
			{
				return lambda->IsAsync();
			}
			if (auto* anon = dynamic_cast<AnonymousMethodExpression*>(ancestor))
			{
				return anon->IsAsync();
			}
			if (auto* entity = dynamic_cast<EntityDeclaration*>(ancestor))
			{
				return (entity->Modifiers() & Modifiers::Async) == Modifiers::Async;
			}
		}
	}

	return false;
}

}  // namespace OutputVisitor
}  // namespace ILSpy::Decompiler::CSharp

#endif  // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPKEYWORDCHECK_HPP
