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

// Port of ICSharpCode.BamlDecompiler/Xaml/NamespaceMap.cs (Ki, 2015, MIT):
// one xmlns-mapping row -- the prefix, the assembly the namespace resolves
// into, the XML namespace URI, and (for clr-namespace URIs) the CLR
// namespace. These rows are what XmlnsDictionary stores in its scopes and
// PI-mapping table.
//
// C#-to-C++ porting decisions:
//  * The C# auto-properties ({ get; set; }) port to public data members:
//    XmlnsDictionary's PIFixup mutates FullAssemblyName/CLRNamespace of an
//    already-stored map in place, and XamlContext/handlers assign the
//    fields at construction time.
//  * CLRNamespace is the one null-carrying field whose null state is
//    observable: the 3-arg constructor chain passes null, both LookupXmlns
//    overloads compare it against the requested CLR namespace (null never
//    equals a non-null request), and ToString's `CLRNamespace ??
//    XMLNamespace` falls back to the XML namespace. It ports to
//    std::optional<std::string> (nullopt is the C# null).
//  * XmlnsPrefix is only ever null through SetPIMapping's
//    `new NamespaceMap(null, ...)`, and a PI mapping never enters a scope
//    (LookupNamespaceFromPrefix walks scopes only), so a null prefix is
//    observable only through ToString -- where .NET's interpolation
//    renders null as the empty string anyway. It ports to a plain
//    std::string (null == ""), a documented equivalence.
//  * FullAssemblyName and XMLNamespace are never null at any call site
//    (the handlers pass resolved assembly names and record strings, which
//    BAML strings guarantee non-null): plain std::string members.

#pragma once

#include <optional>
#include <string>
#include <utility>

namespace ILSpy::BamlDecompiler::Xaml {

class NamespaceMap {
public:
	// The C# 3-arg constructor chains to the 4-arg one with a null CLR
	// namespace (an xmlns that is not a clr-namespace mapping).
	NamespaceMap(std::string prefix, std::string fullAssemblyName, std::string xmlNs)
		: NamespaceMap(std::move(prefix), std::move(fullAssemblyName), std::move(xmlNs), std::nullopt)
	{
	}

	NamespaceMap(std::string prefix, std::string fullAssemblyName, std::string xmlNs,
		std::optional<std::string> clrNs)
		: XmlnsPrefix(std::move(prefix)),
		  FullAssemblyName(std::move(fullAssemblyName)),
		  XMLNamespace(std::move(xmlNs)),
		  CLRNamespace(std::move(clrNs))
	{
	}

	std::string XmlnsPrefix;
	std::string FullAssemblyName;
	std::string XMLNamespace;
	std::optional<std::string> CLRNamespace;

	// The C# `$"{XmlnsPrefix}:[{FullAssemblyName}|{CLRNamespace ?? XMLNamespace}]"`
	// (a diagnostic render; no decompiler output depends on it).
	std::string ToString() const
	{
		std::string result = XmlnsPrefix;
		result += ":[";
		result += FullAssemblyName;
		result += '|';
		result += CLRNamespace ? *CLRNamespace : XMLNamespace;
		result += ']';
		return result;
	}
};

} // namespace ILSpy::BamlDecompiler::Xaml
