// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Implementation of the `TypeInference` pure leaf helpers (see TypeInferenceHelpers.hpp for
// the region overview and the return conventions).

#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"

#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (the RTTI base)
#include "Decompiler/TypeSystem/IParameter.hpp"  // IParameter (the delegate-invoke parameter types)
#include "Decompiler/TypeSystem/IMethod.hpp"  // IMethod (Parameters() / ReturnType())
#include "Decompiler/TypeSystem/IType.hpp"  // IType / ParameterizedType (the unwrap)
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (Namespace via GetDefinition)
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetDelegateInvokeMethod (the D533 free function)

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

using ILSpy::Decompiler::TypeSystem::GetDelegateInvokeMethod;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;

// The C# `static IMethod GetDelegateOrExpressionTreeSignature(IType t)` (TypeInference.cs,
// the Input Types / Output Types region) -- the `Expression<T>` unwrap followed by the
// delegate `Invoke` resolution.
const IMethod* GetDelegateOrExpressionTreeSignature(const IType& t)
{
    // C# `if (t.TypeParameterCount == 1 && t.Name == "Expression"
    //     && t.Namespace == "System.Linq.Expressions") { t = t.TypeArguments[0]; }`
    //
    // The namespace is read via `GetDefinition()->Namespace()` with a null guard (the C#
    // `IType : INamedElement` carries `Namespace`; the port's `IType` does not -- the
    // `UnpackExpressionTreeType` D534 convention). A `ParameterizedType` over the real
    // `Expression`1` generic resolves the definition itself, so the namespace check fires;
    // a definitionless type fails the check and skips the unwrap, faithfully matching the
    // C# where the non-definition's `Namespace` would be empty.
    const IType* current = &t;
    if (current->TypeParameterCount() == 1 && current->Name() == "Expression") {
        const ITypeDefinition* def = current->GetDefinition();
        if (def != nullptr && def->Namespace() == "System.Linq.Expressions") {
            // C# `t = t.TypeArguments[0]` -- the C# `IType.TypeArguments` returns the type
            // parameters for a bare generic definition, but the port's `TypeArguments` is
            // `ParameterizedType`-specific (not on the `IType` interface), so the rebind
            // goes through a `dynamic_cast`. For the only real matching shape --
            // `Expression<T>`, a `ParameterizedType` -- the cast succeeds. A bare
            // `Expression`1` DEFINITION (Kind=Class, not parameterized) cannot carry the
            // unwrap here, but resolves the same result: the C# would rebind to the `T`
            // type parameter (Kind=TypeParameter), and both are non-delegate kinds, so
            // `GetDelegateInvokeMethod` returns null either way.
            if (const ParameterizedType* pt = dynamic_cast<const ParameterizedType*>(current)) {
                // The temporary `ITypePtr` dies but the managed `IType` is owned by the
                // `ParameterizedType`'s `typeArgs_` reachable through the input `t`, so the
                // raw pointer outlives the call (the D516 ownership reasoning).
                if (ITypePtr arg = pt->GetTypeArgument(0))
                    current = arg.get();
            }
        }
    }
    // C# `return t.GetDelegateInvokeMethod();` -- the TypeSystemExtensions free function
    // (D533): the Kind==Delegate guard, then the first `Invoke`-named method (null when the
    // type is not a delegate or has no `Invoke`).
    return GetDelegateInvokeMethod(*current);
}

// The C# `IType[] InputTypes(ResolveResult e, IType t)` (C# spec draft-v11 section 12.6.3.4).
std::vector<const IType*> InputTypes(const ILSpy::Decompiler::Semantics::ResolveResult& e,
                                     const IType& t)
{
    // C# `LambdaResolveResult lrr = e as LambdaResolveResult;`
    const LambdaResolveResult* lrr = dynamic_cast<const LambdaResolveResult*>(&e);
    // C# `if (lrr != null && lrr.IsImplicitlyTyped || e is MethodGroupResolveResult)` -- the
    // operator precedence is `&&` over `||`, so the implicitly-typed-lambda arm and the
    // method-group arm are the two alternatives.
    if ((lrr != nullptr && lrr->IsImplicitlyTyped())
        || dynamic_cast<const MethodGroupResolveResult*>(&e) != nullptr) {
        // C# `IMethod m = GetDelegateOrExpressionTreeSignature(t); if (m != null) { ... }`
        const IMethod* m = GetDelegateOrExpressionTreeSignature(t);
        if (m != nullptr) {
            // C# `IType[] inputTypes = new IType[m.Parameters.Count];
            // for (int i = 0; i < inputTypes.Length; i++)
            //     inputTypes[i] = m.Parameters[i].Type;`
            //
            // The parameter types are owned by the delegate-invoke method (owned by the
            // delegate type reachable through `t`), so the non-owning `const IType*`
            // snapshot outlives the call (the `GetMethods` raw-pointer convention).
            auto params = m->Parameters();
            std::vector<const IType*> inputTypes;
            inputTypes.reserve(params.size());
            for (const IParameter* p : params)
                inputTypes.push_back(&p->Type());
            return inputTypes;
        }
    }
    // C# `return Empty<IType>.Array;` -- the empty snapshot.
    return {};
}

// The C# `IType[] OutputTypes(ResolveResult e, IType t)` (C# spec draft-v11 section 12.6.3.5).
std::vector<const IType*> OutputTypes(const ILSpy::Decompiler::Semantics::ResolveResult& e,
                                      const IType& t)
{
    // C# `LambdaResolveResult lrr = e as LambdaResolveResult; if (lrr != null ||
    // e is MethodGroupResolveResult)` -- ANY lambda (implicitly OR explicitly typed; no
    // `IsImplicitlyTyped` gate here, unlike `InputTypes`) or a method group.
    const LambdaResolveResult* lrr = dynamic_cast<const LambdaResolveResult*>(&e);
    if (lrr != nullptr || dynamic_cast<const MethodGroupResolveResult*>(&e) != nullptr) {
        const IMethod* m = GetDelegateOrExpressionTreeSignature(t);
        if (m != nullptr) {
            // C# `return new[] { m.ReturnType };` -- the one-element snapshot (the return
            // type is owned by the delegate-invoke method, reachable through `t`).
            return { &m->ReturnType() };
        }
    }
    // C# `return Empty<IType>.Array;`
    return {};
}

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
