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

// Port of ICSharpCode.BamlDecompiler/XmlnsDictionary.cs (Ki, 2015, MIT): the
// xmlns bookkeeping the XAML handlers drive while translating the BAML block
// tree. XmlnsDictionary owns the PIMapping table (the BAML
// PIMapping/AssemblyInfo record pairs that bind an XML namespace to a CLR
// namespace inside a specific assembly) plus the scoped prefix table: every
// element scope (HandlerMap.ProcessChildren's PushScope .. PopScope window)
// carries the XmlnsProperty records found inside that element, and lookups
// walk the scope chain innermost-first. XmlnsScope is one scope: the element
// it was pushed for, the chain link to the enclosing scope, and the prefix
// rows (the C# inherits List<NamespaceMap> for them).
//
// C#-to-C++ porting decisions:
//  * Reference aliasing: the C# List<NamespaceMap>/Dictionary hold GC
//    references, and PIFixup's in-place mutation of FullAssemblyName /
//    CLRNamespace must stay visible through every handle (a later
//    LookupNamespaceFromXmlns sees the fixed-up values -- gold probe S3d/e).
//    The port stores std::shared_ptr<NamespaceMap> in the scope's vector and
//    the PI table, and the lookups return the shared handle.
//  * The BamlElement payload of a scope is OPAQUE to everything in this
//    file: the C# never dereferences it inside XmlnsDictionary.cs. The port
//    forward-declares BamlElement (its port still needs the XAML DOM slice)
//    and stores a non-owning, possibly-null BamlElement*; the handler walk
//    will pass the element being processed, and the element must outlive the
//    scope (it does: the handler processes the element's own subtree while
//    the scope is live).
//  * Scope ownership: the C# scope chain is GC-owned and popped scopes stay
//    reachable through the XElement annotations ProcessChildren attaches
//    (AddAnnotation(ctx.XmlNs.CurrentScope), read back by
//    XamlType.ResolveNamespace). The port chains std::shared_ptr scopes
//    (each holds its PreviousScope), so the chain lives as long as the
//    dictionary's current scope or any annotation-held handle does; no
//    shared_ptr cycle exists because the element pointer is raw.
//  * piMappings: the C# Dictionary<string, NamespaceMap> iterates in
//    INSERTION order for the add-only tables this class builds, and
//    LookupXmlns's first match across two PI mappings sharing one
//    (assembly, clr-namespace) pair is order-dependent (gold probe S5f).
//    The port stores an insertion-ordered vector of (key, map) pairs with a
//    linear key scan -- the flat-vector substitute for the comparer-less
//    Dictionary (the MergedNamespace ChildMap convention; a real BAML's PI
//    table holds a handful of rows).
//  * The C# null==null CLR-namespace equality arm (LookupXmlns called with a
//    null clrNs matching a 3-arg row whose CLRNamespace is null -- gold
//    probe S5d) is unreachable through the port's std::string parameter;
//    every real caller passes the type's non-null namespace.
//  * PopScope/Add with a null current scope throw std::runtime_error with
//    the .NET NullReferenceException message (the BamlNode NRE mapping
//    convention; gold probe S7a/b).
//  * CurrentScope has a public setter in the C# (`{ get; set; }`) even
//    though only PushScope/PopScope use it; the port keeps SetCurrentScope
//    on the public surface.

#pragma once

#include "BamlDecompiler/Xaml/NamespaceMap.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::BamlDecompiler {

class BamlElement;

// One element scope of prefix rows (the C# `class XmlnsScope :
// List<NamespaceMap>`).
class XmlnsScope {
public:
	// The C# ctor takes the previous scope (null for the outermost one)
	// and the element the scope was pushed for (the ProcessChildren
	// element; opaque here, may be null in tests).
	XmlnsScope(std::shared_ptr<XmlnsScope> previousScope, BamlElement* element);

	BamlElement* Element() const;
	std::shared_ptr<XmlnsScope> PreviousScope() const;

	// The inherited List<NamespaceMap>.Add.
	void Add(std::shared_ptr<Xaml::NamespaceMap> map);

	// The inherited enumerable surface: the rows in insertion order.
	const std::vector<std::shared_ptr<Xaml::NamespaceMap>>& Maps() const;

	// The C# scope-local LookupXmlns: the first row of THIS scope whose
	// assembly and (non-null) CLR namespace match, rendered as its XML
	// namespace; a null CLR-namespace row never matches (gold probe S9b).
	std::optional<std::string> LookupXmlns(const std::string& fullAssemblyName,
		const std::string& clrNs) const;

private:
	std::shared_ptr<XmlnsScope> previousScope_;
	BamlElement* element_;
	std::vector<std::shared_ptr<Xaml::NamespaceMap>> maps_;
};

// The dictionary the handlers share through XamlContext.XmlNs.
class XmlnsDictionary {
public:
	// The C# ctor sets CurrentScope to null.
	XmlnsDictionary() = default;

	std::shared_ptr<XmlnsScope> CurrentScope() const;
	void SetCurrentScope(std::shared_ptr<XmlnsScope> scope);

	// The C# PushScope/PopScope pair ProcessChildren drives around each
	// element's children; Add lands the XmlnsProperty rows in the current
	// scope.
	void PushScope(BamlElement* element);
	void PopScope();
	void Add(std::shared_ptr<Xaml::NamespaceMap> map);

	// The PIMapping/AssemblyInfo record pair: bind xmlNs to clrNs inside
	// fullAssemblyName. First mapping wins; a later call for the same
	// xmlNs is ignored (gold probe S4).
	void SetPIMapping(const std::string& xmlNs, const std::string& clrNs,
		const std::string& fullAssemblyName);

	// Walk the scope chain innermost-first for the first row carrying the
	// prefix; the found row is fixed up through the PI table (mutating it
	// in place) before it is returned. Null when nothing matches.
	std::shared_ptr<Xaml::NamespaceMap> LookupNamespaceFromPrefix(const std::string& prefix);

	// The same walk without the PI fixup (gold probe S3f/g).
	std::shared_ptr<Xaml::NamespaceMap> LookupNamespaceFromXmlns(const std::string& xmlNs);

	// Reverse lookup (XamlType.ResolveNamespace's path): the PI table
	// first -- returning the PI row's KEY -- then the scope chain
	// innermost-first (gold probe S5a). Nullopt when nothing matches.
	std::optional<std::string> LookupXmlns(const std::string& fullAssemblyName,
		const std::string& clrNs) const;

private:
	// The C# PIFixup: when the row's XML namespace has a PI mapping, copy
	// the PI row's assembly and CLR namespace into it (in place -- visible
	// through every handle to the same row).
	std::shared_ptr<Xaml::NamespaceMap> PIFixup(std::shared_ptr<Xaml::NamespaceMap> map);

	// Insertion-ordered (xmlNs key, row) pairs (the C#
	// Dictionary<string, NamespaceMap>).
	std::vector<std::pair<std::string, std::shared_ptr<Xaml::NamespaceMap>>> piMappings_;
	std::shared_ptr<XmlnsScope> currentScope_;
};

} // namespace ILSpy::BamlDecompiler
