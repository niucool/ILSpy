// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Transforms/CustomPatterns.cs -- the three
// hand-written patterns the C# transforms (and the pattern-based AST rewrite
// machinery) match with, which the generated pattern nodes cannot express:
// `TypePattern` (an AST type whose resolve-result type has a given namespace and
// short name), `LdTokenPattern` (a `ldtoken(...)` invocation wrapping a single
// argument, captured under a group), and `TypeOfPattern` (the expanded
// `typeof(...)` shape the decompiler produces before
// `ReplaceMethodCallsWithOperators` collapses it).
//
// The C# `TypePattern(Type type)` ctor reads `type.Namespace` / `type.Name` off a
// `System.Type`; the port has no such reflection type, so the ctor takes the two
// strings directly (the call sites pass the `typeof(MethodInfo)` equivalent). The
// `ToString` diagnostics the C# overrides emit are omitted, matching the port's
// existing pattern nodes (INode has no `ToString`).

#pragma once

#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {
class AnyNode;
class INode;
} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `sealed class TypePattern : Pattern`: matches an `AstType` (or a
// `ComposedType` that carries no modifiers, whose `BaseType` is then examined)
// whose resolve-result type has the stored namespace and short name.
class TypePattern final : public Syntax::PatternMatching::Pattern {
    std::string namespace_;
    std::string name_;

public:
    TypePattern(std::string namespaceName, std::string name);

    const std::string& NamespaceName() const { return namespace_; }
    const std::string& TypeName() const { return name_; }

    bool DoMatch(Syntax::PatternMatching::INode* other,
                 Syntax::PatternMatching::Match match) override;
};

// The C# `sealed class LdTokenPattern : Pattern`: matches an
// `InvocationExpression` carrying a `LdTokenAnnotation` with exactly one argument,
// delegating the argument to an internal `AnyNode` so it is captured under the
// group name. (`ldtoken(...)` is the IL-token pseudo-call the decompiler emits
// before it is rewritten to a `typeof` / method reference.)
class LdTokenPattern final : public Syntax::PatternMatching::Pattern {
    std::unique_ptr<Syntax::PatternMatching::AnyNode> childNode_;

public:
    explicit LdTokenPattern(std::string groupName);

    bool DoMatch(Syntax::PatternMatching::INode* other,
                 Syntax::PatternMatching::Match match) override;
};

// The C# `sealed class TypeOfPattern : Pattern`: matches the expanded `typeof`
// shape `System.Type.GetTypeFromHandle(typeof(T)).TypeHandle` and captures `T`
// (the `typeof` argument) under the group name. The C# builds its child node tree
// in the ctor; the port owns that tree through the pattern.
class TypeOfPattern final : public Syntax::PatternMatching::Pattern {
    std::unique_ptr<Syntax::PatternMatching::INode> childNode_;

public:
    explicit TypeOfPattern(std::string groupName);

    bool DoMatch(Syntax::PatternMatching::INode* other,
                 Syntax::PatternMatching::Match match) override;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
