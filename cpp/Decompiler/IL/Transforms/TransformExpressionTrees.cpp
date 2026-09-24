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


} // namespace ILSpy::Decompiler::IL
