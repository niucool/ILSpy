// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/ICodeContext.cs -- the resolution context
// a lambda body resolves against: an `ITypeResolveContext` (module / using scope / type
// definition / member slots, D409) plus the two code-level members -- the currently
// visible local variables and the within-lambda-expression flag. In the C# source the
// interface's ONLY implementor is `CSharpResolver` (ICSharpCode.Decompiler/CSharp/
// Resolver/CSharpResolver.cs line 40, `public class CSharpResolver : ICodeContext`);
// the interface exists so resolver-adjacent code can be typed against the context
// without the whole resolver.
//
// The C# `IEnumerable<IVariable> LocalVariables` ports to a `std::vector` snapshot of
// owning `shared_ptr<const IVariable>` handles (the caller receives an owning snapshot
// of the variables the implementing context reports; the values are `const` because the
// context only ever READS the local variables -- lookups, never mutation). The C#
// `bool IsWithinLambdaExpression` ports to a plain `bool` accessor.
//
// The `CSharpResolver` skeleton (the port's first piece of the mega-class) does NOT yet
// derive this interface: doing so would force the resolver's `WithCurrentTypeDefinition`
// / `WithCurrentMember` overrides to the interface's `std::unique_ptr<ITypeResolveContext>`
// signature, losing the concrete `std::shared_ptr<CSharpResolver>` clone-factory returns
// every consumer chains (`With*` + `Resolve*`). Since the interface has no other C#
// consumers, the derivation is deferred with no behavioral loss; see the
// `CSharpResolver.hpp` header comment for the full rationale.

#pragma once

#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `public interface ICodeContext : ITypeResolveContext` -- adds the code-level
// members to the four resolution slots: the visible local variables / lambda parameters
// (NOT method parameters) and whether the context is within a lambda expression or
// anonymous method.
class ICodeContext : public ITypeResolveContext {
public:
    // The C# `IEnumerable<IVariable> LocalVariables { get; }` -- all currently visible
    // local variables and lambda parameters. Does not include method parameters. Returns
    // an owning snapshot (`shared_ptr<const IVariable>` handles) because the variables
    // are owned by the implementing context's local-variable storage, which the snapshot
    // must keep alive.
    virtual std::vector<std::shared_ptr<const IVariable>> LocalVariables() const = 0;

    // The C# `bool IsWithinLambdaExpression { get; }` -- whether the context is within a
    // lambda expression or anonymous method.
    virtual bool IsWithinLambdaExpression() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
