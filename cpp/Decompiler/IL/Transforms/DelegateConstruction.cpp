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

#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

namespace ILSpy::Decompiler::IL {

bool DelegateConstruction::MatchDelegateConstruction(ILInstruction* inst,
                                                      DelegateConstructionMatch& out,
                                                      bool allowTransformed) {
    out = DelegateConstructionMatch{};
    // The C# switches on `inst` (NewObj or LdVirtDelegate); this port models a
    // newobj as a Call with IsNewObj, and the LdVirtDelegate node now exists
    // (D121) but this helper still handles only the NewObj (Call) case -- the
    // LdVirtDelegate branch lands with the full DelegateConstruction transform
    // (which runs after StatementTransform, by which point
    // ExpressionTransforms.TransformDelegateCtorLdVirtFtnToLdVirtDelegate has
    // already folded virtual delegate constructions to LdVirtDelegate).
    if (!inst || inst->Op != OpCode::Call) return false;
    auto* call = static_cast<Call*>(inst);
    if (!call->IsNewObj) return false;
    if (call->Arguments.size() != 2) return false;

    // The second argument is the function pointer: ldftn or ldvirtftn. The C#
    // also accepts an ILFunction when allowTransformed (after the DelegateConstruction
    // transform rewrites the NewObj into a closure); that case never arises here
    // (the transform is not ported), so allowTransformed has no effect.
    (void)allowTransformed;
    auto* opArg = call->Arguments[1].get();
    if (!opArg) return false;
    if (opArg->Op != OpCode::LdFtn && opArg->Op != OpCode::LdVirtFtn) return false;

    // A null declaring type is treated like the C# null DeclaringTypeDefinition.
    if (!call->DeclaringType) return false;
    TypeSystem::TypeKind kind = call->DeclaringType->Kind();
    if (kind != TypeSystem::TypeKind::Delegate && kind != TypeSystem::TypeKind::Unknown)
        return false;

    out.target = call->Arguments[0].get();
    out.delegateType = call->DeclaringType;
    out.targetMethod = (opArg->Op == OpCode::LdFtn)
        ? static_cast<LdFtn*>(opArg)->MethodName
        : static_cast<LdVirtFtn*>(opArg)->MethodName;
    return true;
}

} // namespace ILSpy::Decompiler::IL
