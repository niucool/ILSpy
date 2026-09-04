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

// Port of ICSharpCode.BamlDecompiler/XmlnsDictionary.cs (Ki, 2015, MIT). See
// XmlnsDictionary.hpp for the porting decisions; every observable behavior
// here is pinned against the real assembly by the gold probe
// (C:\temp-probe\XmlnsProbe) and the XmlnsDictionaryTest suite.

#include "BamlDecompiler/XmlnsDictionary.hpp"

#include <stdexcept>
#include <utility>

namespace ILSpy::BamlDecompiler {

// The NullReferenceException message every null-CurrentScope arm produces
// (gold probe S7a/S7b/S8b).
static const char* kNullReferenceMessage =
	"Object reference not set to an instance of an object.";

XmlnsScope::XmlnsScope(std::shared_ptr<XmlnsScope> previousScope, BamlElement* element)
	: previousScope_(std::move(previousScope)),
	  element_(element)
{
}

BamlElement* XmlnsScope::Element() const
{
	return element_;
}

std::shared_ptr<XmlnsScope> XmlnsScope::PreviousScope() const
{
	return previousScope_;
}

void XmlnsScope::Add(std::shared_ptr<Xaml::NamespaceMap> map)
{
	maps_.push_back(std::move(map));
}

const std::vector<std::shared_ptr<Xaml::NamespaceMap>>& XmlnsScope::Maps() const
{
	return maps_;
}

std::optional<std::string> XmlnsScope::LookupXmlns(const std::string& fullAssemblyName,
	const std::string& clrNs) const
{
	for (const auto& ns : maps_) {
		// A disengaged CLRNamespace (the C# null) compares unequal to
		// every requested namespace -- std::optional's mixed comparison
		// gives exactly the C# `null != "..."` behavior.
		if (fullAssemblyName == ns->FullAssemblyName && ns->CLRNamespace == clrNs)
			return ns->XMLNamespace;
	}
	return std::nullopt;
}

std::shared_ptr<XmlnsScope> XmlnsDictionary::CurrentScope() const
{
	return currentScope_;
}

void XmlnsDictionary::SetCurrentScope(std::shared_ptr<XmlnsScope> scope)
{
	currentScope_ = std::move(scope);
}

void XmlnsDictionary::PushScope(BamlElement* element)
{
	currentScope_ = std::make_shared<XmlnsScope>(currentScope_, element);
}

void XmlnsDictionary::PopScope()
{
	if (!currentScope_)
		throw std::runtime_error(kNullReferenceMessage);
	currentScope_ = currentScope_->PreviousScope();
}

void XmlnsDictionary::Add(std::shared_ptr<Xaml::NamespaceMap> map)
{
	if (!currentScope_)
		throw std::runtime_error(kNullReferenceMessage);
	currentScope_->Add(std::move(map));
}

void XmlnsDictionary::SetPIMapping(const std::string& xmlNs, const std::string& clrNs,
	const std::string& fullAssemblyName)
{
	// The C# ContainsKey guard: the first mapping for an XML namespace
	// wins (gold probe S4).
	for (const auto& [key, value] : piMappings_) {
		if (key == xmlNs)
			return;
	}
	// The C# `new NamespaceMap(null, fullAssemblyName, xmlNs, clrNs)` --
	// the null prefix is the documented null == "" equivalence of the
	// ported NamespaceMap.
	piMappings_.emplace_back(xmlNs,
		std::make_shared<Xaml::NamespaceMap>("", fullAssemblyName, xmlNs, clrNs));
}

std::shared_ptr<Xaml::NamespaceMap> XmlnsDictionary::PIFixup(
	std::shared_ptr<Xaml::NamespaceMap> map)
{
	for (const auto& [key, value] : piMappings_) {
		if (key == map->XMLNamespace) {
			map->FullAssemblyName = value->FullAssemblyName;
			map->CLRNamespace = value->CLRNamespace;
			break;
		}
	}
	return map;
}

std::shared_ptr<Xaml::NamespaceMap> XmlnsDictionary::LookupNamespaceFromPrefix(
	const std::string& prefix)
{
	auto scope = currentScope_;
	while (scope) {
		for (const auto& ns : scope->Maps()) {
			if (ns->XmlnsPrefix == prefix)
				return PIFixup(ns);
		}
		scope = scope->PreviousScope();
	}
	return nullptr;
}

std::shared_ptr<Xaml::NamespaceMap> XmlnsDictionary::LookupNamespaceFromXmlns(
	const std::string& xmlNs)
{
	// No PI fixup on this path (gold probe S3f/S3g): the raw row comes
	// back exactly as the scope stored it.
	auto scope = currentScope_;
	while (scope) {
		for (const auto& ns : scope->Maps()) {
			if (ns->XMLNamespace == xmlNs)
				return ns;
		}
		scope = scope->PreviousScope();
	}
	return nullptr;
}

std::optional<std::string> XmlnsDictionary::LookupXmlns(const std::string& fullAssemblyName,
	const std::string& clrNs) const
{
	// The PI table first, in insertion order, returning the row's KEY
	// (the XML namespace the mapping was registered under).
	for (const auto& [key, value] : piMappings_) {
		if (fullAssemblyName == value->FullAssemblyName && value->CLRNamespace == clrNs)
			return key;
	}
	// Then the scope chain, innermost-first.
	auto scope = currentScope_;
	while (scope) {
		const auto found = scope->LookupXmlns(fullAssemblyName, clrNs);
		if (found)
			return found;
		scope = scope->PreviousScope();
	}
	return std::nullopt;
}

} // namespace ILSpy::BamlDecompiler
