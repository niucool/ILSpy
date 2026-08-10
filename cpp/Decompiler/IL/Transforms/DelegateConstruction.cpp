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
    // (D121). Both branches are handled here: the NewObj (Call) branch (the
    // D76 foundation) and the LdVirtDelegate branch (the case that arises after
    // ExpressionTransforms.TransformDelegateCtorLdVirtFtnToLdVirtDelegate has
    // folded a virtual delegate construction to an LdVirtDelegate).
    if (!inst) return false;

    if (inst->Op == OpCode::Call) {
        // The `case NewObj call:` branch -- a newobj delegate construction.
        auto* call = static_cast<Call*>(inst);
        if (!call->IsNewObj) return false;
        if (call->Arguments.size() != 2) return false;

        // The second argument is the function pointer: ldftn or ldvirtftn. The
        // C# also accepts an ILFunction when allowTransformed (after the
        // DelegateConstruction transform rewrites the NewObj into a closure);
        // that case never arises here (the transform is not ported), so
        // allowTransformed has no effect.
        (void)allowTransformed;
        auto* opArg = call->Arguments[1].get();
        if (!opArg) return false;
        if (opArg->Op != OpCode::LdFtn && opArg->Op != OpCode::LdVirtFtn) return false;

        out.target = call->Arguments[0].get();
        out.delegateType = call->DeclaringType;
        out.targetMethod = (opArg->Op == OpCode::LdFtn)
            ? static_cast<LdFtn*>(opArg)->MethodName
            : static_cast<LdVirtFtn*>(opArg)->MethodName;
    } else if (inst->Op == OpCode::LdVirtDelegate) {
        // The `case LdVirtDelegate ldVirtDelegate:` branch -- a virtual delegate
        // construction already folded by TransformDelegateCtorLdVirtFtnToLdVirtDelegate.
        // The C# captures target = ldVirtDelegate.Argument, targetMethod =
        // ldVirtDelegate.Method (this port's MethodName string stand-in), and
        // delegateType = ldVirtDelegate.Type. allowTransformed is not consulted
        // by the C# for this branch.
        (void)allowTransformed;
        auto* ldv = static_cast<LdVirtDelegate*>(inst);
        out.target = ldv->Argument.get();
        out.targetMethod = ldv->MethodName;
        out.delegateType = ldv->Type;
    } else {
        return false;
    }

    // The C# final gate: `delegateType.Kind == Delegate || Unknown`. A null
    // declaring type is treated like the C# null DeclaringTypeDefinition (the
    // NewObj branch's defensive guard, applied uniformly to both branches).
    if (!out.delegateType) return false;
    TypeSystem::TypeKind kind = out.delegateType->Kind();
    return kind == TypeSystem::TypeKind::Delegate || kind == TypeSystem::TypeKind::Unknown;
}

} // namespace ILSpy::Decompiler::IL
