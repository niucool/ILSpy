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
#include "Decompiler/IL/StackTypeOf.hpp"
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

} // namespace ILSpy::Decompiler::IL
