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

#include <functional>
#include <map>
#include <memory>
#include <string>
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::TypeSystem {
class IParameter;
}

namespace ILSpy::Decompiler::IL {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

class ILVariable;
class StLoc;
class Call;
class ILFunction;

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

    // The instance state (the C# `this.context/conversions/resolver/
    // parameters/parameterMapping/instructionsToRemove/lambdaStack` fields).
    // The conversions/resolver pair is deferred with the resolver-backed arms
    // (ConvertComparison/ConvertBinaryNumericOperator's user-defined-operator
    // forms); the parameter maps drive the LdLoc arms below.
    struct State {
        // The C# `Dictionary<ILVariable, (IType, string)> parameters`.
        std::map<ILVariable*, std::pair<::ILSpy::Decompiler::TypeSystem::ITypePtr, std::string>>
            parameters;
        // The C# `Dictionary<ILVariable, ILVariable> parameterMapping`.
        std::map<ILVariable*, std::shared_ptr<ILVariable>> parameterMapping;
        std::vector<ILInstruction*> instructionsToRemove;
        std::vector<ILFunction*> lambdaStack;
        // The storing stloc per recorded parameter (the port's ILVariable does
        // not track store instructions; the C# reads
        // `v.StoreInstructions[0]` in ReadParameters).
        std::map<ILVariable*, StLoc*> parameterStores;
    };

    // The per-Run state (re-initialized at the top of Run, the C# fields
    // re-assigned there).
    State state_;
    // The enclosing StatementTransformContext (set at the top of Run; the C#
    // `this.context` field the Convert* arms consult).
    StatementTransformContext* context_ = nullptr;

    // The C# `bool TryConvertExpressionTree(ILInstruction, ILInstruction)`:
    // walk the instruction tree for the first MightBeExpressionTree call.
    bool TryConvertExpressionTree(ILInstruction* instruction,
                                  ILInstruction* statement);

    // The C# `(Func<ILInstruction>, IType) ConvertLambda(CallInstruction)`:
    // builds the ILFunction (Kind ExpressionTree/Delegate) over the converted
    // body; a deferred BuildFunction thunk pattern (the C# local function
    // captured by the returned Func). Returns {nullptr, null type} on failure.
    struct ConvertResult {
        std::function<std::unique_ptr<ILInstruction>()> thunk;
        ::ILSpy::Decompiler::TypeSystem::ITypePtr type;
    };

    ConvertResult ConvertLambda(Call* instruction);
    ConvertResult ConvertInstruction(ILInstruction* instruction,
                                     TypeSystem::IType* typeHint = nullptr);

    // The C# `bool IsExpressionTree(IType)` / `IType UnwrapExpressionTree(IType)`:
    // the `Expression<T>` ParameterizedType probe and its element-type unwrap.
    static bool IsExpressionTree(const TypeSystem::IType& delegateType);
    static ::ILSpy::Decompiler::TypeSystem::ITypePtr UnwrapExpressionTree(
        const TypeSystem::IType& delegateType);

    // The C# `void SetExpressionTreeFlag(ILFunction, CallInstruction)`.
    static void SetExpressionTreeFlag(ILFunction& lambda, const Call& call);

    // The C# `bool ReadParameters(ILInstruction, List<IParameter>,
    // List<ILVariable>, SimpleTypeResolveContext)`: the initializer-block arm
    // over the recorded parameter assignments. The C# `context` parameter is
    // unused there and is dropped (the port carries no dead parameter).
    bool ReadParameters(ILInstruction* initializer,
                        std::vector<std::shared_ptr<const ::ILSpy::Decompiler::TypeSystem::IParameter>>&
                            parameterList,
                        std::vector<std::shared_ptr<ILVariable>>&
                            parameterVariables);

    // The C# `(Func<ILInstruction>, IType) ConvertConstant(CallInstruction)`
    // and its `MatchConstantCall` helper (the value+type pair extraction).
    ConvertResult ConvertConstant(Call* invocation);
    bool MatchConstantCall(ILInstruction* inst, ILInstruction*& value,
                           ::ILSpy::Decompiler::TypeSystem::ITypePtr& type);

    // The C# `ILInstruction ConvertValue(ILInstruction, ILInstruction)`:
    // clone the constant operand, remapping parameter loads. The closure-
    // reference arm is deferred with TransformDisplayClassUsage.
    std::unique_ptr<ILInstruction> ConvertValue(ILInstruction* value,
                                                ILInstruction* context);

    // The C# `(Func<ILInstruction>, IType) ConvertQuote(CallInstruction)`.
    ConvertResult ConvertQuote(Call* invocation);
};

} // namespace ILSpy::Decompiler::IL