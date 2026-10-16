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

// Field-address and typed memory load/store nodes, faithful to the generated
// Instructions.cs. ldfld is LdObj(LdFlda(target, field), field.Type); stfld is
// StObj(LdFlda(target, field), value, field.Type); ldsfld/stsfld use LdsFlda.
// LdFlda.DelayExceptions defers the NullReferenceException to the enclosing
// LdObj/StObj (the C# sets it true for ldfld/stfld).

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/SimpleInstruction.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

// ldflda: &target.field. One Target child (inlineable). Result is I (unmanaged
// pointer) if the target is an integer type, else Ref.
//
// IsCompilerGeneratedField is set by the IL reader from the field token's
// [CompilerGenerated] custom attribute (or its declaring type's), mirroring
// the C# IField.IsCompilerGeneratedOrIsInCompilerGeneratedClass. It is the
// gate the cached-delegate / cached-ReadOnlySpan transforms consult to
// recognise a compiler-synthesized cache field (e.g. a
// <PrivateImplementationDetails> static or a display-class field); this port's
// minimal type system does not carry per-field IField metadata, so the check
// is resolved once at read time and stored on the node.
class LdFlda : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Target;
    std::string FieldName;  // "Namespace.Type::Field" (resolved by the IL reader)
    // The raw metadata token (table 0x04 FieldDef or 0x0A field MemberRef) the
    // reader resolved FieldName from. Carried so transforms that need richer
    // field metadata (the field's type, attributes, ...) can resolve it via
    // the MetadataFile without re-parsing the name.
    std::uint32_t FieldToken = 0;
    bool IsCompilerGeneratedField = false;
    // The C# `IField.IsReadOnly` (the FieldAttributes.InitOnly bit, resolved at
    // read time): a store through a readonly field's address is not a mutable
    // lvalue, so ILInlining.ClassifyExpression reports ReadonlyLValue for it.
    bool FieldIsReadOnly = false;
    // The C# `public readonly IField Field` -- the resolved field identity the
    // field-reference arms read. Populated by tests/transforms (the `Call::Method`
    // convention: the port's IL reader only records the raw token/name/deferred
    // metadata, so the fully resolved IField is set outside the reader). Null when
    // unresolved, in which case no visit arm consumes it.
    std::shared_ptr<TypeSystem::IField> Field;
    bool DelayExceptions = false;
    LdFlda(std::unique_ptr<ILInstruction> target, std::string field)
        : ILInstruction(OpCode::LdFlda), Target(std::move(target)), FieldName(std::move(field)) {
        if (Target) { Target->Parent = this; Target->ChildIndex = 0; }
    }
    InstructionFlags DirectFlags() const override {
        return DelayExceptions ? InstructionFlags::None : InstructionFlags::MayThrow;
    }
    StackType ResultType() const override {
        // target.ResultType.IsIntegerType() ? I : Ref. A managed ref target -> Ref;
        // an unmanaged pointer target -> I. For the foundation, treat Ref/Unknown
        // targets as Ref and I targets as I.
        if (Target && Target->ResultType() == StackType::I) return StackType::I;
        return StackType::Ref;
    }
    int ChildCount() const override { return Target ? 1 : 0; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? Target.get() : nullptr; }
    void WriteTo(std::string& out) const override {
        out += "ldflda(";
        out += FieldName;
        out += ", ";
        if (Target) Target->WriteTo(out); else out += "(null)";
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Target);
        Target = std::move(n);
        return old;
    }
};

// ldsflda: &field (static). SimpleInstruction (no target). Result Ref.
// IsCompilerGeneratedField is set by the IL reader; see LdFlda for the detail.
class LdsFlda : public SimpleInstruction {
public:
    std::string FieldName;
    std::uint32_t FieldToken = 0;  // see LdFlda::FieldToken
    bool IsCompilerGeneratedField = false;
    bool FieldIsReadOnly = false;  // see LdFlda::FieldIsReadOnly
    std::shared_ptr<TypeSystem::IField> Field;  // see LdFlda::Field
    explicit LdsFlda(std::string field) : SimpleInstruction(OpCode::LdsFlda), FieldName(std::move(field)) {}
    StackType ResultType() const override { return StackType::Ref; }
    void WriteTo(std::string& out) const override {
        out += "ldsflda("; out += FieldName; out += ')';
    }
};

// ldobj: typed load from an address. One Target child (inlineable).
// DirectFlags = SideEffect | MayThrow; result = StackTypeOf(Type).
class LdObj : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Target;
    TypeSystem::ITypePtr Type;
    // The C# `LdObj : ILInstruction, ISupportsVolatilePrefix, ISupportsUnalignedPrefix`
    // operands; rendered BEFORE the opcode (the Initblk/Cpblk convention).
    bool IsVolatile = false;
    std::uint8_t UnalignedPrefix = 0;
    LdObj(std::unique_ptr<ILInstruction> target, TypeSystem::ITypePtr type)
        : ILInstruction(OpCode::LdObj), Target(std::move(target)), Type(std::move(type)) {
        if (Target) { Target->Parent = this; Target->ChildIndex = 0; }
    }
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::SideEffect | InstructionFlags::MayThrow;
    }
    StackType ResultType() const override { return StackTypeOf(Type); }
    int ChildCount() const override { return Target ? 1 : 0; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? Target.get() : nullptr; }
    void WriteTo(std::string& out) const override {
        if (IsVolatile) out += "volatile.";
        if (UnalignedPrefix != 0) {
            out += "unaligned(";
            out += std::to_string(UnalignedPrefix);
            out += ").";
        }
        out += "ldobj(";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ", ";
        if (Target) Target->WriteTo(out); else out += "(null)";
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Target);
        Target = std::move(n);
        return old;
    }
};

// stobj: typed store to an address. Target + Value children (both inlineable).
// DirectFlags = SideEffect | MayThrow; result Void.
class StObj : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Target;
    std::unique_ptr<ILInstruction> Value;
    TypeSystem::ITypePtr Type;
    // The C# `StObj : ILInstruction, ISupportsVolatilePrefix, ISupportsUnalignedPrefix`
    // operands; rendered BEFORE the opcode (the Initblk/Cpblk convention).
    bool IsVolatile = false;
    std::uint8_t UnalignedPrefix = 0;
    StObj(std::unique_ptr<ILInstruction> target, std::unique_ptr<ILInstruction> value,
         TypeSystem::ITypePtr type)
        : ILInstruction(OpCode::StObj), Target(std::move(target)), Value(std::move(value)),
          Type(std::move(type)) {
        if (Target) { Target->Parent = this; Target->ChildIndex = 0; }
        if (Value) { Value->Parent = this; Value->ChildIndex = 1; }
    }
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::SideEffect | InstructionFlags::MayThrow;
    }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override { return (Target ? 1 : 0) + (Value ? 1 : 0); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Target.get();
        if (i == 1) return Value.get();
        return nullptr;
    }
    void WriteTo(std::string& out) const override {
        if (IsVolatile) out += "volatile.";
        if (UnalignedPrefix != 0) {
            out += "unaligned(";
            out += std::to_string(UnalignedPrefix);
            out += ").";
        }
        out += "stobj(";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += ", ";
        if (Target) Target->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Value) Value->WriteTo(out); else out += "(null)";
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0 || i == 1);
        if (i == 0) { auto old = std::move(Target); Target = std::move(n); return old; }
        auto old = std::move(Value); Value = std::move(n); return old;
    }
};

// localloc: allocate the argument's many bytes on the stack. One Argument child
// (the byte count, inlineable). Result I (an unmanaged pointer);
// DirectFlags = MayThrow (the C# LocAlloc.ComputeFlags). Port of the C#
// `LocAlloc : UnaryInstruction` (Instructions.cs line 3340).
//
// This port's IL reader still decodes the raw `localloc` opcode as an LdNull
// placeholder (the seed --csharp render's convention); tests drive this node by
// hand and the future reader/seed reconcile slice wires it into the pipeline.
class LocAlloc : public UnaryInstruction {
public:
    explicit LocAlloc(std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::LocAlloc, std::move(argument)) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::MayThrow; }
    StackType ResultType() const override { return StackType::I; }
    void WriteTo(std::string& out) const override {
        out += "localloc(";
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// locallocspan (the ExpressionTransforms localloc + newobj Span<T> fold): allocate
// a block of the argument's many bytes on the stack wrapped in the type operand
// (Span<T> or ReadOnlySpan<T>). One Argument child (the count, inlineable) plus the
// non-child Type field. Result O; DirectFlags = MayThrow (the C#
// LocAllocSpan.ComputeFlags). Port of the C# `LocAllocSpan : UnaryInstruction`
// (Instructions.cs line 3371); the Type operand is the C# `IType type` field.
// The same reader placeholder note as LocAlloc applies.
class LocAllocSpan : public UnaryInstruction {
public:
    TypeSystem::ITypePtr Type;
    LocAllocSpan(std::unique_ptr<ILInstruction> argument, TypeSystem::ITypePtr type)
        : UnaryInstruction(OpCode::LocAllocSpan, std::move(argument)), Type(std::move(type)) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::MayThrow; }
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override {
        out += "locallocspan ";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// ckfinite: check that the top-of-stack float is neither NaN nor infinite.
// UnaryInstruction with Void result; DirectFlags = base(None) | MayThrow. The C#
// ILReader PEEKS the argument (the check runs in place -- the value stays on the
// stack), so this port's seed reader models the opcode as a no-op wrapper
// (ILReader.cpp); tests/transforms drive this node by hand the same way they
// drive LocAlloc. Port of the C# `Ckfinite : UnaryInstruction` (Instructions.cs
// line 2232).
class Ckfinite : public UnaryInstruction {
public:
    explicit Ckfinite(std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::Ckfinite, std::move(argument)) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::MayThrow; }
    StackType ResultType() const override { return StackType::Void; }
    void WriteTo(std::string& out) const override {
        out += "ckfinite(";
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// The two prefix-carrying block memory instructions. The C# `WriteToCore` renders
// the ISupportsVolatilePrefix `volatile.` and ISupportsUnalignedPrefix
// `unaligned(<n>).` prefixes BEFORE the opcode (InstructionOutputExtensions' opcode
// names are the lowercase IL mnemonics); the port's seed IL reader skips prefixes
// (see ILReader.cpp), so tests set the fields directly.
//
// initblk: memset(address, value, size). Three inlineable children in slot order
// Address(0)/Value(1)/Size(2); Void result; DirectFlags = MayThrow | SideEffect.
// Port of the C# `Initblk : ILInstruction, ISupportsVolatilePrefix,
// ISupportsUnalignedPrefix` (Instructions.cs line 3582).
class Initblk : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Address;
    std::unique_ptr<ILInstruction> Value;
    std::unique_ptr<ILInstruction> Size;
    // The C# `ISupportsUnalignedPrefix.UnalignedPrefix` operand (0 = no prefix) and
    // `ISupportsVolatilePrefix.IsVolatile`.
    std::uint8_t UnalignedPrefix = 0;
    bool IsVolatile = false;
    Initblk(std::unique_ptr<ILInstruction> address, std::unique_ptr<ILInstruction> value,
            std::unique_ptr<ILInstruction> size)
        : ILInstruction(OpCode::Initblk), Address(std::move(address)), Value(std::move(value)),
          Size(std::move(size)) {
        if (Address) { Address->Parent = this; Address->ChildIndex = 0; }
        if (Value) { Value->Parent = this; Value->ChildIndex = 1; }
        if (Size) { Size->Parent = this; Size->ChildIndex = 2; }
    }
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayThrow | InstructionFlags::SideEffect;
    }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override {
        return (Address ? 1 : 0) + (Value ? 1 : 0) + (Size ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Address.get();
        if (i == 1) return Value.get();
        if (i == 2) return Size.get();
        return nullptr;
    }
    void WriteTo(std::string& out) const override {
        if (IsVolatile) out += "volatile.";
        if (UnalignedPrefix != 0) {
            out += "unaligned(";
            out += std::to_string(UnalignedPrefix);
            out += ").";
        }
        out += "initblk(";
        if (Address) Address->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Value) Value->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Size) Size->WriteTo(out); else out += "(null)";
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i >= 0 && i <= 2);
        auto& slot = i == 0 ? Address : (i == 1 ? Value : Size);
        auto old = std::move(slot);
        slot = std::move(n);
        return old;
    }
};

// cpblk: memcpy(destAddress, sourceAddress, size). Three inlineable children in
// slot order DestAddress(0)/SourceAddress(1)/Size(2); Void result; the same
// flags as Initblk. Port of the C# `Cpblk : ILInstruction, ISupportsVolatilePrefix,
// ISupportsUnalignedPrefix` (Instructions.cs line 3431).
class Cpblk : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> DestAddress;
    std::unique_ptr<ILInstruction> SourceAddress;
    std::unique_ptr<ILInstruction> Size;
    std::uint8_t UnalignedPrefix = 0;
    bool IsVolatile = false;
    Cpblk(std::unique_ptr<ILInstruction> destAddress,
          std::unique_ptr<ILInstruction> sourceAddress, std::unique_ptr<ILInstruction> size)
        : ILInstruction(OpCode::Cpblk), DestAddress(std::move(destAddress)),
          SourceAddress(std::move(sourceAddress)), Size(std::move(size)) {
        if (DestAddress) { DestAddress->Parent = this; DestAddress->ChildIndex = 0; }
        if (SourceAddress) { SourceAddress->Parent = this; SourceAddress->ChildIndex = 1; }
        if (Size) { Size->Parent = this; Size->ChildIndex = 2; }
    }
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayThrow | InstructionFlags::SideEffect;
    }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override {
        return (DestAddress ? 1 : 0) + (SourceAddress ? 1 : 0) + (Size ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return DestAddress.get();
        if (i == 1) return SourceAddress.get();
        if (i == 2) return Size.get();
        return nullptr;
    }
    void WriteTo(std::string& out) const override {
        if (IsVolatile) out += "volatile.";
        if (UnalignedPrefix != 0) {
            out += "unaligned(";
            out += std::to_string(UnalignedPrefix);
            out += ").";
        }
        out += "cpblk(";
        if (DestAddress) DestAddress->WriteTo(out); else out += "(null)";
        out += ", ";
        if (SourceAddress) SourceAddress->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Size) Size->WriteTo(out); else out += "(null)";
        out += ')';
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i >= 0 && i <= 2);
        auto& slot = i == 0 ? DestAddress : (i == 1 ? SourceAddress : Size);
        auto old = std::move(slot);
        slot = std::move(n);
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
