// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The ILAmbience implementation (the header carries the port decisions). The
// file-local pieces below are: the UTF-8 decode + the UTF-16 character
// classifications EscapeName consumes (the TextWriterTokenWriter DecodeUtf8
// shape, copied next to its second consumer); the FullNameOf/NamespaceOf
// IType-surface reads the C# `type.FullName` needs (the TypeSystemAstBuilder.cpp
// file-local helpers, whose C# home is the same AbstractType.FullName /
// MetadataTypeDefinition.Namespace pair -- the port's minimal IType carries
// neither, so the definitions route through IEntity / ParameterizedType /
// UnknownType); and the TypeToStringVisitor (the C# nested class, lifted to
// namespace scope and kept .cpp-internal -- the BaseListNameabilityVisitor
// precedent).

#include "Decompiler/IL/ILAmbience.hpp"

#include "Decompiler/Disassembler/EnumNameCollection.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::IL {

// The `UnknownType` class/function name collision (the elaborated-type-specifier
// fix): a file-scope using-declaration imports the whole name set so the
// `class UnknownType` elaborated form in NamespaceOf's dynamic_cast finds the
// class instead of declaring a local class (the TypeSystemAstBuilder.cpp
// precedent).
using TypeSystem::UnknownType;

// ---------------------------------------------------------------------------
// File-local UTF-8 / character-classification helpers for EscapeName.
// ---------------------------------------------------------------------------

namespace {

// Decode one UTF-8 sequence in `str` starting at `index` to a code point; returns
// the code point and the number of bytes consumed. For an invalid/overlong/
// surrogate sequence, returns the offending lead byte as a code point and
// consumes 1 byte. (The TextWriterTokenWriter detail::DecodeUtf8 shape -- the
// port's text convention is UTF-8 where the C# iterates UTF-16 code units.)
std::pair<char32_t, std::size_t> DecodeUtf8(std::string_view str, std::size_t index) {
	if (index >= str.size())
		return {char32_t(0), 0};
	unsigned char b0 = static_cast<unsigned char>(str[index]);
	if (b0 < 0x80)
		return {char32_t(b0), 1};
	char32_t cp = 0;
	std::size_t n = 0;
	if ((b0 & 0xE0) == 0xC0) { n = 2; cp = char32_t(b0 & 0x1F); }
	else if ((b0 & 0xF0) == 0xE0) { n = 3; cp = char32_t(b0 & 0x0F); }
	else if ((b0 & 0xF8) == 0xF0) { n = 4; cp = char32_t(b0 & 0x07); }
	else
		return {char32_t(b0), 1};
	for (std::size_t i = 1; i < n; ++i) {
		if (index + i >= str.size())
			return {char32_t(b0), 1};
		unsigned char b = static_cast<unsigned char>(str[index + i]);
		if ((b & 0xC0) != 0x80)
			return {char32_t(b0), 1};
		cp = (cp << 6) | char32_t(b & 0x3F);
	}
	if (n == 2 && cp < 0x80) return {char32_t(b0), 1};           // overlong
	if (n == 3 && cp < 0x800) return {char32_t(b0), 1};          // overlong
	if (n == 3 && cp >= 0xD800 && cp <= 0xDFFF) return {char32_t(b0), 1};  // surrogate
	if (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return {char32_t(b0), 1};  // out of range
	return {cp, n};
}

// The C# `char.IsWhiteSpace(char)` -- the exact .NET Framework UTF-16 code-unit
// set: the ASCII set 0x09-0x0D, 0x20, plus the separators 0x85, 0xA0, 0x1680,
// the 0x2000-0x200A block, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000.
bool IsUtf16WhiteSpace(char32_t ch) {
	if (ch >= 0x2000 && ch <= 0x200A)
		return true;
	switch (ch) {
	case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D: case 0x0020:
	case 0x0085: case 0x00A0: case 0x1680: case 0x2028: case 0x2029:
	case 0x202F: case 0x205F: case 0x3000:
		return true;
	default:
		return false;
	}
}

// The C# `char.IsControl(char)` -- 0x0000-0x001F and 0x007F-0x009F.
bool IsUtf16Control(char32_t ch) {
	return ch <= 0x001F || (ch >= 0x007F && ch <= 0x009F);
}

// The C# `char.IsSurrogate(char)` -- the UTF-16 surrogate block 0xD800-0xDFFF.
// (A valid UTF-8 decode never yields a code point in this block; the check
// keeps the C# classification set complete.)
bool IsUtf16Surrogate(char32_t ch) {
	return ch >= 0xD800 && ch <= 0xDFFF;
}

// The C# `sb.AppendFormat("\\u{0:x4}", (int)ch)` -- the `\u` + lowercase hex,
// zero-padded to 4 digits.
void AppendUnicodeEscape4(std::string& sb, std::uint32_t codeUnit) {
	char buf[8];
	std::snprintf(buf, sizeof buf, "\\u%04x", codeUnit);
	sb.append(buf);
}

// The C# `AbstractType.FullName` (`Namespace.IsNullOrEmpty() ? Name :
// Namespace + "." + Name`) for any `IType` -- the port's minimal `IType`
// surface carries neither `FullName` nor `Namespace`, so the namespace routes
// through the definition (the TypeSystemAstBuilder.cpp `NamespaceOf` shape:
// an entity reads its own namespace, a parameterized type its generic's, an
// `UnknownType` its stored full-name namespace; everything else -- the
// non-definition minimal-port types -- has the `AbstractType` empty default).
std::string NamespaceOf(const TypeSystem::IType& type) {
	if (const auto* entity = dynamic_cast<const TypeSystem::IEntity*>(&type))
		return entity->Namespace();
	if (const auto* pt = dynamic_cast<const TypeSystem::ParameterizedType*>(&type))
		return pt->GenericType() ? NamespaceOf(*pt->GenericType()) : std::string();
	if (const auto* unknown = dynamic_cast<const class TypeSystem::UnknownType*>(&type))
		return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
	return std::string();
}

std::string FullNameOf(const TypeSystem::IType& type) {
	const std::string ns = NamespaceOf(type);
	if (ns.empty())
		return type.Name();
	return ns + "." + type.Name();
}

// ---------------------------------------------------------------------------
// TypeToStringVisitor (ILAmbience.cs lines 311-502, the C# private nested
// class, lifted to namespace scope and kept .cpp-internal -- the
// BaseListNameabilityVisitor precedent). `ConvertType` drives it; the
// overrides mirror the C# `return type` reference semantics through
// `shared_from_this` (no override ever returns a changed type, so the
// port's base-vs-original distinction is unreachable).
// ---------------------------------------------------------------------------
class TypeToStringVisitor final : public TypeSystem::TypeVisitor {
public:
	explicit TypeToStringVisitor(Output::ConversionFlags flags) : flags_(flags) {}

	// The C# `public override string ToString()` -- the built text.
	std::string ToString() const { return builder_; }

	TypeSystem::ITypePtr VisitArrayType(TypeSystem::ArrayType& type) override {
		// The C# `base.VisitArrayType(type)` visits the element through this
		// visitor first (the element's rendering lands before the `[...]`).
		const auto visited = TypeSystem::TypeVisitor::VisitArrayType(type);
		builder_ += '[';
		if (type.Rank() > 1)
			builder_.append(static_cast<std::size_t>(type.Rank() - 1), ',');
		builder_ += ']';
		return visited;
	}

	TypeSystem::ITypePtr VisitByReferenceType(TypeSystem::ByReferenceType& type) override {
		const auto visited = TypeSystem::TypeVisitor::VisitByReferenceType(type);
		builder_ += '&';
		return visited;
	}

	TypeSystem::ITypePtr VisitModOpt(TypeSystem::ModifiedType& type) override {
		// The C# visits the element directly (no base call): the element
		// renders, then the ` modopt(<modifier>)` suffix.
		if (type.Element())
			type.Element()->AcceptVisitor(*this);
		builder_ += " modopt(";
		if (type.Modifier())
			type.Modifier()->AcceptVisitor(*this);
		builder_ += ')';
		return type.shared_from_this();
	}

	TypeSystem::ITypePtr VisitModReq(TypeSystem::ModifiedType& type) override {
		if (type.Element())
			type.Element()->AcceptVisitor(*this);
		builder_ += " modreq(";
		if (type.Modifier())
			type.Modifier()->AcceptVisitor(*this);
		builder_ += ')';
		return type.shared_from_this();
	}

	TypeSystem::ITypePtr VisitPointerType(TypeSystem::PointerType& type) override {
		const auto visited = TypeSystem::TypeVisitor::VisitPointerType(type);
		builder_ += '*';
		return visited;
	}

	TypeSystem::ITypePtr VisitTypeParameter(TypeSystem::ITypeParameter& type) override {
		const auto visited = TypeSystem::TypeVisitor::VisitTypeParameter(type);
		ILAmbience::EscapeName(builder_, type.Name());
		return visited;
	}

	TypeSystem::ITypePtr VisitParameterizedType(TypeSystem::ParameterizedType& type) override {
		if (type.GenericType())
			type.GenericType()->AcceptVisitor(*this);
		builder_ += '<';
		const auto& typeArguments = type.TypeArguments();
		for (std::size_t i = 0; i < typeArguments.size(); i++) {
			if (i > 0)
				builder_ += ',';
			if (typeArguments[i])
				typeArguments[i]->AcceptVisitor(*this);
		}
		builder_ += '>';
		return type.shared_from_this();
	}

	TypeSystem::ITypePtr VisitTupleType(TypeSystem::TupleType& type) override {
		// A tuple renders its underlying `System.ValueTuple<...>` chain.
		if (type.UnderlyingType())
			type.UnderlyingType()->AcceptVisitor(*this);
		return type.shared_from_this();
	}

	TypeSystem::ITypePtr VisitFunctionPointerType(TypeSystem::FunctionPointerType& type) override {
		builder_ += "method ";
		if (type.CallingConvention() != TypeSystem::SignatureCallingConvention::Default) {
			builder_ += TypeSystem::ToILSyntax(type.CallingConvention());
			builder_ += ' ';
		}
		if (type.ReturnType())
			type.ReturnType()->AcceptVisitor(*this);
		builder_ += " *(";
		bool first = true;
		for (const auto& parameterType : type.ParameterTypes()) {
			if (first)
				first = false;
			else
				builder_ += ", ";
			if (parameterType)
				parameterType->AcceptVisitor(*this);
		}
		builder_ += ')';
		return type.shared_from_this();
	}

	TypeSystem::ITypePtr VisitOtherType(TypeSystem::IType& type) override {
		WriteType(type);
		return type.shared_from_this();
	}

	TypeSystem::ITypePtr VisitTypeDefinition(TypeSystem::ITypeDefinition& type) override {
		switch (type.KnownTypeCode()) {
		case TypeSystem::KnownTypeCode::Object:
			builder_ += "object";
			break;
		case TypeSystem::KnownTypeCode::Boolean:
			builder_ += "bool";
			break;
		case TypeSystem::KnownTypeCode::Char:
			builder_ += "char";
			break;
		case TypeSystem::KnownTypeCode::SByte:
			builder_ += "int8";
			break;
		case TypeSystem::KnownTypeCode::Byte:
			builder_ += "uint8";
			break;
		case TypeSystem::KnownTypeCode::Int16:
			builder_ += "int16";
			break;
		case TypeSystem::KnownTypeCode::UInt16:
			builder_ += "uint16";
			break;
		case TypeSystem::KnownTypeCode::Int32:
			builder_ += "int32";
			break;
		case TypeSystem::KnownTypeCode::UInt32:
			builder_ += "uint32";
			break;
		case TypeSystem::KnownTypeCode::Int64:
			builder_ += "int64";
			break;
		case TypeSystem::KnownTypeCode::UInt64:
			builder_ += "uint64";
			break;
		case TypeSystem::KnownTypeCode::Single:
			builder_ += "float32";
			break;
		case TypeSystem::KnownTypeCode::Double:
			builder_ += "float64";
			break;
		case TypeSystem::KnownTypeCode::String:
			builder_ += "string";
			break;
		case TypeSystem::KnownTypeCode::Void:
			builder_ += "void";
			break;
		case TypeSystem::KnownTypeCode::IntPtr:
			builder_ += "native int";
			break;
		case TypeSystem::KnownTypeCode::UIntPtr:
			builder_ += "native uint";
			break;
		case TypeSystem::KnownTypeCode::TypedReference:
			builder_ += "typedref";
			break;
		default:
			WriteType(type);
			break;
		}
		return type.shared_from_this();
	}

private:
	void WriteType(const TypeSystem::IType& type) {
		if ((flags_ & Output::ConversionFlags::UseFullyQualifiedTypeNames)
			== Output::ConversionFlags::UseFullyQualifiedTypeNames)
			ILAmbience::EscapeName(builder_, FullNameOf(type));
		else
			ILAmbience::EscapeName(builder_, type.Name());
		if (type.TypeParameterCount() > 0) {
			builder_ += '`';
			builder_ += std::to_string(type.TypeParameterCount());
		}
	}

	Output::ConversionFlags flags_;
	std::string builder_;
};

} // namespace

// ---------------------------------------------------------------------------
// ConvertConstantValue (ILAmbience.cs line 40) -- throws unconditionally.
// ---------------------------------------------------------------------------
std::string ILAmbience::ConvertConstantValue(const CSharp::Syntax::PrimitiveValue& constantValue)
{
	(void)constantValue;
	throw std::logic_error("ILAmbience.ConvertConstantValue is not implemented");
}

// ---------------------------------------------------------------------------
// ConvertSymbol (ILAmbience.cs lines 45-53 + the writer-driven core at 54-302).
// ---------------------------------------------------------------------------
std::string ILAmbience::ConvertSymbol(const TypeSystem::ISymbol& symbol)
{
	std::ostringstream sw;
	ConvertSymbol(sw, symbol);
	return sw.str();
}

void ILAmbience::ConvertSymbol(std::ostringstream& writer, const TypeSystem::ISymbol& symbol)
{
	const TypeSystem::IEntity* entity = dynamic_cast<const TypeSystem::IEntity*>(&symbol);
	const Metadata::MetadataFile* metadata =
		entity != nullptr && entity->ParentModule() != nullptr
			? entity->ParentModule()->MetadataFile()
			: nullptr;
	// The C# `entity?.MetadataToken ?? default` -- the raw token of the entity's
	// metadata row, or the 0 token for a non-entity.
	const std::uint32_t token = entity != nullptr ? entity->MetadataToken() : 0;

	// The C# `var output = new PlainTextOutput(writer)` -- the flag writers write
	// into the same underlying stream the literal text writes into.
	Output::PlainTextOutput output(writer);

	// When the symbol's owning module has been torn down (stale entity pinned in
	// navigation history after the assembly was unloaded/reloaded), the rest of
	// the switch can't read its metadata. Fall back to the symbol's name so
	// callers still get sensible display text -- the NRE would otherwise surface
	// up through every consumer that formats a stale history entry.
	if (metadata == nullptr) {
		writer << symbol.Name();
		return;
	}

	// The C# `switch (symbol)` -- the per-kind metadata-driven flag prefix
	// (the cases in the C# order: IField / IMethod / IProperty / IEvent /
	// ITypeDefinition).
	if (const TypeSystem::IField* f = dynamic_cast<const TypeSystem::IField*>(&symbol)) {
		if (HasFlag(CF::ShowDefinitionKeyword))
			writer << ".field ";
		// The C# `metadata.GetFieldDefinition((FieldDefinitionHandle)token)` +
		// `.Attributes` -- the port's per-row raw-flags read (II.23.1.5),
		// widened to the System.Reflection enum stand-in.
		const auto fd = static_cast<Disassembler::FieldAttributes>(
			metadata->GetFieldAttributes(token));
		if (HasFlag(CF::ShowAccessibility))
			Disassembler::WriteEnum(fd & Disassembler::FieldAttributes::FieldAccessMask,
				Disassembler::fieldVisibility, output);
		if (HasFlag(CF::ShowModifiers)) {
			// The C# local `hasXAttributes` -- the HasDefault / HasFieldMarshal /
			// HasFieldRVA bits are hidden from the flags output (the .custom /
			// at-<rva> arms render them).
			const Disassembler::FieldAttributes hasXAttributes =
				Disassembler::FieldAttributes::HasDefault
				| Disassembler::FieldAttributes::HasFieldMarshal
				| Disassembler::FieldAttributes::HasFieldRVA;
			Disassembler::WriteFlags(
				fd & ~(Disassembler::FieldAttributes::FieldAccessMask | hasXAttributes),
				Disassembler::fieldAttributes, output);
			if (!f->IsStatic()) {
				writer << "instance ";
			}
		}
	} else if (const TypeSystem::IMethod* m = dynamic_cast<const TypeSystem::IMethod*>(&symbol)) {
		if (HasFlag(CF::ShowDefinitionKeyword))
			writer << ".method ";
		const auto md = static_cast<Disassembler::MethodAttributes>(
			metadata->GetMethodAttributes(token));
		if (HasFlag(CF::ShowAccessibility))
			Disassembler::WriteEnum(md & Disassembler::MethodAttributes::MemberAccessMask,
				Disassembler::methodVisibility, output);
		if (HasFlag(CF::ShowModifiers)) {
			Disassembler::WriteFlags(md & ~Disassembler::MethodAttributes::MemberAccessMask,
				Disassembler::methodAttributeFlags, output);
			if (!m->IsStatic()) {
				writer << "instance ";
			}
		}
	} else if (const TypeSystem::IProperty* p = dynamic_cast<const TypeSystem::IProperty*>(&symbol)) {
		if (HasFlag(CF::ShowDefinitionKeyword))
			writer << ".property ";
		const auto pd = static_cast<Disassembler::PropertyAttributes>(
			metadata->GetPropertyAttributes(token));
		if (HasFlag(CF::ShowModifiers)) {
			Disassembler::WriteFlags(pd, Disassembler::propertyAttributes, output);
			if (!p->IsStatic()) {
				writer << "instance ";
			}
		}
	} else if (const TypeSystem::IEvent* e = dynamic_cast<const TypeSystem::IEvent*>(&symbol)) {
		if (HasFlag(CF::ShowDefinitionKeyword))
			writer << ".event ";
		const auto ed = static_cast<Disassembler::EventAttributes>(
			metadata->GetEventAttributes(token));
		if (HasFlag(CF::ShowModifiers)) {
			Disassembler::WriteFlags(ed, Disassembler::eventAttributes, output);
			if (!e->IsStatic()) {
				writer << "instance ";
			}
		}
	} else if (dynamic_cast<const TypeSystem::ITypeDefinition*>(&symbol) != nullptr) {
		const auto td = static_cast<Disassembler::TypeAttributes>(
			metadata->GetTypeDefAttributes(token));
		if (HasFlag(CF::ShowDefinitionKeyword)) {
			writer << ".class ";
			// The C# `td.Attributes.HasFlag(TypeAttributes.Interface)` -- the
			// ClassSemanticsMask bit.
			if ((td & Disassembler::TypeAttributes::Interface)
				== Disassembler::TypeAttributes::Interface)
				writer << "interface ";
		}
		if (HasFlag(CF::ShowAccessibility))
			Disassembler::WriteEnum(td & Disassembler::TypeAttributes::VisibilityMask,
				Disassembler::typeVisibility, output);
		const Disassembler::TypeAttributes masks =
			Disassembler::TypeAttributes::ClassSemanticsMask
			| Disassembler::TypeAttributes::VisibilityMask
			| Disassembler::TypeAttributes::LayoutMask
			| Disassembler::TypeAttributes::StringFormatMask;
		if (HasFlag(CF::ShowModifiers))
			Disassembler::WriteFlags(td & ~masks, Disassembler::typeAttributes, output);
	}

	const bool showReturnTypeBefore = HasFlag(CF::ShowReturnType)
		&& !HasFlag(CF::PlaceReturnTypeAfterParameterList);
	const bool showReturnTypeAfter = HasFlag(CF::ShowReturnType)
		&& HasFlag(CF::PlaceReturnTypeAfterParameterList);

	// The C# `symbol is IMember { SymbolKind: not SymbolKind.Constructor }` -- a
	// member whose kind is not a constructor (the type-parameter-declaration
	// shapes carry no return type). The four `case IField/IMethod/IProperty/
	// IEvent: writer.Write(ConvertType(f/m/p/e.ReturnType))` arms all read the
	// same `IMember::ReturnType()` virtual each interface re-exposes, so the
	// member-level read dispatches identically to the four-arm switch.
	const TypeSystem::IMember* member = dynamic_cast<const TypeSystem::IMember*>(&symbol);
	const bool memberNotConstructor = member != nullptr
		&& member->SymbolKind() != TypeSystem::SymbolKind::Constructor;

	if (showReturnTypeBefore && memberNotConstructor) {
		writer << ConvertType(member->ReturnType());
		writer << ' ';
	}

	// The name switch: a type definition renders through `WriteTypeDefinition`;
	// a member renders its (optionally declaring-type-prefixed) name and, for a
	// method, its own type-parameter arity.
	if (const TypeSystem::ITypeDefinition* definition =
		dynamic_cast<const TypeSystem::ITypeDefinition*>(&symbol)) {
		WriteTypeDefinition(writer, *definition);
	} else if (member != nullptr) {
		if ((HasFlag(CF::UseFullyQualifiedTypeNames) || HasFlag(CF::ShowDeclaringType))
			&& member->DeclaringTypeDefinition() != nullptr) {
			WriteTypeDefinition(writer, *member->DeclaringTypeDefinition());
			writer << "::";
		}
		writer << member->Name();
		if (const TypeSystem::IMethod* method = dynamic_cast<const TypeSystem::IMethod*>(&symbol)) {
			WriteTypeParameters(writer, method->TypeParameters(), *member);
		}
	}

	// The parameter list -- every `IParameterizedMember` except a property
	// (a C# property cannot carry parameters; a parameterized property -- an
	// indexer -- reports `SymbolKind.Indexer` and DOES render them).
	if (HasFlag(CF::ShowParameterList)) {
		const TypeSystem::IParameterizedMember* pm =
			dynamic_cast<const TypeSystem::IParameterizedMember*>(&symbol);
		if (pm != nullptr && pm->SymbolKind() != TypeSystem::SymbolKind::Property) {
			writer << '(';
			int i = 0;
			for (const TypeSystem::IParameter* parameter : pm->Parameters()) {
				if (i > 0)
					writer << ", ";
				writer << ConvertType(parameter->Type());
				if (HasFlag(CF::ShowParameterNames))
					writer << ' ' << parameter->Name();
				i++;
			}
			writer << ')';
		}
	}

	if (showReturnTypeAfter && memberNotConstructor) {
		writer << " : ";
		writer << ConvertType(member->ReturnType());
	}
}

// ---------------------------------------------------------------------------
// WriteTypeDefinition / WriteTypeParameters (the C# local functions, lines
// 179-226).
// ---------------------------------------------------------------------------
void ILAmbience::WriteTypeDefinition(std::ostringstream& writer,
	const TypeSystem::ITypeDefinition& typeDef)
{
	if ((HasFlag(CF::UseFullyQualifiedEntityNames) || HasFlag(CF::ShowDeclaringType))
		&& typeDef.DeclaringTypeDefinition() != nullptr) {
		WriteTypeDefinition(writer, *typeDef.DeclaringTypeDefinition());
		writer << '.';
	} else if (HasFlag(CF::UseFullyQualifiedEntityNames) && !typeDef.Namespace().empty()) {
		writer << typeDef.Namespace();
		writer << '.';
	}
	writer << typeDef.Name();
	WriteTypeParameters(writer, typeDef.TypeParameters(), typeDef);
}

void ILAmbience::WriteTypeParameters(std::ostringstream& writer,
	const std::vector<const ITypeParameter*>& typeParameters, const IEntity& owner)
{
	if (typeParameters.empty())
		return;
	// The C# `owner.DeclaringTypeDefinition?.TypeParameterCount ?? 0` -- the
	// outer type's declared count is subtracted so a member of a generic type
	// reports only its OWN arity.
	const int ownerCount = owner.DeclaringTypeDefinition() != nullptr
		? owner.DeclaringTypeDefinition()->TypeParameterCount()
		: 0;
	const int typeParameterCount = static_cast<int>(typeParameters.size()) - ownerCount;
	if (typeParameterCount > 0) {
		// The C# `switch (owner)` -- a type's own arity takes one backtick, a
		// method's two (the IL ``2 spelling).
		if (dynamic_cast<const TypeSystem::IType*>(&owner) != nullptr)
			writer << '`';
		else if (dynamic_cast<const TypeSystem::IMethod*>(&owner) != nullptr)
			writer << "``";
		writer << typeParameterCount;
	}

	if (HasFlag(CF::ShowTypeParameterList)) {
		int i = 0;
		writer << '<';
		for (const ITypeParameter* tp : typeParameters) {
			if (i > 0)
				writer << ',';
			if (HasFlag(CF::ShowTypeParameterVarianceModifier)) {
				switch (tp->Variance()) {
				case TypeSystem::VarianceModifier::Covariant:
					writer << '+';
					break;
				case TypeSystem::VarianceModifier::Contravariant:
					writer << '-';
					break;
				default:
					break;
				}
			}
			writer << tp->Name();
			i++;
		}
		writer << '>';
	}
}

// ---------------------------------------------------------------------------
// ConvertType (ILAmbience.cs line 304) -- the TypeToStringVisitor drive.
// ---------------------------------------------------------------------------
std::string ILAmbience::ConvertType(const TypeSystem::IType& type)
{
	TypeToStringVisitor visitor(conversionFlags_);
	// The port's `IType::AcceptVisitor` is non-const (D406); the interface's
	// const reference is cast away at the visit call site (the D515 convention --
	// the underlying type-system objects are mutable, the accessor's const is
	// the contract).
	const_cast<TypeSystem::IType&>(type).AcceptVisitor(visitor);
	return visitor.ToString();
}

// ---------------------------------------------------------------------------
// WrapComment (ILAmbience.cs line 504).
// ---------------------------------------------------------------------------
std::string ILAmbience::WrapComment(std::string_view comment)
{
	return "// " + std::string(comment);
}

// ---------------------------------------------------------------------------
// EscapeName (ILAmbience.cs lines 512-531).
// ---------------------------------------------------------------------------
std::string& ILAmbience::EscapeName(std::string& sb, std::string_view name)
{
	std::size_t i = 0;
	while (i < name.size()) {
		const auto decoded = DecodeUtf8(name, i);
		const char32_t cp = decoded.first;
		const std::size_t n = decoded.second;
		if (IsUtf16Surrogate(cp)) {
			// A decoded code point in the surrogate block (an invalid UTF-8
			// sequence in the port's text convention): the C# escapes each
			// such code unit -- the defensive classification.
			AppendUnicodeEscape4(sb, static_cast<std::uint32_t>(cp));
		} else if (cp >= 0x10000) {
			// A non-BMP code point iterates as TWO surrogate halves over the
			// C#'s UTF-16 `char` loop, each `IsSurrogate` half escaping
			// separately -- the port emits the same `\ud83d\ude00` pair.
			const std::uint32_t offset = static_cast<std::uint32_t>(cp - 0x10000);
			AppendUnicodeEscape4(sb, 0xD800u + (offset >> 10));
			AppendUnicodeEscape4(sb, 0xDC00u + (offset & 0x3FFu));
		} else if (IsUtf16WhiteSpace(cp) || IsUtf16Control(cp)) {
			AppendUnicodeEscape4(sb, static_cast<std::uint32_t>(cp));
		} else {
			sb.append(name.substr(i, n));
		}
		i += n;
	}
	return sb;
}

std::string ILAmbience::EscapeName(std::string_view name)
{
	std::string sb;
	EscapeName(sb, name);
	return sb;
}

} // namespace ILSpy::Decompiler::IL
