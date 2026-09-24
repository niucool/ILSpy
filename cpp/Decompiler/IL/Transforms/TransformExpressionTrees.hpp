// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
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

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformExpressionTrees.cs --
// converts LINQ Expression.Lambda call trees into ILFunctions. The C# builds
// a tree of Func<ILInstruction> thunks that only execute if the whole
// transform succeeds; the port carries the same two-phase shape (the Convert*
// functions return a deferred-thunk + type pair).
//
// Deferrals (each documented at the arm):
//  - the C# `parameters` dictionary is read-only state the transform runs
//    carry; the port carries it as a plain map.
//  - the closure-reference arm (TransformDisplayClassUsage.IsPotentialClosure)
//    is deferred with that surface (the C# needs the decompiled-type
//    definition).
//  - `MatchLdTypeToken` / `MatchLdMemberToken` are token-instruction dynamic
//    casts over the port's LdTypeToken / (MethodDef token) nodes.

#pragma once

#include "Decompiler/IL/Transforms/StatementTransform.hpp"

#include <memory>
#include <string>
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::IL {

class ILVariable;

class TransformExpressionTrees final : public IStatementTransform {
public:
    // The C# `internal static bool MatchGetTypeFromHandle(ILInstruction inst,
    // out IType type)` (a static helper the tests use).
    static bool MatchGetTypeFromHandle(ILInstruction* inst,
                                       ::ILSpy::Decompiler::TypeSystem::ITypePtr& type);

    void Run(Block& block, int pos, StatementTransformContext& context) override;

    // The C# `bool MatchParameterVariableAssignment(ILInstruction expr,
    // out ILVariable, out IType, out string)`: match the
    // `stloc(v, Expression.Parameter(GetTypeFromHandle(ldtoken T), "name"))`
    // assignment shape the compiler emits for each lambda parameter.
    static bool MatchParameterVariableAssignment(
        ILInstruction* expr, std::shared_ptr<ILVariable>& parameterReferenceVar,
        ::ILSpy::Decompiler::TypeSystem::ITypePtr& type, std::string& name);

    // The C# `static bool MightBeExpressionTree(ILInstruction inst,
    // ILInstruction stmt)`: a candidate `Expression.Lambda(body, args)` call
    // (2 arguments; the second an empty parameter-list expression).
    static bool MightBeExpressionTree(ILInstruction* inst, ILInstruction* stmt);

    // The C# `static bool IsEmptyParameterList(ILInstruction)`: the
    // `Array.Empty<T>()` / `newarr ParameterExpression` /
    // `newarr Expression` forms.
    static bool IsEmptyParameterList(ILInstruction* inst);
};

} // namespace ILSpy::Decompiler::IL