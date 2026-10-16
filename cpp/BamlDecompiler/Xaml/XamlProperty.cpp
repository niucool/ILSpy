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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlProperty.cs (Ki, 2015, MIT) --
// the implementation half (ToXName needs the complete XamlContext). See
// XamlProperty.hpp for the porting decisions.

#include "BamlDecompiler/Xaml/XamlProperty.hpp"

#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/Xml/XmlConvert.hpp"

#include <stdexcept>

namespace ILSpy::BamlDecompiler::Xaml {

namespace {

// The standard .NET NullReferenceException message (the XmlnsDictionary NRE
// convention).
const char* kNullReferenceMessage =
	"Object reference not set to an instance of an object.";

// The C# `IType.FullName` (the INamedElement property; the port's IType
// carries Name/ReflectionName only, so the AbstractType `Namespace + "." +
// Name` composition is a local helper -- the ILAmbience FullNameOf shape,
// copied next to its second consumer).
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

} // namespace

// The C# `void TryResolve()`.
void XamlProperty::TryResolve()
{
	if (ResolvedMember != nullptr)
		return;

	// The C# `DeclaringType.ResolvedType.GetDefinition()` -- a null
	// ResolvedType is the null-receiver NRE.
	if (DeclaringType == nullptr || DeclaringType->ResolvedType == nullptr)
		throw std::runtime_error(kNullReferenceMessage);

	const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDef =
		DeclaringType->ResolvedType->GetDefinition();
	if (typeDef == nullptr)
		return;

	// The C# `GetProperties(p => p.Name == PropertyName).FirstOrDefault()` --
	// the member enumerations run at the default `GetMemberOptions.None`.
	auto properties = typeDef->GetProperties(
		[this](const ILSpy::Decompiler::TypeSystem::IProperty* p) {
			return p->Name() == PropertyName;
		});
	if (!properties.empty()) {
		ResolvedMember = properties.front();
		return;
	}

	auto propertyFields = typeDef->GetFields(
		[this](const ILSpy::Decompiler::TypeSystem::IField* f) {
			return f->Name() == PropertyName + "Property";
		});
	if (!propertyFields.empty()) {
		ResolvedMember = propertyFields.front();
		return;
	}

	auto events = typeDef->GetEvents(
		[this](const ILSpy::Decompiler::TypeSystem::IEvent* e) {
			return e->Name() == PropertyName;
		});
	if (!events.empty()) {
		ResolvedMember = events.front();
		return;
	}

	auto eventFields = typeDef->GetFields(
		[this](const ILSpy::Decompiler::TypeSystem::IField* f) {
			return f->Name() == PropertyName + "Event";
		});
	if (!eventFields.empty())
		ResolvedMember = eventFields.front();
}

// The C# `bool IsAttachedTo(XamlType type)`.
bool XamlProperty::IsAttachedTo(const XamlType* type) const
{
	if (type == nullptr || ResolvedMember == nullptr || type->ResolvedType == nullptr)
		return true;

	// The C# `var declType = ResolvedMember.DeclaringType;` then
	// `declType.FullName` -- a null declaring type is the null-receiver NRE.
	ILSpy::Decompiler::TypeSystem::ITypePtr declType = ResolvedMember->DeclaringType();
	if (declType == nullptr)
		throw std::runtime_error(kNullReferenceMessage);

	ILSpy::Decompiler::TypeSystem::ITypePtr t = type->ResolvedType;
	do {
		if (FullNameOf(*t) == FullNameOf(*declType)
			&& t->TypeParameterCount() == declType->TypeParameterCount())
			return false;
		std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> baseTypes =
			t->DirectBaseTypes();
		t = baseTypes.empty() ? nullptr : baseTypes.front();
	} while (t != nullptr);
	return true;
}

// The C# `XName ToXName(XamlContext ctx, XElement parent, bool isFullName = true)`.
Xml::XName XamlProperty::ToXName(XamlContext& ctx, const Xml::XElement* parent,
	bool isFullName) const
{
	Xml::XName typeName = DeclaringType->ToXName(ctx);
	if (!isFullName)
		return Xml::XName::Get(Xml::EncodeLocalName(PropertyName));

	// The C# `name = typeName.LocalName + "." + EncodeLocalName(PropertyName)`
	// -- the implicit string-to-XName conversion yields the None namespace;
	// only the rebinding below carries a namespace.
	std::string localName =
		typeName.LocalName() + "." + Xml::EncodeLocalName(PropertyName);
	if (parent == nullptr || !(parent->GetDefaultNamespace() == typeName.Namespace()))
		return typeName.Namespace() + localName;
	return Xml::XName::Get(localName);
}

} // namespace ILSpy::BamlDecompiler::Xaml
