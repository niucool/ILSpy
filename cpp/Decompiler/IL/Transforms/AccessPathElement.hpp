// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
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

// Port of the `AccessPathKind` enum + `AccessPathElement` struct from
// ICSharpCode.Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.cs
// (lines 325-608): the shared static machinery that decomposes a store (or `Add`
// call) instruction into the member-access path it writes through
// (`obj.Field.Property = value` -> the [Field, Property] path with the value and
// the root target variable). Both the IL-side
// TransformCollectionAndObjectInitializers transform and the C#-side
// ExpressionBuilder.TranslateObjectAndCollectionInitializer /
// TranslateWithInitializer (the remaining VisitBlock arms) consume it, so it
// lives in its own translation unit like the C#'s public struct.
//
// The C# `GetAccessPath` takes an optional `CSharpResolver` (the C# layer's
// resolver); the port forward-declares it here and pulls the CSharp-layer
// headers only in the .cpp (the resolver's applicability checks run only when
// a resolver is passed, mirroring the C# `resolver != null` gates).

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {
class CSharpResolver;
}

namespace ILSpy::Decompiler {
class DecompilerSettings;
}

namespace ILSpy::Decompiler::IL {

class ILInstruction;
class ILVariable;

// The C# `public enum AccessPathKind` (line 325) -- what the outermost
// instruction of the path does: a store (setter) or an `Add` call (adder).
enum class AccessPathKind {
    Invalid,
    Setter,
    Adder,
};

// The C# `public struct AccessPathElement : IEquatable<AccessPathElement>`
// (lines 332-586). One member access in the path: the (OpCode, Member) pair
// plus the indexer argument instructions for parameterized members.
//
// PORT CONVENTIONS:
//  * The C# `readonly IMember Member` reference ports to a NON-OWNING raw
//    pointer (the owning shared_ptr lives on the IL node the member was read
//    from -- `Call::Method` / `LdFlda::Field` -- or in the caller's fixture;
//    the C# GC roots it the same way through the instruction).
//  * The C# `readonly ILInstruction[]? Indices` ports to
//    `std::optional<std::vector<ILInstruction*>>`: the C# NULL (the
//    non-accessor and field arms) is `nullopt`, while an accessor call always
//    builds an ARRAY -- EMPTY for a plain property (`ToArray()` of an empty
//    `Take` is an empty array, not null). The null-vs-empty distinction is
//    observable through `Equals` (the C# reference-compares the arrays first)
//    and the `Indices?.Length > 0` gates, so `optional` is required. The
//    element pointers are non-owning (the call's `Arguments` slots keep the
//    instructions).
struct AccessPathElement {
    // The C# `public readonly OpCode OpCode` -- the opcode of the instruction
    // the member was accessed through (Call for accessor/Add calls, StObj/LdObj/
    // LdObjIfRef/LdFlda for the field arms). The C# field shares the type's name;
    // a C++ member named `OpCode` would shadow the `OpCode` TYPE in the struct
    // scope (the self-named-member trap), so the port renames it. The port's
    // one-Call-node convention maps the C# Call/CallVirt pair to `OpCode::Call`
    // (newobj is excluded by the walk before any element is built).
    OpCode ElementOpCode = OpCode::Nop;
    // The C# `public readonly IMember Member` -- the accessed member (an
    // IMethod for `Add`, an IProperty for accessor calls, an IField for the
    // field arms). Non-owning; nullable in the port only for a default-
    // constructed element (every walk-built element carries a resolved member).
    const TypeSystem::IMember* Member = nullptr;
    // The C# `public readonly ILInstruction[]? Indices`.
    std::optional<std::vector<ILInstruction*>> Indices;

    // The C# `public AccessPathElement(OpCode opCode, IMember member,
    // ILInstruction[]? indices = null)`.
    AccessPathElement() = default;
    AccessPathElement(OpCode opCode, const TypeSystem::IMember* member,
                      std::optional<std::vector<ILInstruction*>> indices = std::nullopt)
        : ElementOpCode(opCode), Member(member), Indices(std::move(indices)) {}

    // The C# `public override string ToString() => $"[{Member}, {Indices}]"`.
    // The C# interpolated `Indices` (an ILInstruction[]) renders the ARRAY's
    // ToString (the type name), not the elements -- a quirk reproduced
    // verbatim; `Member` renders the member's ToString (the FullName form).
    std::string ToString() const;

    // The C# 5-tuple result of `GetAccessPath`:
    // `(AccessPathKind Kind, List<AccessPathElement> Path,
    //    List<ILInstruction>? Values, ILVariable? Target,
    //    List<ILVariable> UsedIndexVariables)`.
    // `Values` is null until the walk decides the kind (the C# `List?` maps
    // to `optional`); the instruction pointers are non-owning.
    struct Info {
        AccessPathKind Kind = AccessPathKind::Invalid;
        std::vector<AccessPathElement> Path;
        std::optional<std::vector<ILInstruction*>> Values;
        ILVariable* Target = nullptr;
        std::vector<ILVariable*> UsedIndexVariables;
    };

    // The C# `public static (...) GetAccessPath(ILInstruction instruction,
    // IType rootType, DecompilerSettings? settings = null,
    // CSharpResolver? resolver = null)` (lines 352-439): walks the
    // store/call/receiver chain collecting the path elements. `rootType` is
    // the type of the object being initialized (the fallback for the Add
    // applicability check's receiver-type inference); the port takes it as a
    // nullable non-owning pointer.
    static Info GetAccessPath(ILInstruction* instruction,
                              const TypeSystem::IType* rootType,
                              const DecompilerSettings* settings = nullptr,
                              CSharp::Resolver::CSharpResolver* resolver = nullptr);

    // The C# `public bool Equals(AccessPathElement other)` (lines 570-576):
    // MEMBER reference equality (the C# `this.Member.Equals(other.Member)`
    // single-argument call binds `object.Equals` -- reference equality, the
    // FakeMember two-arg-Equals precedent) plus the Indices comparison --
    // reference equality first, then per-element `ILInstructionMatchComparer`
    // equality. Null-vs-empty Indices pairs are UNEQUAL (the C# `==` on the
    // arrays is reference equality and the `SequenceEqual` arm requires both
    // non-null).
    bool Equals(const AccessPathElement& other) const;
    bool operator==(const AccessPathElement& other) const { return Equals(other); }
    bool operator!=(const AccessPathElement& other) const { return !Equals(other); }

    // The C# `public override int GetHashCode()` -- `1000000007 *
    // Member.GetHashCode()` over the (never-null in practice) member's runtime
    // identity hash.
    std::size_t GetHashCode() const;
};

} // namespace ILSpy::Decompiler::IL
