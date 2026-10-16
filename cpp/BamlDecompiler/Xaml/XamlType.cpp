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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlType.cs (Ki, 2015, MIT) -- the
// implementation half (ResolveNamespace/ToXName need the complete
// XamlContext). See XamlType.hpp for the porting decisions.

#include "BamlDecompiler/Xaml/XamlType.hpp"

#include "BamlDecompiler/XamlContext.hpp"
#include "BamlDecompiler/XmlnsDictionary.hpp"
#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Xml/XmlConvert.hpp"
#include "Decompiler/Xml/XComment.hpp"

#include <memory>
#include <stdexcept>

namespace ILSpy::BamlDecompiler::Xaml {

namespace {

// The C# `string.ToLowerInvariant()` over the last namespace segment --
// ASCII-scoped (the iteration-31 ToUpperInvariant precedent: the full .NET
// casing table maps only exotic units; a CLR namespace segment is ASCII in
// practice).
std::string ToLowerInvariantAscii(std::string value)
{
	for (char& c : value) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c + ('a' - 'A'));
	}
	return value;
}

} // namespace

// The C# `void ResolveNamespace(XElement elem, XamlContext ctx)`.
void XamlType::ResolveNamespace(Xml::XElement& elem, XamlContext& ctx)
{
	if (namespace_.has_value())
		return;

	// Since XmlnsProperty records are inside the element, the namespace is
	// resolved after processing the element body: the element's own scope
	// annotation first, the PIMapping table second, the assembly's
	// XmlnsDefinitionAttribute rows third.
	std::optional<std::string> xmlNs;
	if (auto* scope = elem.Annotation<std::shared_ptr<XmlnsScope>>())
		xmlNs = (*scope)->LookupXmlns(FullAssemblyName, TypeNamespace);
	if (!xmlNs.has_value())
		xmlNs = ctx.XmlNs().LookupXmlns(FullAssemblyName, TypeNamespace);
	// Sometimes there's no reference to System.Xaml even if x:Type is used.
	if (!xmlNs.has_value())
		xmlNs = ctx.TryGetXmlNamespace(Assembly, TypeNamespace);

	if (!xmlNs.has_value()) {
		// The C# reassigns the xmlNs variable itself -- the generated
		// clr-namespace URI is the namespace the block below resolves and
		// the tail assignment caches.
		if (FullAssemblyName == ctx.TypeSystem().MainModule().FullAssemblyName()) {
			xmlNs = "clr-namespace:" + TypeNamespace;
		} else {
			auto name = ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
				FullAssemblyName);
			xmlNs = "clr-namespace:" + TypeNamespace + ";assembly=" + name.Name();
		}

		// The C# `TypeNamespace.Split('.')`'s LAST element (empty entries
		// preserved: "a." and "" both yield "" -- the substring after the
		// last separator, or the whole string when there is none).
		std::size_t lastDot = TypeNamespace.rfind('.');
		std::string prefix = ToLowerInvariantAscii(
			lastDot == std::string::npos ? TypeNamespace
										 : TypeNamespace.substr(lastDot + 1));
		if (prefix.empty()) {
			if (TypeNamespace.empty())
				prefix = "global";
			else
				prefix = "empty";
		}
		int count = 0;
		std::string truePrefix = prefix;
		Xml::XNamespace ns = *ctx.GetXmlNamespace(xmlNs);
		std::optional<Xml::XNamespace> prefixNs;
		while ((prefixNs = elem.GetNamespaceOfPrefix(truePrefix)).has_value()
			&& *prefixNs != ns) {
			count++;
			truePrefix = prefix + std::to_string(count);
		}

		if (!prefixNs.has_value()) {
			elem.Add(std::make_shared<Xml::XAttribute>(
				Xml::XNamespace::Xmlns() + Xml::EncodeLocalName(truePrefix),
				ns.NamespaceName()));
			if (TypeNamespace.empty())
				elem.AddBeforeSelf(std::make_shared<Xml::XComment>(
					"'" + truePrefix + "' is prefix for the global namespace"));
		}
	}
	namespace_ = ctx.GetXmlNamespace(xmlNs);
}

// The C# `XName ToXName(XamlContext ctx)`.
Xml::XName XamlType::ToXName(XamlContext& ctx) const
{
	if (!namespace_.has_value())
		return Xml::XName::Get(Xml::EncodeLocalName(TypeName));
	return *namespace_ + Xml::EncodeLocalName(TypeName);
}

} // namespace ILSpy::BamlDecompiler::Xaml
