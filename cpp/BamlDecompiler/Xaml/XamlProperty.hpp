// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlProperty.cs (Ki, 2015, MIT):
// one resolved property of the XAML translation -- the declaring `XamlType`,
// the property name, the resolved `IMember` (null until `TryResolve` runs),
// the attached-property walk, and the `XName` render.
//
// C#-to-C++ porting decisions:
//  * `XamlType DeclaringType { get; }` ports to a non-owning
//    `const XamlType*` (the `XamlContext` type map owns the instances it
//    hands out; a test owns its own).
//  * `IMember ResolvedMember { get; set; }` (nullable) ports to a nullable
//    non-owning `const IMember*` -- the `GetProperties`/`GetFields`/
//    `GetEvents` member enumerations return non-owning pointers into the
//    declaring type's member tables (the `KnownMember::Property` precedent).
//  * The `typeDef.GetProperties(...)` filter lambdas port to
//    `std::function` predicates over the same member-enumeration virtuals
//    (the default `GetMemberOptions::None` the C# leaves implicit).
//  * The C# `t.FullName == declType.FullName && t.TypeParameterCount ==
//    declType.TypeParameterCount` walk reads `IMember.DeclaringType` -- a
//    null there is the C# NRE; the port maps it to `std::runtime_error`
//    with the standard message (the XmlnsDictionary NRE convention).

#pragma once

#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <string>

namespace ILSpy::BamlDecompiler {

// The BAML decompiler's C# `using System.Xml.Linq` -- an alias to the port's
// stand-in namespace (the BamlDecompiler tree is not nested in
// ILSpy::Decompiler, so its Xml references need the alias).
namespace Xml = ::ILSpy::Decompiler::Xml;

} // namespace ILSpy::BamlDecompiler

namespace ILSpy::BamlDecompiler::Xaml {

class XamlProperty {
public:
    // The C# ctor.
    XamlProperty(const XamlType* type, std::string name)
        : DeclaringType(type), PropertyName(std::move(name))
    {
    }

    // The C# `void TryResolve()` -- resolves the member from the declaring
    // type's definition: the instance property, then the
    // `<name>Property` field, then the event, then the `<name>Event` field.
    // Defined in XamlContext.cpp (the member-enumeration composition).
    void TryResolve();

    // The C# `bool IsAttachedTo(XamlType type)` -- false when the target
    // type's base chain reaches the resolved member's declaring type (the
    // property is an instance property there), true otherwise (attached).
    // Defined in XamlContext.cpp.
    bool IsAttachedTo(const XamlType* type) const;

    // The C# `XName ToXName(XamlContext ctx, XElement parent, bool
    // isFullName = true)`. Defined in XamlContext.cpp.
    Xml::XName ToXName(XamlContext& ctx, const Xml::XElement* parent,
                       bool isFullName = true) const;

    // The C# `override string ToString() => PropertyName`.
    std::string ToString() const
    {
        return PropertyName;
    }

    // The C# `XamlType DeclaringType { get; }` (non-owning -- see the header
    // porting decisions).
    const XamlType* DeclaringType;

    // The C# `string PropertyName { get; }`.
    std::string PropertyName;

    // The C# `IMember ResolvedMember { get; set; }` (nullable, non-owning).
    const ILSpy::Decompiler::TypeSystem::IMember* ResolvedMember = nullptr;
};

} // namespace ILSpy::BamlDecompiler::Xaml
