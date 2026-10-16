// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlExtension.cs (see
// XamlExtension.hpp for the porting decisions).

#include "BamlDecompiler/Xaml/XamlExtension.hpp"

#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/Xaml/XamlUtils.hpp"
#include "BamlDecompiler/XamlContext.hpp"

#include <stdexcept>

namespace ILSpy::BamlDecompiler::Xaml {

XamlExtension::XamlExtension(XamlType* type)
    : ExtensionType(type)
{
}

void XamlExtension::SetNamedArgument(std::string key, XamlObject value)
{
    // The C# Dictionary indexer: an existing key's value is replaced in
    // place (the iteration order keeps the key's original position).
    for (auto& entry : NamedArguments) {
        if (entry.first == key) {
            entry.second = std::move(value);
            return;
        }
    }
    NamedArguments.emplace_back(std::move(key), std::move(value));
}

void XamlExtension::WriteObject(std::string& sb, XamlContext& ctx,
    Xml::XElement& ctxElement, const XamlObject& value)
{
    if (const auto* extension = std::get_if<std::shared_ptr<XamlExtension>>(&value))
        sb += (*extension)->ToString(ctx, ctxElement);
    else if (const auto* text = std::get_if<std::string>(&value))
        sb += *text;
    else
        // The C# null value's `value.ToString()` NullReferenceException.
        throw std::runtime_error(
            "Object reference not set to an instance of an object.");
}

std::string XamlExtension::ToString(XamlContext& ctx, Xml::XElement& ctxElement)
{
    std::string sb;
    sb += '{';

    const std::string typeName = Xaml::ToString(ctx, ctxElement, *ExtensionType);
    // The C# `typeName.EndsWith("Extension")` -- the 9-char suffix (an
    // exactly-"Extension" name strips to the empty string).
    if (typeName.size() >= 9
        && typeName.compare(typeName.size() - 9, 9, "Extension") == 0)
        sb.append(typeName, 0, typeName.size() - 9);
    else
        sb += typeName;

    bool comma = false;
    if (Initializer.has_value() && !Initializer->empty()) {
        sb += ' ';
        for (std::size_t i = 0; i < Initializer->size(); i++) {
            if (comma)
                sb += ", ";
            WriteObject(sb, ctx, ctxElement, (*Initializer)[i]);
            comma = true;
        }
    }

    if (!NamedArguments.empty()) {
        for (const auto& entry : NamedArguments) {
            if (comma)
                sb += ", ";
            else {
                sb += ' ';
                comma = true;
            }
            sb += entry.first;
            sb += '=';
            WriteObject(sb, ctx, ctxElement, entry.second);
        }
    }

    sb += '}';
    return sb;
}

} // namespace ILSpy::BamlDecompiler::Xaml
