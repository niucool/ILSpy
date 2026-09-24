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
#include "Decompiler/IL/Transforms/TransformExpressionTrees.hpp"

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::IL {

bool TransformExpressionTrees::MatchGetTypeFromHandle(
    ILInstruction* inst, ::ILSpy::Decompiler::TypeSystem::ITypePtr& type) {
    type = nullptr;
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj || call->Method == nullptr ||
        call->Arguments.size() != 1) {
        return false;
    }
    // The C# `getTypeCall.Method.FullName == "System.Type.GetTypeFromHandle"`:
    // the declaring type's full name + the method name pair.
    ::ILSpy::Decompiler::TypeSystem::ITypePtr declaring =
        call->Method->DeclaringType();
    if (declaring == nullptr ||
        !(declaring->Namespace() == "System" && declaring->Name() == "Type")) {
        return false;
    }
    if (call->Method->Name() != "GetTypeFromHandle") return false;
    auto* token = dynamic_cast<LdTypeToken*>(call->Arguments[0].get());
    if (token == nullptr || token->Type == nullptr) return false;
    type = token->Type;
    return true;
}


void TransformExpressionTrees::Run(Block& block, int pos,
                                   StatementTransformContext& context) {
    if (!context.Base.Settings.ExpressionTrees) return;
    // The C# body: scan forward from pos for the parameter-variable
    // assignments and the first Lambda call. The C# `break` after the first
    // non-parameter instruction is the statement-transform re-run driver's
    // contract. The port's ConvertLambda deferred-thunk tree lands per-arm;
    // this Run drives the scan and consults the match helpers.
    for (int i = pos; i < static_cast<int>(block.Instructions.size()); i++) {
        // The C# MatchParameterVariableAssignment arm (a recorded parameter
        // continues the scan; anything else breaks) is deferred with the
        // ConvertLambda thunk tree (the next slice in this file).
        (void)block;
        (void)i;
        break;
    }
}



namespace {

// The C# `method.FullNameIs(ns, name)` extension: true iff the method's
// declaring type is the namespace-qualified type `ns.name` and the method's
// simple name matches. (The C# name check is `method.Name == name`; the port
// uses the same pair.)
bool FullNameIs(const TypeSystem::IMethod* method, const std::string& ns,
                const std::string& typeName, const std::string& name) {
    if (method == nullptr) return false;
    if (method->Name() != name) return false;
    TypeSystem::ITypePtr declaring = method->DeclaringType();
    if (declaring == nullptr) return false;
    return declaring->Namespace() == ns && declaring->Name() == typeName;
}

} // namespace

bool TransformExpressionTrees::MatchParameterVariableAssignment(
    ILInstruction* expr, std::shared_ptr<ILVariable>& parameterReferenceVar,
    TypeSystem::ITypePtr& type, std::string& name) {
    // stloc(v, call(Expression::Parameter,
    //               call(Type::GetTypeFromHandle, ldtoken(...)),
    //               ldstr(...)))
    type = nullptr;
    name.clear();
    auto* stloc = dynamic_cast<StLoc*>(expr);
    if (stloc == nullptr || stloc->Variable == nullptr) return false;
    parameterReferenceVar = stloc->Variable;
    if (!parameterReferenceVar->IsSingleDefinition()) return false;
    if (!(parameterReferenceVar->Kind == VariableKind::Local ||
          parameterReferenceVar->Kind == VariableKind::StackSlot)) {
        return false;
    }
    const auto& variableType = parameterReferenceVar->Type;
    if (variableType == nullptr ||
        variableType->Namespace() != "System.Linq.Expressions" ||
        variableType->Name() != "ParameterExpression") {
        return false;
    }
    auto* initCall = dynamic_cast<Call*>(stloc->Value.get());
    if (initCall == nullptr || initCall->Arguments.size() != 2 ||
        initCall->IsNewObj) {
        return false;
    }
    if (!FullNameIs(initCall->Method.get(), "System.Linq.Expressions",
                    "Expression", "Parameter")) {
        return false;
    }
    auto* typeArg = dynamic_cast<Call*>(initCall->Arguments[0].get());
    if (typeArg == nullptr || typeArg->Arguments.size() != 1 ||
        typeArg->IsNewObj) {
        return false;
    }
    if (!FullNameIs(typeArg->Method.get(), "System", "Type",
                    "GetTypeFromHandle")) {
        return false;
    }
    auto* token = dynamic_cast<LdTypeToken*>(typeArg->Arguments[0].get());
    if (token == nullptr || token->Type == nullptr) return false;
    type = token->Type;
    auto* nameArg = dynamic_cast<LdStr*>(initCall->Arguments[1].get());
    if (nameArg == nullptr) return false;
    name = nameArg->Value;
    return true;
}

bool TransformExpressionTrees::MightBeExpressionTree(ILInstruction* inst,
                                                     ILInstruction* stmt) {
    (void)stmt;  // the C# CanUninline gate is commented out there too
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj ||
        !FullNameIs(call->Method.get(), "System.Linq.Expressions", "Expression",
                    "Lambda") ||
        call->Arguments.size() != 2) {
        return false;
    }
    if (!IsEmptyParameterList(call->Arguments[1].get())) {
        return false;
    }
    return true;
}

bool TransformExpressionTrees::IsEmptyParameterList(ILInstruction* inst) {
    if (auto* emptyCall = dynamic_cast<Call*>(inst)) {
        if (!emptyCall->IsNewObj && emptyCall->Arguments.empty() &&
            FullNameIs(emptyCall->Method.get(), "System", "Array", "Empty")) {
            return true;
        }
    }
    if (auto* newArr = dynamic_cast<NewArr*>(inst)) {
        if (newArr->Type != nullptr &&
            newArr->Type->Namespace() == "System.Linq.Expressions" &&
            (newArr->Type->Name() == "ParameterExpression" ||
             newArr->Type->Name() == "Expression")) {
            return true;
        }
    }
    return false;
}


} // namespace ILSpy::Decompiler::IL
