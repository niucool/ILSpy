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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The CSharpAmbience implementation (see the header for the class-level porting
// decisions). The member order follows the C# source order.

#include "Decompiler/CSharp/OutputVisitor/CSharpAmbience.hpp"

#include "Decompiler/CSharp/OutputVisitor/CSharpOutputVisitor.hpp"
#include "Decompiler/CSharp/OutputVisitor/FormattingOptionsFactory.hpp"
#include "Decompiler/CSharp/OutputVisitor/TextWriterTokenWriter.hpp"
#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DelegateDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"  // Slots::Parameter
#include "Decompiler/CSharp/Syntax/Slots.hpp"                  // Slots::Type
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"       // Detach
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"  // Slots::TypeParameter
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"

#include "Decompiler/TypeSystem/Implementation/LocalFunctionMethod.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

using Syntax::AstNode;
using Syntax::DelegateDeclaration;
using Syntax::EntityDeclaration;
using Syntax::EventDeclaration;
using Syntax::NamespaceDeclaration;
using Syntax::TypeDeclaration;
using Syntax::TypeSystemAstBuilder;  // the sibling-Syntax class (unqualified lookup
                                     // from OutputVisitor does not find it)

// The C# `public string ConvertSymbol(ISymbol symbol)` (CSharpAmbience.cs line 54) -- the
// StringWriter-driven convenience entry. The C# `new StringWriter()` + `new
// TextWriterTokenWriter(writer)` + `FormattingOptionsFactory.CreateEmpty()` ports to a
// stack-local `std::ostringstream` + `TextWriterTokenWriter` over it with the empty
// policy; the `writer.ToString()` reads the stream's `str()`.
std::string CSharpAmbience::ConvertSymbol(const TS::ISymbol& symbol) {
	std::ostringstream writer;
	TextWriterTokenWriter tokenWriter(&writer);
	ConvertSymbol(symbol, &tokenWriter, FormattingOptionsFactory::CreateEmpty());
	return writer.str();
}

// The C# `public void ConvertSymbol(ISymbol symbol, TokenWriter writer, CSharpFormattingOptions
// formattingPolicy)` (line 61) -- the writer-driven core.
void CSharpAmbience::ConvertSymbol(const TS::ISymbol& symbol, TokenWriter* writer,
                                    CSharpFormattingOptions formattingPolicy) const {
	if (writer == nullptr)
		throw std::invalid_argument("writer");

	TypeSystemAstBuilder astBuilder = CreateAstBuilder();

	ExtensionGroup extensionGroup;
	if (IsExtension(dynamic_cast<const TS::ITypeDefinition*>(&symbol), extensionGroup)) {
		astBuilder.ShowParameterNames() = true;
	} else {
		extensionGroup = ExtensionGroup{};
	}
	AstNode* node = astBuilder.ConvertSymbol(symbol);
	writer->StartNode(node);
	if (auto* entityDecl = dynamic_cast<EntityDeclaration*>(node))
		PrintModifiers(entityDecl->Modifiers(), writer);

	// The C# `(ConversionFlags & ConversionFlags.ShowDefinitionKeyword) == ...` block:
	// the class/struct/interface/enum/record keyword, the `delegate`/`event`/`namespace`
	// keywords. The `Exception("Invalid value for ClassType")` default arm ports to
	// `std::runtime_error` (the CreateResolveResult InvalidOperationException-analog
	// convention).
	if ((ConversionFlags() & CF::ShowDefinitionKeyword) == CF::ShowDefinitionKeyword) {
		if (auto* typeDecl = dynamic_cast<TypeDeclaration*>(node)) {
			switch (typeDecl->ClassType()) {
				case Syntax::ClassType::Class:
					writer->WriteKeyword("class");
					break;
				case Syntax::ClassType::Struct:
					writer->WriteKeyword("struct");
					break;
				case Syntax::ClassType::Interface:
					writer->WriteKeyword("interface");
					break;
				case Syntax::ClassType::Enum:
					writer->WriteKeyword("enum");
					break;
				case Syntax::ClassType::RecordClass:
					writer->WriteKeyword("record");
					break;
				case Syntax::ClassType::RecordStruct:
					writer->WriteKeyword("record");
					writer->Space();
					writer->WriteKeyword("struct");
					break;
				default:
					throw std::runtime_error("Invalid value for ClassType");
			}
			writer->Space();
		} else if (dynamic_cast<DelegateDeclaration*>(node) != nullptr) {
			writer->WriteKeyword("delegate");
			writer->Space();
		} else if (dynamic_cast<EventDeclaration*>(node) != nullptr) {
			writer->WriteKeyword("event");
			writer->Space();
		} else if (dynamic_cast<NamespaceDeclaration*>(node) != nullptr) {
			writer->WriteKeyword("namespace");
			writer->Space();
		}
	}

	// The C# return-type-before-the-parameter-list arm: gated on
	// NOT-PlaceReturnTypeAfterParameterList AND ShowReturnType. `node.GetChild(Slots.Type)`
	// ports to the kind-based `GetChildByKind` read (the C# GetChild(Slot) form).
	if ((ConversionFlags() & CF::PlaceReturnTypeAfterParameterList) != CF::PlaceReturnTypeAfterParameterList
		&& (ConversionFlags() & CF::ShowReturnType) == CF::ShowReturnType) {
		if (auto* rt = node->GetChildByKind(&Syntax::Slots::Type)) {
			CSharpOutputVisitor visitor(writer, formattingPolicy);
			rt->AcceptVisitor(visitor);
			writer->Space();
		}
	}

	if (auto* typeDef = dynamic_cast<const TS::ITypeDefinition*>(&symbol)) {
		WriteTypeDeclarationName(*typeDef, writer, formattingPolicy);
	} else if (auto* member = dynamic_cast<const TS::IMember*>(&symbol)) {
		WriteMemberDeclarationName(*member, writer, formattingPolicy);
	} else {
		writer->WriteIdentifier(Syntax::Identifier::Create(symbol.Name()));
	}

	if (ShowParameterList(symbol)) {
		writer->WriteToken(symbol.SymbolKind() == TS::SymbolKind::Indexer ? "[" : "(");
		bool first = true;
		// The C# `IEnumerable<ParameterDeclaration> parameters` -- the three source arms
		// materialize to the eager snapshot vector the port's visitor-facing helpers take.
		std::vector<Syntax::ParameterDeclaration*> parameters;
		// The C# `symbol is IProperty { SymbolKind: SymbolKind.Property }` pattern: BOTH
		// the type test and the kind check (computed once, ahead of the chain, so a failed
		// cast falls to the GetChildren arm exactly as the failed pattern does).
		const TS::IProperty* parameterizedProperty =
			symbol.SymbolKind() == TS::SymbolKind::Property
			? dynamic_cast<const TS::IProperty*>(&symbol) : nullptr;
		if (extensionGroup.Marker != nullptr) {
			// DEFERRED (the C# `extensionGroup.Marker.Specialize(subst).Parameters.Select(...)`
			// arm): the marker is populated only by the DEFERRED
			// `ExtensionInfo.IsExtensionMarkerType` query (see IsExtension); through the
			// ported surface the Marker is always null, so this arm never fires and the
			// two live arms below carry the flow -- the faithful fallback for every
			// port-reachable shape (the deferred-arm convention).
		} else if (parameterizedProperty != nullptr) {
			// C# property syntax has no parameter list, so the converted node carries
			// none; parameterized properties take theirs from the symbol.
			for (const auto* p : parameterizedProperty->Parameters())
				parameters.push_back(astBuilder.ConvertParameter(*p));
		} else {
			auto& collection = node->GetChildren(&Syntax::Slots::Parameter);
			for (int i = 0; i < collection.Count(); i++)
				parameters.push_back(collection.At(i));
		}
		for (auto* param : parameters) {
			if ((ConversionFlags() & CF::ShowParameterModifiers) == CF::None) {
				param->ParameterModifier(TS::ReferenceKind::None);
				param->IsScopedRef(false);
				param->IsParams(false);
			}
			if ((ConversionFlags() & CF::ShowParameterDefaultValues) == CF::None) {
				if (auto* defaultExpression = param->DefaultExpression())
					Syntax::Detach(defaultExpression);
			}
			if (first) {
				first = false;
			} else {
				writer->WriteToken(",");
				writer->Space();
			}
			CSharpOutputVisitor visitor(writer, formattingPolicy);
			param->AcceptVisitor(visitor);
		}
		writer->WriteToken(symbol.SymbolKind() == TS::SymbolKind::Indexer ? "]" : ")");
	}

	// The C# return-type-after-the-parameter-list arm (`C(int a) : int`), gated on
	// PlaceReturnTypeAfterParameterList AND ShowReturnType.
	if ((ConversionFlags() & CF::PlaceReturnTypeAfterParameterList) == CF::PlaceReturnTypeAfterParameterList
		&& (ConversionFlags() & CF::ShowReturnType) == CF::ShowReturnType) {
		if (auto* rt = node->GetChildByKind(&Syntax::Slots::Type)) {
			writer->Space();
			writer->WriteToken(":");
			writer->Space();
			// DEFERRED (the C# `symbol is IField f && CSharpDecompiler.IsFixedField(f, out
			// var type, out int elementCount)` arm): `IsFixedField` belongs to the
			// unported CSharpDecompiler metadata layer; a fixed buffer field would render
			// its `fixed[elementCount]` indexer shape here. The port renders the return
			// type directly -- faithful for every non-fixed field (`IsFixedField` returns
			// false there, the same else arm), and no ported `IField` implementation is a
			// fixed field, so the divergence is unreachable through the current surface.
			CSharpOutputVisitor visitor(writer, formattingPolicy);
			rt->AcceptVisitor(visitor);
		}
	}

	// The C# ShowBody block: NOTE the `writer.EndNode(node)` lives INSIDE this block --
	// with ShowBody off the ambience never calls EndNode (a C# quirk ported faithfully).
	if ((ConversionFlags() & CF::ShowBody) == CF::ShowBody
		&& dynamic_cast<TypeDeclaration*>(node) == nullptr) {
		if (auto* property = dynamic_cast<const TS::IProperty*>(&symbol)) {
			writer->Space();
			writer->WriteToken("{");
			writer->Space();
			if (property->CanGet()) {
				writer->WriteKeyword("get");
				writer->WriteToken(";");
				writer->Space();
			}
			if (property->CanSet()) {
				// The C# `property.Setter.IsInitOnly` deref is null-safe by the
				// `[MemberNotNullWhen]` contract; the port guards the degenerate stub shape
				// (a CanSet property with a null Setter would NRE in C# -- the guard falls
				// to the `set` spelling, the D516 safe-fallback convention).
				if ((ConversionFlags() & CF::SupportInitAccessors) != CF::None
					&& property->Setter() != nullptr && property->Setter()->IsInitOnly()) {
					writer->WriteKeyword("init");
				} else {
					writer->WriteKeyword("set");
				}
				writer->WriteToken(";");
				writer->Space();
			}
			writer->WriteToken("}");
		} else {
			writer->WriteToken(";");
		}
		writer->EndNode(node);
	}
}

// The C# `bool IsExtension(ITypeDefinition? typeDef, out (IMethod Marker,
// IReadOnlyList<ITypeParameter> TypeParameters) extensionGroup)` (line 201).
bool CSharpAmbience::IsExtension(const TS::ITypeDefinition* typeDef,
                                 ExtensionGroup& extensionGroup) const {
	extensionGroup = ExtensionGroup{};
	if ((ConversionFlags() & CF::SupportExtensionDeclarations) == CF::None)
		return false;
	if (typeDef == nullptr)
		return false;
	const TS::ExtensionInfo* extensionInfo = nullptr;
	if (const TS::ITypeDefinition* declaringType = typeDef->DeclaringTypeDefinition()) {
		extensionInfo = declaringType->ExtensionInfo();
		if (extensionInfo == nullptr) {
			if (const TS::ITypeDefinition* declaringDeclaringType =
				declaringType->DeclaringTypeDefinition())
				extensionInfo = declaringDeclaringType->ExtensionInfo();
		}
	}
	// DEFERRED (the C# `return extensionInfo != null &&
	// extensionInfo.IsExtensionMarkerType(typeDef, out extensionGroup)`):
	// `ExtensionInfo.IsExtensionMarkerType` belongs to the long-pole `ExtensionInfo` class
	// (the Util-adjacent MetadataModule pair, forward-declared at ITypeDefinition.hpp).
	// Every `ITypeDefinition` implementation the port carries returns nullptr from
	// `ExtensionInfo()`, so the C# `!= null` test fails for every port-reachable shape and
	// `IsExtensionMarkerType` never runs -- returning false is exactly what the C#
	// produces for those shapes. When the metadata layer lands a non-null `ExtensionInfo`,
	// wire the marker-type query here (the deferred-arm-yields-faithful-fallback
	// convention).
	(void)extensionInfo;
	return false;
}

// The C# `bool ShowParameterList(ISymbol e)` (line 215) -- the `when`-clause switch. The
// C# `case X when cond:` arms port as a switch with the guards inline: the two
// TypeDefinition `when` clauses evaluate in order (a non-delegate, non-extension type
// definition falls to the C# `default: return false`), and the Property `when` clause
// requires a non-empty parameter list.
bool CSharpAmbience::ShowParameterList(const TS::ISymbol& e) const {
	switch (e.SymbolKind()) {
		case TS::SymbolKind::TypeDefinition: {
			const auto* typeDef = dynamic_cast<const TS::ITypeDefinition*>(&e);
			// `case SymbolKind.TypeDefinition when ((ITypeDefinition)e).Kind is TypeKind.Delegate:`
			if (typeDef != nullptr && typeDef->Kind() == TS::TypeKind::Delegate)
				return (ConversionFlags() & CF::ShowParameterList) != CF::None;
			// `case SymbolKind.TypeDefinition when IsExtension((ITypeDefinition)e, out _):`
			ExtensionGroup group;
			if (IsExtension(typeDef, group))
				return (ConversionFlags() & CF::SupportExtensionDeclarations) != CF::None;
			// Neither `when` clause matched -- the C# `default: return false`.
			return false;
		}
		case TS::SymbolKind::Indexer:
		case TS::SymbolKind::Method:
		case TS::SymbolKind::Operator:
		case TS::SymbolKind::Constructor:
		case TS::SymbolKind::Destructor:
			return (ConversionFlags() & CF::ShowParameterList) != CF::None;
		case TS::SymbolKind::Property:
			// `case SymbolKind.Property when ((IProperty)e).Parameters.Count > 0:` -- a
			// zero-parameter property misses the clause and falls to `default`.
			return dynamic_cast<const TS::IProperty&>(e).Parameters().size() > 0
				&& ((ConversionFlags() & CF::ShowParameterList) != CF::None);
		default:
			return false;
	}
}

// The C# `TypeSystemAstBuilder CreateAstBuilder()` (line 232) -- the per-flag builder
// factory. NOTE the C# leaves `ShowConstantValues` at its TRUE default (the builder's
// InitProperties), so parameter default expressions render through the ambience unless
// `ShowParameterDefaultValues` is off (the Detach arm strips them) -- a load-bearing
// quirk the tests pin.
TypeSystemAstBuilder CSharpAmbience::CreateAstBuilder() const {
	TypeSystemAstBuilder astBuilder;
	astBuilder.AddResolveResultAnnotations() = true;
	astBuilder.ShowTypeParametersForUnboundTypes() = true;
	astBuilder.ShowModifiers() =
		(ConversionFlags() & CF::ShowModifiers) == CF::ShowModifiers;
	astBuilder.ShowAccessibility() =
		(ConversionFlags() & CF::ShowAccessibility) == CF::ShowAccessibility;
	astBuilder.UsePrivateProtectedAccessibility() =
		(ConversionFlags() & CF::UsePrivateProtectedAccessibility) == CF::UsePrivateProtectedAccessibility;
	astBuilder.AlwaysUseShortTypeNames() =
		(ConversionFlags() & CF::UseFullyQualifiedTypeNames) != CF::UseFullyQualifiedTypeNames;
	astBuilder.ShowParameterNames() =
		(ConversionFlags() & CF::ShowParameterNames) == CF::ShowParameterNames;
	astBuilder.UseNullableSpecifierForValueTypes() =
		(ConversionFlags() & CF::UseNullableSpecifierForValueTypes) != CF::None;
	astBuilder.SupportInitAccessors() =
		(ConversionFlags() & CF::SupportInitAccessors) != CF::None;
	astBuilder.SupportRecordClasses() =
		(ConversionFlags() & CF::SupportRecordClasses) != CF::None;
	astBuilder.SupportRecordStructs() =
		(ConversionFlags() & CF::SupportRecordStructs) != CF::None;
	astBuilder.SupportUnsignedRightShift() =
		(ConversionFlags() & CF::SupportUnsignedRightShift) != CF::None;
	astBuilder.SupportOperatorChecked() =
		(ConversionFlags() & CF::SupportOperatorChecked) != CF::None;
	astBuilder.SupportExtensionDeclarations() =
		(ConversionFlags() & CF::SupportExtensionDeclarations) != CF::None;
	return astBuilder;
}

// The C# `void WriteTypeDeclarationName(ITypeDefinition typeDef, TokenWriter writer,
// CSharpFormattingOptions formattingPolicy)` (line 279).
void CSharpAmbience::WriteTypeDeclarationName(const TS::ITypeDefinition& typeDef, TokenWriter* writer,
                                              CSharpFormattingOptions formattingPolicy) const {
	TypeSystemAstBuilder astBuilder = CreateAstBuilder();
	EntityDeclaration* node = astBuilder.ConvertEntity(typeDef);
	if (typeDef.DeclaringTypeDefinition() != nullptr
		&& (((ConversionFlags() & CF::ShowDeclaringType) == CF::ShowDeclaringType)
			|| ((ConversionFlags() & CF::UseFullyQualifiedEntityNames) == CF::UseFullyQualifiedEntityNames))) {
		WriteTypeDeclarationName(*typeDef.DeclaringTypeDefinition(), writer, formattingPolicy);
		writer->WriteToken(".");
	} else if ((ConversionFlags() & CF::UseFullyQualifiedEntityNames) == CF::UseFullyQualifiedEntityNames) {
		// The C# `!string.IsNullOrEmpty(typeDef.Namespace)` -- an empty namespace writes
		// no prefix.
		if (!typeDef.Namespace().empty()) {
			WriteQualifiedName(typeDef.Namespace(), writer, formattingPolicy);
			writer->WriteToken(".");
		}
	}
	ExtensionGroup group;
	if (IsExtension(&typeDef, group)) {
		writer->WriteKeyword("extension");
		std::vector<Syntax::TypeParameterDeclaration*> typeParameters;
		for (const auto* tp : group.TypeParameters)
			typeParameters.push_back(astBuilder.ConvertTypeParameter(*tp));
		WriteTypeParameters(typeParameters, writer, formattingPolicy);
	} else {
		writer->WriteIdentifier(node->NameToken());
		std::vector<Syntax::TypeParameterDeclaration*> typeParameters;
		auto& collection = node->GetChildren(&Syntax::Slots::TypeParameter);
		for (int i = 0; i < collection.Count(); i++)
			typeParameters.push_back(collection.At(i));
		WriteTypeParameters(typeParameters, writer, formattingPolicy);
	}
}

// The C# `void WriteMemberDeclarationName(IMember member, TokenWriter writer,
// CSharpFormattingOptions formattingPolicy)` (line 316).
void CSharpAmbience::WriteMemberDeclarationName(const TS::IMember& member, TokenWriter* writer,
                                                CSharpFormattingOptions formattingPolicy) const {
	TypeSystemAstBuilder astBuilder = CreateAstBuilder();
	EntityDeclaration* node = astBuilder.ConvertEntity(member);
	if ((ConversionFlags() & CF::ShowDeclaringType) == CF::ShowDeclaringType
		&& member.DeclaringType() != nullptr
		&& dynamic_cast<const TS::Implementation::LocalFunctionMethod*>(&member) == nullptr) {
		ConvertType(*member.DeclaringType(), writer, formattingPolicy);
		writer->WriteToken(".");
	}
	TS::ITypePtr explicitInterfaceType = GetExplicitInterfaceType(member);
	std::string name = member.Name();
	if (explicitInterfaceType != nullptr) {
		// The C# `name.Substring(name.LastIndexOf('.') + 1)` -- a name with no dot yields
		// the whole name (LastIndexOf's -1 maps to the Substring(0) start).
		std::size_t lastDot = name.rfind('.');
		name = name.substr(lastDot == std::string::npos ? 0 : lastDot + 1);
	}
	switch (member.SymbolKind()) {
		case TS::SymbolKind::Indexer:
			if (explicitInterfaceType != nullptr) {
				ConvertType(*explicitInterfaceType, writer, formattingPolicy);
				writer->WriteToken(".");
			}
			writer->WriteKeyword("this");
			break;
		case TS::SymbolKind::Constructor:
			// The C# `member.DeclaringType!.Name` deref is guarded for the degenerate
			// stub shape (a null DeclaringType would NRE in C#; the guard writes nothing,
			// the D516 safe-fallback convention).
			if (member.DeclaringType() != nullptr)
				WriteQualifiedName(member.DeclaringType()->Name(), writer, formattingPolicy);
			break;
		case TS::SymbolKind::Destructor:
			writer->WriteToken("~");
			if (member.DeclaringType() != nullptr)
				WriteQualifiedName(member.DeclaringType()->Name(), writer, formattingPolicy);
			break;
		case TS::SymbolKind::Operator:
			if (name == "op_Implicit") {
				writer->WriteKeyword("implicit");
				writer->Space();
				if (explicitInterfaceType != nullptr) {
					ConvertType(*explicitInterfaceType, writer, formattingPolicy);
					writer->WriteToken(".");
				}
				writer->WriteKeyword("operator");
				writer->Space();
				ConvertType(member.ReturnType(), writer, formattingPolicy);
			} else if (name == "op_Explicit" || name == "op_CheckedExplicit") {
				writer->WriteKeyword("explicit");
				writer->Space();
				if (explicitInterfaceType != nullptr) {
					ConvertType(*explicitInterfaceType, writer, formattingPolicy);
					writer->WriteToken(".");
				}
				writer->WriteKeyword("operator");
				writer->Space();
				if (name == "op_CheckedExplicit") {
					writer->WriteToken("checked");
					writer->Space();
				}
				ConvertType(member.ReturnType(), writer, formattingPolicy);
			} else {
				if (explicitInterfaceType != nullptr) {
					ConvertType(*explicitInterfaceType, writer, formattingPolicy);
					writer->WriteToken(".");
				}
				writer->WriteKeyword("operator");
				writer->Space();
				auto operatorType = Syntax::OperatorDeclaration::GetOperatorType(name);
				if (operatorType.has_value()
					&& !((ConversionFlags() & CF::SupportOperatorChecked) == CF::None
						&& Syntax::OperatorDeclaration::IsChecked(*operatorType))) {
					if (Syntax::OperatorDeclaration::IsChecked(*operatorType)) {
						writer->WriteToken("checked");
						writer->Space();
					}
					writer->WriteToken(Syntax::OperatorDeclaration::GetToken(*operatorType));
				} else {
					// The unsupported-operator fallback writes the rendered node's own
					// name token (a `checked` operator with the flag off, or a name the
					// table does not know).
					writer->WriteIdentifier(node->NameToken());
				}
			}
			break;
		default:
			if (explicitInterfaceType != nullptr) {
				ConvertType(*explicitInterfaceType, writer, formattingPolicy);
				writer->WriteToken(".");
			}
			writer->WriteIdentifier(Syntax::Identifier::Create(name));
			break;
	}
	std::vector<Syntax::TypeParameterDeclaration*> typeParameters;
	auto& collection = node->GetChildren(&Syntax::Slots::TypeParameter);
	for (int i = 0; i < collection.Count(); i++)
		typeParameters.push_back(collection.At(i));
	WriteTypeParameters(typeParameters, writer, formattingPolicy);
}

// The C# `void WriteTypeParameters(IEnumerable<TypeParameterDeclaration> typeParameters,
// TokenWriter writer, CSharpFormattingOptions formattingPolicy)` (line 401). The C#
// `RemoveVarianceModifier` local function mutates the declarations in place (the LINQ
// Select re-yields the same mutated objects), so the port copies the snapshot and strips
// the variance on each element when the variance flag is off.
void CSharpAmbience::WriteTypeParameters(
	const std::vector<Syntax::TypeParameterDeclaration*>& typeParameters, TokenWriter* writer,
	CSharpFormattingOptions formattingPolicy) const {
	if ((ConversionFlags() & CF::ShowTypeParameterList) == CF::ShowTypeParameterList) {
		std::vector<Syntax::TypeParameterDeclaration*> list = typeParameters;
		if ((ConversionFlags() & CF::ShowTypeParameterVarianceModifier) == CF::None) {
			for (auto* decl : list)
				decl->Variance(TS::VarianceModifier::Invariant);
		}
		CSharpOutputVisitor outputVisitor(writer, formattingPolicy);
		outputVisitor.WriteTypeParameters(list);
	}
}

// The C# `void PrintModifiers(Modifiers modifiers, TokenWriter writer)` (line 422) --
// the modifiers in the `CSharpModifiers.AllModifiers` output order.
void CSharpAmbience::PrintModifiers(Syntax::Modifiers modifiers, TokenWriter* writer) const {
	for (auto m : Syntax::CSharpModifiers::AllModifiers) {
		if ((modifiers & m) == m) {
			writer->WriteKeyword(Syntax::CSharpModifiers::GetModifierName(m));
			writer->Space();
		}
	}
}

// The C# `void WriteQualifiedName(string name, TokenWriter writer,
// CSharpFormattingOptions formattingPolicy)` (line 433) -- the `AstType.Create` dotted
// chain rendered through a fresh output visitor.
void CSharpAmbience::WriteQualifiedName(const std::string& name, TokenWriter* writer,
                                        CSharpFormattingOptions formattingPolicy) const {
	Syntax::AstType* node = Syntax::AstType::Create(name);
	CSharpOutputVisitor outputVisitor(writer, formattingPolicy);
	node->AcceptVisitor(outputVisitor);
}

// The C# `public string ConvertVariable(IVariable v)` (line 440) -- the C#
// `TrimEnd(';', '\r', '\n', (char)8232)`: strip the trailing statement terminator and
// line breaks (8232 == U+2028 LINE SEPARATOR, whose UTF-8 encoding is the 3-byte
// sequence 0xE2 0x80 0xA8 handled below).
std::string CSharpAmbience::ConvertVariable(const TS::IVariable& v) const {
	TypeSystemAstBuilder astBuilder = CreateAstBuilder();
	AstNode* astNode = astBuilder.ConvertVariable(v);
	std::string text = astNode->ToString();
	std::size_t end = text.size();
	while (end > 0) {
		char c = text[end - 1];
		if (c == ';' || c == '\r' || c == '\n') {
			end--;
			continue;
		}
		if (end >= 3
			&& static_cast<unsigned char>(text[end - 3]) == 0xE2
			&& static_cast<unsigned char>(text[end - 2]) == 0x80
			&& static_cast<unsigned char>(text[end - 1]) == 0xA8) {
			end -= 3;
			continue;
		}
		break;
	}
	return text.substr(0, end);
}

// The C# `public string ConvertType(IType type)` (line 448). NOTE the flag: the C# maps
// `UseFullyQualifiedEntityNames` (NOT the `UseFullyQualifiedTypeNames` bit
// `CreateAstBuilder` maps) -- a C# quirk ported faithfully.
std::string CSharpAmbience::ConvertType(const TS::IType& type) {
	TypeSystemAstBuilder astBuilder = CreateAstBuilder();
	astBuilder.AlwaysUseShortTypeNames() =
		(ConversionFlags() & CF::UseFullyQualifiedEntityNames) != CF::UseFullyQualifiedEntityNames;
	Syntax::AstType* astType = astBuilder.ConvertType(const_cast<TS::IType&>(type));
	return astType->ToString();
}

// The C# private `void ConvertType(IType type, TokenWriter writer,
// CSharpFormattingOptions formattingPolicy)` (line 461).
void CSharpAmbience::ConvertType(const TS::IType& type, TokenWriter* writer,
                                  CSharpFormattingOptions formattingPolicy) const {
	TypeSystemAstBuilder astBuilder = CreateAstBuilder();
	astBuilder.AlwaysUseShortTypeNames() =
		(ConversionFlags() & CF::UseFullyQualifiedEntityNames) != CF::UseFullyQualifiedEntityNames;
	Syntax::AstType* astType = astBuilder.ConvertType(const_cast<TS::IType&>(type));
	CSharpOutputVisitor outputVisitor(writer, formattingPolicy);
	astType->AcceptVisitor(outputVisitor);
}

// The C# `IType? GetExplicitInterfaceType(IMember member)` (line 472) -- the
// `FirstOrDefault` over the explicitly-implemented interface members maps to the
// non-empty check + front read.
TS::ITypePtr CSharpAmbience::GetExplicitInterfaceType(const TS::IMember& member) const {
	if (member.IsExplicitInterfaceImplementation()) {
		auto members = member.ExplicitlyImplementedInterfaceMembers();
		if (!members.empty())
			return members.front()->DeclaringType();
	}
	return nullptr;
}

// The C# `public string ConvertConstantValue(object constantValue)` (line 482).
std::string CSharpAmbience::ConvertConstantValue(const Syntax::PrimitiveValue& constantValue) {
	return TextWriterTokenWriter::PrintPrimitiveValue(constantValue);
}

// The C# `public string WrapComment(string comment)` (line 487).
std::string CSharpAmbience::WrapComment(std::string_view comment) {
	return "// " + std::string(comment);
}

} // namespace ILSpy::Decompiler::CSharp::OutputVisitor
