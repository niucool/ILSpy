// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
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

// Port of the `CSharpSlotInfo` / `CSharpSlotInfo<T>` slot system in
// ICSharpCode.Decompiler/CSharp/Syntax/CSharpSlotInfo.cs. A slot describes one
// child position of a node type: its name, its declared child type, whether it is
// a collection, the canonical `Slots` kind it points at, and whether it may be
// empty. The generator emits one typed static per slot
// (e.g. `BinaryOperatorExpression.LeftSlot`) and one typed constant per distinct
// kind in the `Slots` holder; `node.Slot` is compared against a node's slot static
// by object identity, and `node.Slot.Kind` against a `Slots` constant, so a node's
// position is identified by a pointer comparison.
//
// The C# `ChildType` is a `System.Type` and the is-a test is
// `ChildType.IsInstanceOfType(child)`. C++ has no `System.Type`: the declared child
// type is captured at the generic `CSharpSlotInfoT<T>` ctor as a type-erased
// `bool(*)(const AstNode*)` predicate that does `dynamic_cast<const T*>`, so the
// is-a check is faithful to the CLR `IsInstanceOfType` (it accepts `T` and any
// subtype of `T`). That requires the `AstNode` hierarchy to be polymorphic, which
// is why `CSharpSlotInfo` lands alongside the (start of the) `AstNode` base: the
// generic slot is only instantiated for a concrete node type once `AstNode` is a
// complete polymorphic class.
//
// The C# generic `CSharpSlotInfo<T>` cannot reuse the base name in C++ (a class
// template may not share a name with a non-template class), so the generic is
// `CSharpSlotInfoT<T>` (the `T` suffix evokes the `<T>` type parameter); it derives
// from the non-template `CSharpSlotInfo` base, matching the C# inheritance.
//
// Slot statics are singletons compared by address, so a node-type header must
// define each slot static as a C++17 `inline` variable (one address across
// translation units); a plain `static` would give each translation unit its own
// copy and break the pointer-identity comparison.

#pragma once

#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// Forward declaration: the slot only needs `AstNode` as the parameter type of the
// type-erased is-a predicate. The generic `CSharpSlotInfoT<T>` instantiates
// `dynamic_cast<const T*>` at the use site, where both `AstNode` and `T` are
// complete (the concrete-node header includes the full `AstNode` definition).
class AstNode;

// Describes one child slot of a node type. Compare by address: each slot is a
// single static instance, and `node.Slot == &SomeNode::XSlot` identifies a node's
// position by object identity (the C# `==` on the reference-type `CSharpSlotInfo`).
class CSharpSlotInfo {
    std::string name_;
    bool isCollection_;
    const CSharpSlotInfo* kind_;
    bool isOptional_;
    // The type-erased `ChildType.IsInstanceOfType`: returns whether `node` is an
    // instance of the slot's declared child type (or a subtype). Captured by the
    // generic `CSharpSlotInfoT<T>` ctor as a `dynamic_cast<const T*>` test.
    bool (*isInstance_)(const AstNode*);

public:
    // Mirrors the C# `internal CSharpSlotInfo(string, Type, bool, CSharpSlotInfo?,
    // bool)` ctor, with the `Type` replaced by the `isInstance` predicate.
    CSharpSlotInfo(std::string name, bool isCollection, const CSharpSlotInfo* kind,
                   bool isOptional, bool (*isInstance)(const AstNode*))
        : name_(std::move(name)), isCollection_(isCollection), kind_(kind),
          isOptional_(isOptional), isInstance_(isInstance) {}

    // The C# `string Name`.
    std::string_view Name() const { return name_; }

    // The C# `bool IsCollection`.
    bool IsCollection() const { return isCollection_; }

    // The C# `CSharpSlotInfo? Kind`: the canonical shared slot for this position,
    // or null for a `Slots` constant (which is itself the kind).
    const CSharpSlotInfo* Kind() const { return kind_; }

    // The C# `bool IsOptional`.
    bool IsOptional() const { return isOptional_; }

    // The C# `Type.IsInstanceOfType(object)`. A null node is not an instance of any
    // slot type (matching the C# `IsInstanceOfType(null) == false`); the slot's
    // declared child type test is the `dynamic_cast` captured at construction.
    bool IsInstanceOfType(const AstNode* node) const {
        return node != nullptr && isInstance_(node);
    }

    // The C# `override string ToString() => Name`.
    std::string ToString() const { return name_; }
};

// A `CSharpSlotInfo` that carries its child type as a type parameter, so the typed
// child accessors (`node.GetChild(SomeNode.XSlot)`) infer the result type from the
// slot. `T` is the child type (the element type for a collection slot) and must
// derive from `AstNode`. Mirrors the C# `CSharpSlotInfo<T> : CSharpSlotInfo`.
template<class T>
class CSharpSlotInfoT : public CSharpSlotInfo {
public:
    CSharpSlotInfoT(std::string name, bool isCollection, const CSharpSlotInfo* kind, bool isOptional)
        : CSharpSlotInfo(std::move(name), isCollection, kind, isOptional,
            // The C# `typeof(T)` / `IsInstanceOfType`: a captureless lambda decays to
            // the `bool(*)(const AstNode*)` predicate stored by the base.
            [](const AstNode* node) {
                return dynamic_cast<const T*>(node) != nullptr;
            }) {}
};

} // namespace ILSpy::Decompiler::CSharp::Syntax
