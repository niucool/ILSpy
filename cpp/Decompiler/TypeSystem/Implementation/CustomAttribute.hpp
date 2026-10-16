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
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/CustomAttribute.cs --
// `sealed class CustomAttribute : IAttribute`, the metadata-row-backed resolved
// attribute. Unlike `DefaultAttribute` (the already-resolved shape), the
// `CustomAttribute` carries the row token and defers the VALUE decode to the
// first `FixedArguments` / `NamedArguments` / `HasDecodeErrors` read, decoding
// through the module-owned `TypeProvider` (the C# `attr.DecodeValue(module.
// TypeProvider)`, the SRM `CustomAttribute.DecodeValue` entry the port's
// `Metadata::CustomAttributeDecoder` implements). The AttributeListBuilder's
// `Add(CustomAttributeHandleCollection, target)` constructs one per surviving
// custom-attribute row, so every entity's `GetAttributes()` list is built from
// these.
//
// KEY PORT CONVENTIONS:
//  (a) OWNERSHIP: the class is shared_ptr-owned (the AttributeListBuilder's
//      `attributes_` vector holds it), matching the C# GC reference the
//      builder's list carries. The `AttributeType` reference return (the
//      `IAttribute` D374 non-null convention) is valid for the attribute's
//      lifetime: it aliases the resolved constructor's `DeclaringType()`
//      handle, which the module's entity caches / keep-alive registries own.
//  (b) The C# lazy `value` / `valueDecoded` / `hasDecodeErrors` fields guarded
//      by `lock (syncRoot)` port to mutable state under a `std::mutex` (the
//      C# lock shape; single-threaded in practice, but the decode may be
//      re-entered through the provider's FindType on the same attribute, and
//      the mutex keeps the double-check faithful).
//  (c) EXCEPTIONS: the decode's `EnumUnderlyingTypeResolveException` and
//      `BadImageFormatException` catch arms (the C# "in case of errors, never
//      try again" reset to an EMPTY value with `hasDecodeErrors = true`) map
//      to the port's `EnumUnderlyingTypeResolveException` and the decoder's
//      `std::invalid_argument` family (the CustomAttributeDecoder convention
//      (d)). The port also catches `std::out_of_range` alongside the
//      invalid_argument family (the winmd raw-surface exception pair the
//      metadata reads can throw) -- a documented belt-and-braces divergence:
//      no real decode path reaches it, but a crafted manifest's row read can.
//  (d) `MemberForNamedArgument` (the internal static the
//      `TypeSystemAstBuilder.ConvertAttribute` named-argument member resolution
//      consumes) ports with the C# `LastOrDefault` semantics: the LAST member
//      of the `GetFields` / `GetProperties` enumeration whose name matches
//      (the lazy C# enumeration is order-stable over the metadata rows).

#pragma once

#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"

#include <cstdint>
#include <mutex>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations (the .cpp includes the full headers).
class MetadataModule;

namespace Implementation {

class CustomAttribute : public IAttribute {
public:
    // The C# `internal CustomAttribute(MetadataModule module, IMethod
    // attrCtor, CustomAttributeHandle handle)`. The `Debug.Assert`s are
    // compiled out of the shipped release assembly (the release-form
    // convention), so the port takes the members at face value.
    CustomAttribute(const MetadataModule& module, const IMethod* attrCtor,
                    std::uint32_t handle);

    // The C# `public IType AttributeType => Constructor.DeclaringType` --
    // non-null (the `IAttribute` contract; the resolved constructor's
    // declaring type, which the module's caches own).
    const IType& AttributeType() const override;

    // The C# `public IMethod Constructor { get; }` -- the resolved attribute
    // constructor (non-null; the module's resolve-method registries own it).
    const IMethod* Constructor() const override { return constructor_; }

    // The C# `bool HasDecodeErrors` -- decodes on first read.
    bool HasDecodeErrors() const override;

    // The C# `ImmutableArray<CustomAttributeTypedArgument<IType>>
    // FixedArguments` -- decodes on first read, by value (the D385 snapshot).
    std::vector<CustomAttributeTypedArgument> FixedArguments()
        const override;

    // The C# `ImmutableArray<CustomAttributeNamedArgument<IType>>
    // NamedArguments` -- decodes on first read, by value (the D385 snapshot).
    std::vector<CustomAttributeNamedArgument> NamedArguments()
        const override;

    // The C# `internal static IMember MemberForNamedArgument(IType
    // attributeType, CustomAttributeNamedArgument<IType> namedArgument)` --
    // the field/property the named argument assigns (the LAST name match,
    // convention (d)); null for a kind that is neither Field nor Property.
    static const IMember* MemberForNamedArgument(
        const IType& attributeType,
        const CustomAttributeNamedArgument& namedArgument);

private:
    // The C# `void DecodeValue()` -- the lazy, locked, never-retry-on-error
    // decode over the row's value blob through the module's TypeProvider.
    void DecodeValue() const;

    const MetadataModule& module_;
    std::uint32_t handle_;  // the raw 0x0C...... row token
    const IMethod* constructor_;

    // The lazy decode state (convention (b)); the C# `CustomAttributeValue`
    // is stored decoded and the flags guard the re-entry.
    mutable std::vector<CustomAttributeTypedArgument> fixedArguments_;
    mutable std::vector<CustomAttributeNamedArgument> namedArguments_;
    mutable bool valueDecoded_ = false;
    mutable bool hasDecodeErrors_ = false;
    mutable std::mutex syncRoot_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation

} // namespace ILSpy::Decompiler::TypeSystem
