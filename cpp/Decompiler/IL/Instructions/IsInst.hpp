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

// IsInst: isinst <T> -- test whether the value is a T; pushes T or null. Throws
// only on a null target via NullReferenceException, so DirectFlags stays None
// (matches the C#). UnaryInstruction; result O.

#pragma once

#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class IsInst : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    IsInst(TypeSystem::ITypePtr type, std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::IsInst, std::move(argument)), Type(std::move(type)) {}
    StackType ResultType() const override { return StackType::O; }

    // The C# `internal override bool CanInlineIntoSlot(int childIndex,
    // ILInstruction newChild)` (IsInst.cs line 28): the `expr is T` emulation
    // gates -- a reference-type target always supports the duplicate evaluation;
    // a pure expression is emulated via `expr is T ? (T)expr : null`; the Box arm
    // and the UnboxAny/Comp-null/ControlFlow-block parent shapes follow the C#.
    bool CanInlineIntoSlot(int childIndex, ILInstruction* newChild) override;
    void WriteTo(std::string& out) const override {
        out += "isinst(";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ", ";
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// The out-of-line IsInst::CanInlineIntoSlot (the C# IsInst.cs line 28 body): the
// `expr is T` emulation gates. The UnboxAny arm replicates
// `ExpressionBuilder.IsUnboxAnyWithIsInst` (the C# IsInst.cs reaches into the CSharp
// layer for it; the port avoids the IL->CSharp include with the same two-line test),
// and the Comp-null arms replicate the MatchCompEqualsNull/MatchCompNotEqualsNull
// probes (the C# ILAst Match* extensions).
inline bool IsInst::CanInlineIntoSlot(int childIndex, ILInstruction* newChild) {
    (void)childIndex;
    // The C# `Debug.Assert(base.CanInlineIntoSlot(...))`.
    if (Type != nullptr && Type->IsReferenceType() == true) {
        return true;  // reference-type isinst is always supported
    }
    if (newChild != nullptr && IsPure(newChild->Flags())) {
        return true;  // emulated via "expr is T ? (T)expr : null"
    }
    if (auto* box = dynamic_cast<Box*>(newChild)) {
        if (box->Argument && IsPure(box->Argument->Flags())
            && (!Argument || Argument->ChildCount() == 0)) {
            // Also emulated via "expr is T ? (T)expr : null". The boxing
            // side-effect duplication is harmless as one of the boxes is only
            // used in the `expr is T` type test where the object identity can
            // never be observed. The C# restricts to a Box appearing directly
            // top-level in the IsInst (the previous argument has no children).
            return true;
        }
    }
    if (Parent != nullptr) {
        // The supported "expr as T?" pattern:
        // `unboxAny.Type.Equals(isInstType) && (unboxAny.Type.IsKnownType(
        // NullableOfT) || isInstType.IsReferenceType == true)`.
        if (auto* unboxAny = dynamic_cast<UnboxAny*>(Parent)) {
            if (unboxAny->Type && Type && unboxAny->Type->Equals(*Type)) {
                bool nullableOfT = false;
                for (const TypeSystem::IType* baseType
                     : TypeSystem::GetAllBaseTypes(unboxAny->Type.get())) {
                    if (baseType != nullptr
                        && TypeSystem::IsKnownType(
                            *baseType, TypeSystem::KnownTypeCode::NullableOfT)) {
                        nullableOfT = true;
                        break;
                    }
                }
                if (nullableOfT
                    || (Type && Type->IsReferenceType() == true))
                    return true;
            }
        }
        // The supported "expr is T" pattern: `comp(Equality, X, ldnull)` either
        // side (the port's Comp carries ComparisonKind, so the probe is the
        // MatchCompEqualsNull/MatchCompNotEqualsNull pair over the Comp node).
        if (auto* comp = dynamic_cast<Comp*>(Parent)) {
            if (comp->Kind == ComparisonKind::Equality) {
                const bool leftNull = comp->Left != nullptr
                    && comp->Left->Op == OpCode::LdNull;
                const bool rightNull = comp->Right != nullptr
                    && comp->Right->Op == OpCode::LdNull;
                if (leftNull || rightNull)
                    return true;
            }
        }
        if (dynamic_cast<Block*>(Parent) != nullptr)
            return true;  // supported via StatementBuilder.VisitIsInst
    }
    return false;
}

} // namespace ILSpy::Decompiler::IL
