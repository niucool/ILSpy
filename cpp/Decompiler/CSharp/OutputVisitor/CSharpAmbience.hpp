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

// Port of ICSharpCode.Decompiler/CSharp/OutputVisitor/CSharpAmbience.cs -- the C#
// ambience: converts type-system symbols and types to display text (editor tooltips,
// member overviews, search results, ...). The class is a thin driver over the
// TypeSystemAstBuilder (the per-flag `CreateAstBuilder` factory + the
// ConvertSymbol/ConvertEntity/ConvertParameter/ConvertType/ConvertVariable renderers)
// and the OutputVisitor stack (`TokenWriter` + `CSharpOutputVisitor` + the
// `AstNode.ToString` base rendering), implementing the `Output::IAmbience` interface
// (D372, the `ConversionFlags` enum + the four `Convert*`/`WrapComment` virtuals).
//
// C#-to-C++ porting decisions:
//  * `public class CSharpAmbience : IAmbience` -> derives the ported
//    `::ILSpy::Decompiler::Output::IAmbience` abstract base. The
//    `ConversionFlags ConversionFlags { get; set; }` property overrides the
//    interface's pure-virtual getter/setter pair; the backing field defaults to
//    `ConversionFlags.None` (the C# default enum value).
//  * The `ConversionFlags` property-name-shares-enum-type collision (the D472
//    `NameLookupMode` precedent, third application): the inherited member function
//    `ConversionFlags()` hides the `Output::ConversionFlags` enum name for the whole
//    class body, so the class-scope alias `CF` (declared before every use) carries the
//    enum in every declaration and every body spelling (`CF::ShowReturnType`), and the
//    property reads go through the getter call `ConversionFlags()`.
//  * The C# `TokenWriter` parameter ports as a `TokenWriter*` (the port's
//    `CSharpOutputVisitor` ctor convention); the C# null-check throws
//    `std::invalid_argument` (the `ArgumentNullException` convention). The C#
//    `formattingPolicy` null-check compiles out (a by-value parameter has no null
//    state); the `CSharpFormattingOptions` data class copies by value (the D317
//    precedent).
//  * The C# private helpers are widened to public for direct TDD ahead of the
//    production consumers (the CSharpResolver `TryConvert` widening convention); the
//    class-level member order follows the C# source order.
//  * DEFERRED (documented at each site, the deferred-arm-yields-faithful-fallback
//    convention):
//     - `ExtensionInfo.IsExtensionMarkerType` (the `IsExtension` tail; `ExtensionInfo`
//       is a forward-declared long-pole dep at ITypeDefinition.hpp and every ported
//       `ITypeDefinition` implementation returns nullptr from `ExtensionInfo()`, so the
//       C# `extensionInfo != null &&` short-circuit returns false for every
//       port-reachable shape -- `IsExtension` is faithfully false).
//     - `CSharpDecompiler.IsFixedField` (the `PlaceReturnTypeAfterParameterList`
//       return-type arm; a fixed buffer renders the `fixed[N]` indexer shape in C# --
//       the port renders the declared return type directly, faithful for every
//       non-fixed field, and no ported `IField` implementation is a fixed field).
//     - the `extensionGroup.Marker != null` parameter-list arm (populated only by the
//       deferred `IsExtensionMarkerType`; the port's marker is provably null, so the
//       arm never fires and the two live arms below carry the flow).
//  * The C# `symbol`/`type` `ArgumentNullException` guards compile out (C++ references
//    are non-null, the D374 convention).
//  * The C# value tuple `(IMethod Marker, IReadOnlyList<ITypeParameter> TypeParameters)`
//    (the `IsExtension` out-parameter) ports as the public nested `ExtensionGroup`
//    struct; the C# `default` is the null-marker/empty-list state the member
//    initializers express.

#pragma once

#include "Decompiler/Output/IAmbience.hpp"
#include "Decompiler/CSharp/OutputVisitor/CSharpFormattingOptions.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include <string>
#include <string_view>
#include <vector>

// The `TypeSystem` namespace alias (the sibling-namespace trap: an unqualified
// `TypeSystem::` inside `ILSpy::Decompiler::CSharp` binds the nested
// `CSharp::TypeSystem` of CSharpTypeResolveContext/UsingScope, NOT
// `ILSpy::Decompiler::TypeSystem`; the CSharpResolver.hpp convention).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace ILSpy::Decompiler::TypeSystem {
class IMember;
class IMethod;
class ISymbol;
class ITypeParameter;
class IVariable;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

class TokenWriter; // same-namespace forward declaration (a pointer parameter needs only this)

// The C# `public class CSharpAmbience : IAmbience` (CSharpAmbience.cs line 45) -- the
// C#-syntax ambience. Every entry funnels through `CreateAstBuilder` (the per-flag
// TypeSystemAstBuilder factory) and drives a `TokenWriter` with the pieces the flag mask
// selects.
class CSharpAmbience : public ::ILSpy::Decompiler::Output::IAmbience {
private:
	// The class-scope alias for the flags enum: the inherited member function
	// `ConversionFlags()` hides the `Output::ConversionFlags` enum name for the whole
	// class body (the `NameLookupMode` precedent).
	using CF = ::ILSpy::Decompiler::Output::ConversionFlags;

public:
	// The C# value tuple `(IMethod Marker, IReadOnlyList<ITypeParameter> TypeParameters)`
	// -- the extension-block group `IsExtension` reports through its out-parameter. The
	// C# `default` is the null-marker/empty-list state. Only the DEFERRED
	// `ExtensionInfo.IsExtensionMarkerType` query writes the fields; through the ported
	// surface `IsExtension` always reports false and leaves the group defaulted.
	struct ExtensionGroup {
		const TS::IMethod* Marker = nullptr;
		std::vector<const TS::ITypeParameter*> TypeParameters;
	};

	// ---- The `ConversionFlags` property (CSharpAmbience.cs line 49) --------------------
	// The IAmbience override pair. The getter's return type goes through the `CF` alias
	// (an unqualified `ConversionFlags` in the class body resolves to the inherited
	// member function, not the enum); the setter's parameter is fully qualified (the
	// IAmbience declaration-site precedent).
	CF ConversionFlags() const override { return conversionFlags_; }
	void ConversionFlags(::ILSpy::Decompiler::Output::ConversionFlags value) override {
		conversionFlags_ = value;
	}

	// ---- The ConvertSymbol region (CSharpAmbience.cs lines 53-228) ----------------------

	// The C# `public string ConvertSymbol(ISymbol symbol)` (line 54) -- render a symbol
	// to text through a fresh `StringWriter` (an `std::ostringstream`) +
	// `TextWriterTokenWriter` with the empty formatting policy. The C# null-check
	// compiles out (a reference is non-null).
	std::string ConvertSymbol(const TS::ISymbol& symbol) override;

	// The C# `public void ConvertSymbol(ISymbol symbol, TokenWriter writer,
	// CSharpFormattingOptions formattingPolicy)` (line 61) -- the writer-driven core the
	// string overload delegates to. Reads only the flags, so the port marks it const
	// (the read-only-members convention).
	void ConvertSymbol(const TS::ISymbol& symbol, TokenWriter* writer,
	                   CSharpFormattingOptions formattingPolicy) const;

	// The C# `bool IsExtension(ITypeDefinition? typeDef, out (IMethod, IReadOnlyList<ITypeParameter>) extensionGroup)`
	// (line 201) -- whether the symbol is an extension-block marker type. The C# nullable
	// first parameter ports as a pointer (the `symbol as ITypeDefinition` call site passes
	// a possibly-null cast result); the out tuple ports as the `ExtensionGroup&`
	// reference. Widened to public for direct TDD.
	bool IsExtension(const TS::ITypeDefinition* typeDef, ExtensionGroup& extensionGroup) const;

	// The C# `bool ShowParameterList(ISymbol e)` (line 215) -- which symbol kinds carry a
	// parameter list in the ambience's rendering (the C# `when`-clause switch). Widened
	// to public for direct TDD.
	bool ShowParameterList(const TS::ISymbol& e) const;

	// The C# `TypeSystemAstBuilder CreateAstBuilder()` (line 232) -- the per-flag builder
	// factory (the single place the `ConversionFlags` bits map onto the builder's
	// configuration surface). Returns by value (the builder is a copyable configuration
	// object). Widened to public for direct TDD. The return type is `Syntax::-qualified`
	// (`TypeSystemAstBuilder` lives in the sibling `Syntax` namespace -- unqualified
	// lookup from `OutputVisitor` does not find it).
	Syntax::TypeSystemAstBuilder CreateAstBuilder() const;

	// The C# `void WriteTypeDeclarationName(ITypeDefinition typeDef, TokenWriter writer,
	// CSharpFormattingOptions formattingPolicy)` (line 279) -- the type-declaration name
	// (with the recursive ShowDeclaringType prefix and the fully-qualified namespace
	// prefix). Widened to public for direct TDD.
	void WriteTypeDeclarationName(const TS::ITypeDefinition& typeDef, TokenWriter* writer,
	                              CSharpFormattingOptions formattingPolicy) const;

	// The C# `void WriteMemberDeclarationName(IMember member, TokenWriter writer,
	// CSharpFormattingOptions formattingPolicy)` (line 316) -- the member name: the
	// constructor/destructor declaring-type names, the operator keyword dispatch
	// (`implicit`/`explicit`/`operator checked`/the token), the `this` keyword of an
	// indexer, and the plain identifier default. Widened to public for direct TDD.
	void WriteMemberDeclarationName(const TS::IMember& member, TokenWriter* writer,
	                                CSharpFormattingOptions formattingPolicy) const;

	// The C# `void WriteTypeParameters(IEnumerable<TypeParameterDeclaration> typeParameters,
	// TokenWriter writer, CSharpFormattingOptions formattingPolicy)` (line 401) -- the
	// `ShowTypeParameterList`-gated `<...>` writer with the variance stripping. Widened
	// to public for direct TDD.
	void WriteTypeParameters(
		const std::vector<Syntax::TypeParameterDeclaration*>& typeParameters, TokenWriter* writer,
		CSharpFormattingOptions formattingPolicy) const;

	// The C# `void PrintModifiers(Modifiers modifiers, TokenWriter writer)` (line 422) --
	// the `CSharpModifiers.AllModifiers`-ordered keyword writer. Widened to public for
	// direct TDD.
	void PrintModifiers(Syntax::Modifiers modifiers, TokenWriter* writer) const;

	// The C# `void WriteQualifiedName(string name, TokenWriter writer,
	// CSharpFormattingOptions formattingPolicy)` (line 433) -- the dotted-name writer
	// (`AstType.Create` + the output visitor). Widened to public for direct TDD.
	void WriteQualifiedName(const std::string& name, TokenWriter* writer,
	                        CSharpFormattingOptions formattingPolicy) const;

	// ---- The variable/type/constant/comment entries (CSharpAmbience.cs lines 438-511) ---

	// The C# `public string ConvertVariable(IVariable v)` (line 440) -- render a variable
	// declaration (the trailing `;`/line breaks trimmed). Not part of the IAmbience
	// interface (a C#-ambience extra). Widened over the C# public.
	std::string ConvertVariable(const TS::IVariable& v) const;

	// The C# `public string ConvertType(IType type)` (line 448) -- the IAmbience override.
	// NOTE the flag the C# reads here is `UseFullyQualifiedEntityNames` (NOT the
	// `UseFullyQualifiedTypeNames` bit `CreateAstBuilder` maps) -- a C# quirk ported
	// faithfully. The builder's `ConvertType` takes a non-const `IType&` (the
	// `ChangeNullability`/`shared_from_this` arms); the const reference from the
	// interface contract is `const_cast` away at the call site (the D515 convention --
	// the accessor's const is the contract, the underlying type-system objects are
	// mutable).
	std::string ConvertType(const TS::IType& type) override;

	// The C# private `void ConvertType(IType type, TokenWriter writer,
	// CSharpFormattingOptions formattingPolicy)` (line 461) -- the writer-driven form the
	// `ShowDeclaringType` and explicit-interface prefixes use. Widened to public for
	// direct TDD.
	void ConvertType(const TS::IType& type, TokenWriter* writer,
	                 CSharpFormattingOptions formattingPolicy) const;

	// The C# `IType? GetExplicitInterfaceType(IMember member)` (line 472) -- the declaring
	// type of the first explicitly-implemented interface member. Widened to public for
	// direct TDD.
	TS::ITypePtr GetExplicitInterfaceType(const TS::IMember& member) const;

	// The C# `public string ConvertConstantValue(object constantValue)` (line 482) -- the
	// IAmbience override delegating to `TextWriterTokenWriter.PrintPrimitiveValue`.
	std::string ConvertConstantValue(const Syntax::PrimitiveValue& constantValue) override;

	// The C# `public string WrapComment(string comment)` (line 487) -- the IAmbience
	// override prepending `"// "`.
	std::string WrapComment(std::string_view comment) override;

private:
	// The C# `ConversionFlags ConversionFlags { get; set; }` backing field.
	CF conversionFlags_ = CF::None;
};

} // namespace ILSpy::Decompiler::CSharp::OutputVisitor
