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

// Port of ICSharpCode.BamlDecompiler/XamlContext.cs (Ki, 2015, MIT) -- the
// implementation half. See XamlContext.hpp for the porting decisions.

#include "BamlDecompiler/XamlContext.hpp"

#include "BamlDecompiler/Baml/BamlDocument.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"

#include <algorithm>
#include <any>
#include <stdexcept>
#include <utility>

namespace ILSpy::BamlDecompiler {

namespace {

// The standard .NET NullReferenceException message (the XmlnsDictionary NRE
// convention) for the null surfaces the C# dereferences.
const char* kNullReferenceMessage =
	"Object reference not set to an instance of an object.";

// The C# `IType.FullName` (the INamedElement property the C# interface
// exposes; the port's IType carries Name/ReflectionName only, so the
// AbstractType `Namespace + "." + Name` composition is a local helper --
// the ILAmbience FullNameOf shape, copied next to its second consumer).
std::string FullNameOf(const ILSpy::Decompiler::TypeSystem::IType& type)
{
	std::string ns;
	if (const auto* entity = dynamic_cast<const ILSpy::Decompiler::TypeSystem::IEntity*>(&type))
		ns = entity->Namespace();
	else if (const auto* pt = dynamic_cast<const ILSpy::Decompiler::TypeSystem::ParameterizedType*>(&type))
		ns = pt->GenericType() ? FullNameOf(*pt->GenericType()) : std::string();
	else if (const auto* unknown = dynamic_cast<const class ILSpy::Decompiler::TypeSystem::UnknownType*>(&type))
		ns = unknown->FullTypeName().GetTopLevelTypeName().Namespace();
	if (ns.empty())
		return type.Name();
	return ns + "." + type.Name();
}

// The C# `string type.Namespace` on the record arm's `IType` (a definition, an
// UnknownType fallback, or a composite): the port's `IType` declares no `Namespace`
// virtual (it lives on `INamedElement`, the IEntity base), so the ILAmbience /
// TypeSystemAstBuilder `NamespaceOf` helper ships here as its third consumer --
// entities delegate to their own `Namespace`, parameterized types to their generic,
// `UnknownType` to its stored full-name namespace, everything else the empty default.
std::string NamespaceOf(const ILSpy::Decompiler::TypeSystem::IType& type)
{
	if (const auto* entity =
		dynamic_cast<const ILSpy::Decompiler::TypeSystem::IEntity*>(&type))
		return entity->Namespace();
	if (const auto* pt =
		dynamic_cast<const ILSpy::Decompiler::TypeSystem::ParameterizedType*>(&type))
		return pt->GenericType() ? NamespaceOf(*pt->GenericType()) : std::string();
	if (const auto* unknown =
		dynamic_cast<const class ILSpy::Decompiler::TypeSystem::UnknownType*>(&type))
		return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
	return std::string();
}

} // namespace

XamlContext::XamlContext(const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem)
	: typeSystem_(typeSystem)
{
}

// Out-of-line (the incomplete unique_ptr map values -- see the header).
XamlContext::~XamlContext() = default;

// The C# `static XamlContext Construct(...)`: the record walk, the tree parse,
// the PIMapping feed, then the node map (in the C# order -- a ConstructContext
// failure propagates before any parse runs).
std::unique_ptr<XamlContext> XamlContext::Construct(
	const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
	Baml::BamlDocument& document,
	const BamlDecompilerSettings* bamlDecompilerOptions)
{
	auto ctx(std::unique_ptr<XamlContext>(new XamlContext(typeSystem)));
	if (bamlDecompilerOptions != nullptr) {
		ctx->settings_ = bamlDecompilerOptions;
	} else {
		ctx->ownedSettings_ = std::make_unique<BamlDecompilerSettings>();
		ctx->settings_ = ctx->ownedSettings_.get();
	}

	ctx->baml_ = Baml::BamlContext::ConstructContext(typeSystem, document);
	ctx->rootNode_ = Baml::BamlNode::Parse(document);
	ctx->BuildPIMappings(document);
	ctx->BuildNodeMap(ctx->rootNode_.get());
	return ctx;
}

// The C# `void BuildNodeMap(BamlBlockNode node)`.
void XamlContext::BuildNodeMap(Baml::BamlBlockNode* node)
{
	if (node == nullptr)
		return;

	nodeMap_[node->Header] = node;

	for (const auto& child : node->Children) {
		if (auto* childBlock = dynamic_cast<Baml::BamlBlockNode*>(child.get()))
			BuildNodeMap(childBlock);
	}
}

// The C# `void BuildPIMappings(BamlDocument document)`.
void XamlContext::BuildPIMappings(Baml::BamlDocument& document)
{
	for (const auto& record : document.Records) {
		const auto* piMap = dynamic_cast<const Baml::PIMappingRecord*>(record.get());
		if (piMap == nullptr)
			continue;

		xmlNs_.SetPIMapping(piMap->XmlNamespace, piMap->ClrNamespace,
			baml_->ResolveAssembly(piMap->AssemblyId).FullAssemblyName);
	}
}

// The C# `XamlType ResolveType(ushort id)`.
Xaml::XamlType* XamlContext::ResolveType(std::uint16_t id)
{
	return ResolveTypeOwning(id).get();
}

// The annotation-rooting form (see the header): the cache entry is the
// owning handle the handlers' element annotations share.
std::shared_ptr<Xaml::XamlType> XamlContext::ResolveTypeOwning(std::uint16_t id)
{
	auto cached = typeMap_.find(id);
	if (cached != typeMap_.end())
		return cached->second;

	const ILSpy::Decompiler::TypeSystem::IType* type = nullptr;
	// The record arm's resolved type (the ParseReflectionName result owns its
	// composites; the KnownThings arm aliases the module-owned definition with the
	// no-op deleter) -- what `{ ResolvedType = type }` snapshots.
	ILSpy::Decompiler::TypeSystem::ITypePtr resolvedType;
	const ILSpy::Decompiler::TypeSystem::IModule* assembly = nullptr;
	std::string fullAssemblyName;

	if (id > 0x7fff) {
		// The C# `Baml.KnownThings.Types((KnownTypes)(short)-unchecked((short)id))`:
		// the wire form of a known-type id is `(ushort)(-index)`, so the
		// negation of the sign-extended id recovers it.
		const ILSpy::Decompiler::TypeSystem::ITypeDefinition* knownType =
			baml_->KnownThings().Types(static_cast<Baml::KnownTypes>(
				static_cast<std::int16_t>(-static_cast<std::int16_t>(id))));
		// The C# `type.GetDefinition().ParentModule` -- GetDefinition() on a
		// definition returns itself; a null type (or its null parent module)
		// is the C# NullReferenceException.
		if (knownType == nullptr)
			throw std::runtime_error(kNullReferenceMessage);
		assembly = knownType->ParentModule();
		if (assembly == nullptr)
			throw std::runtime_error(kNullReferenceMessage);
		fullAssemblyName = assembly->FullAssemblyName();
		type = knownType;
		// The KnownThings cache owns the type definition; the port snapshots it as a
		// NON-OWNING shared_ptr alias (the KnownTypeCache convention (d)
		// no-op-deleter precedent).
		resolvedType = ILSpy::Decompiler::TypeSystem::ITypePtr(
			const_cast<ILSpy::Decompiler::TypeSystem::IType*>(
				static_cast<const ILSpy::Decompiler::TypeSystem::IType*>(knownType)),
			[](ILSpy::Decompiler::TypeSystem::IType*) {});
	} else {
		const Baml::TypeInfoRecord& typeRec = *baml_->TypeIdMap.at(id);
		Baml::ResolvedAssembly resolved = baml_->ResolveAssembly(typeRec.AssemblyId);
		fullAssemblyName = resolved.FullAssemblyName;
		assembly = resolved.Assembly;
		// The C# `ReflectionHelper.ParseReflectionName(typeRec.TypeFullName, new
		// SimpleTypeResolveContext(TypeSystem))` -- the reflection-name parser/resolver
		// over the iteration-48 `TypeName` parser, resolving against this context's
		// compilation (the IDecompilerTypeSystem narrowed to its ICompilation surface).
		resolvedType = ILSpy::Decompiler::TypeSystem::ParseReflectionName(
			typeRec.TypeFullName,
			ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext(TypeSystem()));
		type = resolvedType.get();
	}

	std::string clrNs = NamespaceOf(*type);
	std::optional<std::string> xmlNs = xmlNs_.LookupXmlns(fullAssemblyName, clrNs);

	auto xamlType = std::make_shared<Xaml::XamlType>(assembly, fullAssemblyName, clrNs,
		type->Name(), GetXmlNamespace(xmlNs));
	// The C# `{ ResolvedType = type }`.
	xamlType->ResolvedType = resolvedType;

	std::shared_ptr<Xaml::XamlType> result = xamlType;
	typeMap_.emplace(id, std::move(xamlType));
	return result;
}

// The C# `XamlProperty ResolveProperty(ushort id)`.
Xaml::XamlProperty* XamlContext::ResolveProperty(std::uint16_t id)
{
	return ResolvePropertyOwning(id).get();
}

// The annotation-rooting form (see the header).
std::shared_ptr<Xaml::XamlProperty> XamlContext::ResolvePropertyOwning(std::uint16_t id)
{
	auto cached = propertyMap_.find(id);
	if (cached != propertyMap_.end())
		return cached->second;

	Xaml::XamlType* type = nullptr;
	std::string name;
	const ILSpy::Decompiler::TypeSystem::IMember* member = nullptr;

	if (id > 0x7fff) {
		// The C# `Baml.KnownThings.Members((KnownMembers)unchecked((short)-(short)id))`
		// -- the same `(ushort)(-index)` wire form as the type ids.
		const Baml::KnownMember* knownProp = baml_->KnownThings().Members(
			static_cast<Baml::KnownMembers>(
				static_cast<std::int16_t>(-static_cast<std::int16_t>(id))));
		type = ResolveType(static_cast<std::uint16_t>(static_cast<std::int16_t>(
			-static_cast<std::int16_t>(knownProp->Parent))));
		name = knownProp->Name;
		member = knownProp->Property;
	} else {
		const Baml::AttributeInfoRecord& attrRec = *baml_->AttributeIdMap.at(id);
		type = ResolveType(attrRec.OwnerTypeId);
		name = attrRec.Name;
		member = nullptr;
	}

	auto xamlProp = std::make_shared<Xaml::XamlProperty>(type, name);
	xamlProp->ResolvedMember = member;
	xamlProp->TryResolve();

	std::shared_ptr<Xaml::XamlProperty> result = xamlProp;
	propertyMap_.emplace(id, std::move(xamlProp));
	return result;
}

// The C# `string ResolveString(ushort id)`.
std::optional<std::string> XamlContext::ResolveString(std::uint16_t id)
{
	if (id > 0x7fff) {
		// The C# `Baml.KnownThings.Strings(unchecked((short)-id))`: `-id`
		// promotes to int, the `(short)` cast wraps (ids above 0xffff - 2 are
		// unreachable -- the strings table holds two rows).
		return baml_->KnownThings().Strings(
			static_cast<std::int16_t>(-static_cast<std::int32_t>(id)));
	}
	if (id < baml_->StringIdMap.size())
		return baml_->StringIdMap[id]->Value;

	return std::nullopt;
}

// The C# `XNamespace GetXmlNamespace(string xmlns)`.
std::optional<Xml::XNamespace> XamlContext::GetXmlNamespace(
	const std::optional<std::string>& xmlns)
{
	if (!xmlns.has_value())
		return std::nullopt;

	auto it = xmlnsMap_.find(*xmlns);
	if (it == xmlnsMap_.end())
		it = xmlnsMap_.emplace(*xmlns, Xml::XNamespace::Get(*xmlns)).first;
	return it->second;
}

// The C# `string TryGetXmlNamespace(IModule assembly, string typeNamespace)`.
std::optional<std::string> XamlContext::TryGetXmlNamespace(
	const ILSpy::Decompiler::TypeSystem::IModule* assembly,
	const std::string& typeNamespace) const
{
	if (assembly == nullptr)
		return std::nullopt;

	// The C# `HashSet<string> possibleXmlNs` -- an add-only HashSet iterates
	// in insertion order, so the port is a dedup-on-insert vector (the
	// FirstOrDefault pick below keeps the first insertion).
	std::vector<std::string> possibleXmlNs;
	for (const auto* attr : assembly->GetAssemblyAttributes()) {
		if (FullNameOf(attr->AttributeType())
			!= "System.Windows.Markup.XmlnsDefinitionAttribute")
			continue;

		auto fixedArguments = attr->FixedArguments();
		if (fixedArguments.size() != 2)
			continue;

		const std::any& firstValue = fixedArguments[0].Value();
		const std::any& secondValue = fixedArguments[1].Value();
		const std::string* xmlNs = std::any_cast<std::string>(&firstValue);
		const std::string* typeNs = std::any_cast<std::string>(&secondValue);
		if (xmlNs == nullptr || typeNs == nullptr)
			continue;

		if (typeNamespace == *typeNs) {
			if (std::find(possibleXmlNs.begin(), possibleXmlNs.end(), *xmlNs)
				== possibleXmlNs.end())
				possibleXmlNs.push_back(*xmlNs);
		}
	}

	for (const auto& ns : possibleXmlNs) {
		if (ns == KnownNamespace_Presentation)
			return KnownNamespace_Presentation;
	}
	if (!possibleXmlNs.empty())
		return possibleXmlNs.front();
	return std::nullopt;
}

// The C# `XName GetKnownNamespace(string name, string xmlNamespace, XElement
// context = null)`.
Xml::XName XamlContext::GetKnownNamespace(const std::string& name,
	const std::string& xmlNamespace, const Xml::XElement* context)
{
	std::optional<Xml::XNamespace> xNs = GetXmlNamespace(xmlNamespace);
	if (context != nullptr && xNs.has_value() && *xNs == context->GetDefaultNamespace())
		return Xml::XName::Get(name);
	// The `xNs + name` arm with a null xNs is the C# null-receiver NRE
	// (GetXmlNamespace returns null only for a null input).
	if (!xNs.has_value())
		throw std::runtime_error(kNullReferenceMessage);
	return *xNs + name;
}

// The C# `XName GetPseudoName(string name)`.
Xml::XName XamlContext::GetPseudoName(const std::string& name) const
{
	return Xml::XNamespace::Get("https://github.com/icsharpcode/ILSpy").GetName(name);
}

} // namespace ILSpy::BamlDecompiler
