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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of `ReflectionHelper.GetTypeCode` + `ReflectionHelper.FindType(ICompilation, TypeCode)`
// -- see the header.

#include "Decompiler/TypeSystem/ReflectionHelper.hpp"

#include "Decompiler/Metadata/TypeName.hpp"  // TypeName (the SRM parser, the ParseReflectionName input)
#include "Decompiler/TypeSystem/FullTypeName.hpp"  // FullTypeName (the nested-arm UnknownType fallback)
#include "Decompiler/TypeSystem/ICompilation.hpp"  // ICompilation (FindType's compilation)
#include "Decompiler/TypeSystem/IMethod.hpp"  // IMethod (the ``N CurrentMember arm)
#include "Decompiler/TypeSystem/IModule.hpp"  // IModule (the module lookups)
#include "Decompiler/TypeSystem/IType.hpp"  // IType (+ the concrete composites)
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (KnownTypeCode / NestedTypes)
#include "Decompiler/TypeSystem/ITypeParameter.hpp"  // ITypeParameter (the wired type-parameter arms)
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"  // ITypeResolveContext (the context surface)
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"  // DummyTypeParameter
#include "Decompiler/TypeSystem/ReflectionNameParseException.hpp"  // ReflectionNameParseException
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"  // TopLevelTypeName (SplitTypeParameterCount)
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // FindModuleByAssemblyNameInfo

#include <memory>

namespace ILSpy::Decompiler::TypeSystem {

TypeCode GetTypeCode(const IType& type) {
    const ITypeDefinition* def = dynamic_cast<const ITypeDefinition*>(&type);
    if (def != nullptr) {
        const KnownTypeCode typeCode = def->KnownTypeCode();
        if (typeCode <= KnownTypeCode::String && typeCode != KnownTypeCode::Void) {
            // The C# `(TypeCode)typeCode` -- a numeric cast. The alignment is exact
            // BY CONSTRUCTION of the real enums: KnownTypeCode continues
            // System.TypeCode's numbering (both carry String=18 and NO member at
            // 17), so the cast is the identity over the primitive range; the
            // guard excludes Void and anything past String.
            return static_cast<TypeCode>(typeCode);
        }
        return TypeCode::Empty;
    }
    return TypeCode::Empty;
}

// The C# `public static IType FindType(this ICompilation compilation, TypeCode typeCode)`
// (ReflectionHelper.cs line 106): `return compilation.FindType((KnownTypeCode)typeCode);` -- a
// straight numeric cast forwarded to the interface lookup. Both enums continue
// System.TypeCode's numbering (String=18 in both, no member at 17), so the
// cast is the identity for every `TypeCode` value (`None` <-> `Empty`).
const IType& FindType(const ICompilation& compilation, TypeCode typeCode) {
    return compilation.FindType(static_cast<KnownTypeCode>(typeCode));
}

// The C# `public static string SplitTypeParameterCountFromReflectionName(string)`
// (ReflectionHelper.cs line 66): the position of the LAST '`'; no backtick -> the name
// as-is; else the prefix before it. Unlike the 2-arg overload this strip is
// UNCONDITIONAL -- the digits after the backtick are never checked.
std::string SplitTypeParameterCountFromReflectionName(std::string_view reflectionName) {
    auto pos = reflectionName.rfind('`');
    if (pos == std::string_view::npos) return std::string(reflectionName);
    return std::string(reflectionName.substr(0, pos));
}

// The C# `public static string SplitTypeParameterCountFromReflectionName(string, out int)`
// (ReflectionHelper.cs line 83): delegate to the existing
// TopLevelTypeName::SplitTypeParameterCount (the same strip-only-when-the-tail-parses
// semantics, already mirrored for TopLevelTypeName's reflection-name ctor).
std::string SplitTypeParameterCountFromReflectionName(std::string_view reflectionName,
                                                       int& typeParameterCount) {
    return TopLevelTypeName::SplitTypeParameterCount(reflectionName, typeParameterCount);
}

// The C# `private static IType ResolveTypeName(TypeName result, ITypeResolveContext
// resolveContext)` (ReflectionHelper.cs lines 157-223) -- the arm chain behind
// `ParseReflectionName`. File-local (the C# is private static; every arm is reachable
// through the public entry).
namespace {

// The non-owning snapshot of a module-owned definition (the C# `return type;` over the
// GC-owned reference): the modules own their entities, so the returned `ITypePtr` aliases
// with a no-op deleter -- the XamlContext/KnownTypeCache convention.
ITypePtr SnapshotDefinition(const IType* type) {
    return ITypePtr(const_cast<IType*>(type), [](IType*) { /* no-op: the module owns it */ });
}

// The C# `int.TryParse(s, out value)` -- the plain (culture-invariant, no-styles)
// integer parse the two type-parameter arms use: an optional sign then digits, no
// whitespace, no overflow. Returns false without touching `value` on failure (the
// C# leaves the out parameter at its default; the callers initialize to 0).
bool TryParseInt32(const std::string& text, int& value)
{
    if (text.empty()) return false;
    std::size_t i = 0;
    bool negative = false;
    if (text[0] == '+' || text[0] == '-') {
        negative = text[0] == '-';
        i = 1;
    }
    if (i >= text.size()) return false;
    long long parsed = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        parsed = parsed * 10 + (text[i] - '0');
        if (parsed > 0x7FFFFFFF) return false;  // int overflow fails the TryParse
    }
    value = negative ? static_cast<int>(-parsed) : static_cast<int>(parsed);
    return true;
}

ITypePtr ResolveTypeName(const Metadata::TypeName& result,
                         const ITypeResolveContext& resolveContext)
{
    if (result.IsArray()) {
        // The C# `new ArrayType(resolveContext.Compilation, ResolveTypeName(element),
        // GetArrayRank())` -- the compilation argument feeds only the DirectBaseTypes/
        // GetMembers surface the minimal-port ArrayType does not carry (the
        // ArrayTypeReference::Resolve precedent), so the (element, rank) ctor stands in.
        // The multi-dim ctor is used for EVERY rank (the C# has a single class -- the SZ
        // form is rank 1 of it; the minimal port's isSzArray distinction is the
        // signature-decoder's own shape, not this path's).
        return std::make_shared<ArrayType>(
            ResolveTypeName(*result.GetElementType(), resolveContext),
            result.GetArrayRank());
    }
    if (result.IsByRef()) {
        // The C# `new ByReferenceType(ResolveTypeName(element))`.
        return std::make_shared<ByReferenceType>(
            ResolveTypeName(*result.GetElementType(), resolveContext));
    }
    if (result.IsConstructedGenericType()) {
        ITypePtr genericType = ResolveTypeName(*result.GetGenericTypeDefinition(), resolveContext);
        const std::vector<std::shared_ptr<Metadata::TypeName>>& genericArgs =
            result.GetGenericArguments();
        // The C# degenerate arm: a resolved generic definition with `TypeParameterCount == 0`
        // is returned AS-IS (the arguments dropped) -- reachable when a non-generic name
        // carries arguments (e.g. "System.Int32[[System.String]]", which the parser
        // accepts verbatim).
        if (genericType->TypeParameterCount() == 0)
            return genericType;
        // The C# `IType[] resolvedTypes = new IType[genericType.TypeParameterCount];` -- an
        // array with NULL entries, filled only over the parsed arguments. The port mirrors
        // the null tail (the C# then binds the INTERNAL unchecked `params IType[]`
        // ParameterizedType ctor -- the IType[] argument converts to the params array
        // identically, beating the IEnumerable overload -- so NO ctor check fires and the
        // null entry flows into the constructed type, observable through its getters).
        std::vector<ITypePtr> resolvedTypes(
            static_cast<std::size_t>(genericType->TypeParameterCount()));
        for (std::size_t i = 0; i < genericArgs.size(); i++) {
            // The C# write `resolvedTypes[i]` for i < genericArgs.Length over the
            // TypeParameterCount-sized array -- more arguments than the arity is the
            // IndexOutOfRangeException, mapped to std::out_of_range with the .NET message
            // (the size_t-underflow guard the iteration-31 convention requires).
            if (i >= resolvedTypes.size())
                throw std::out_of_range("Index was outside the bounds of the array.");
            resolvedTypes[i] = ResolveTypeName(*genericArgs[i], resolveContext);
        }
        return std::make_shared<ParameterizedType>(std::move(genericType),
                                                   std::move(resolvedTypes));
    }
    if (result.IsNested()) {
        // The C# `ResolveTypeName(result.DeclaringType, resolveContext).GetDefinition()` -- a
        // non-definition resolved declaring type (an array, an UnknownType) yields null and
        // the walk is skipped, the UnknownType fallback taken.
        const ITypeDefinition* declaringType =
            ResolveTypeName(*result.DeclaringType(), resolveContext)->GetDefinition();
        int tpc = 0;
        const std::string plainName =
            SplitTypeParameterCountFromReflectionName(result.Name(), tpc);
        if (declaringType != nullptr) {
            for (const ITypeDefinition* type : declaringType->NestedTypes()) {
                // The match: the plain (arity-stripped) name AND the FULL type-parameter
                // count -- the nested type inherits the declaring type's parameters, so the
                // candidate's count is its own plus the declaring type's.
                if (type->Name() == plainName
                    && type->TypeParameterCount() == tpc + declaringType->TypeParameterCount())
                    return SnapshotDefinition(type);
            }
        }
        // The fallback preserves the nested chain: `new UnknownType(new
        // FullTypeName(result.FullName))` (the "Outer+Inner" reflection name parses into the
        // top-level + nested segments the FullTypeName ctor keeps).
        return std::make_shared<class UnknownType>(FullTypeName(result.FullName()));
    }
    if (result.IsPointer()) {
        // The C# `new PointerType(ResolveTypeName(element))`.
        return std::make_shared<PointerType>(
            ResolveTypeName(*result.GetElementType(), resolveContext));
    }
    // The simple arm. `Debug.Assert(result.IsSimple)` -- the five arms above are exhaustive
    // over the parser's shapes.
    const std::string fullName = result.FullName();
    if (fullName.length() > 1 && fullName[0] == '`') {
        if (fullName.length() > 2 && fullName[1] == '`') {
            // "``N" -- the METHOD type parameter at N.
            int index = 0;
            if (TryParseInt32(fullName.substr(2), index)) {
                const IMember* currentMember = resolveContext.CurrentMember();
                const IMethod* m = dynamic_cast<const IMethod*>(currentMember);
                if (m != nullptr && index < static_cast<int>(m->TypeParameters().size()))
                    return SnapshotDefinition(m->TypeParameters()[
                        static_cast<std::size_t>(index)]);
                return Implementation::DummyTypeParameter::GetMethodTypeParameter(index);
            }
        } else {
            // "`N" -- the CLASS type parameter at N.
            int index = 0;
            if (TryParseInt32(fullName.substr(1), index)) {
                const ITypeDefinition* currentTypeDefinition =
                    resolveContext.CurrentTypeDefinition();
                if (currentTypeDefinition != nullptr
                    && index < currentTypeDefinition->TypeParameterCount())
                    return SnapshotDefinition(
                        currentTypeDefinition->TypeParameters()[
                            static_cast<std::size_t>(index)]);
                return Implementation::DummyTypeParameter::GetClassTypeParameter(index);
            }
        }
    }
    // The C# `var topLevelTypeName = new TopLevelTypeName(result.FullName);` -- the
    // reflection-name ctor (namespace split at the last '.', the arity stripped).
    const TopLevelTypeName topLevelTypeName(fullName);
    if (result.AssemblyName() != nullptr) {
        // The assembly-qualified arm: the module whose assembly matches the parsed name
        // (FullName first, then the short Name, both case-insensitive). A HIT scopes the
        // lookup to that module -- a type miss inside it takes the UnknownType fallback
        // WITHOUT falling through to the walk below; a null module falls through.
        const IModule* module =
            FindModuleByAssemblyNameInfo(resolveContext.Compilation(),
                                         *result.AssemblyName());
        if (module != nullptr) {
            const ITypeDefinition* typeDef = module->GetTypeDefinition(topLevelTypeName);
            if (typeDef != nullptr)
                return SnapshotDefinition(typeDef);
            return std::make_shared<class UnknownType>(FullTypeName(topLevelTypeName));
        }
    }
    // The plain walk: every module in order, the first GetTypeDefinition hit wins (the
    // main module is first -- the ICompilation contract).
    for (const IModule* module : resolveContext.Compilation().Modules()) {
        const ITypeDefinition* type = module->GetTypeDefinition(topLevelTypeName);
        if (type != nullptr)
            return SnapshotDefinition(type);
    }
    return std::make_shared<class UnknownType>(FullTypeName(topLevelTypeName));
}

} // namespace

// The C# `public static IType ParseReflectionName(string reflectionTypeName,
// ITypeResolveContext resolveContext)` (ReflectionHelper.cs lines 131-155).
ITypePtr ParseReflectionName(std::string_view reflectionTypeName,
                              const ITypeResolveContext& resolveContext)
{
    // The C# `ArgumentNullException(nameof(reflectionTypeName))` is N/A (a string_view
    // has no null state).
    std::shared_ptr<Metadata::TypeName> result;
    if (!Metadata::TypeName::TryParse(reflectionTypeName, result)) {
        // The C# throws at position 0 -- the SRM TryParse failure carries no position.
        throw ReflectionNameParseException(
            0, "Invalid type name: " + std::string(reflectionTypeName));
    }
    return ResolveTypeName(*result, resolveContext);
}

// The C# `internal static int ReadTypeParameterCount(string reflectionTypeName, ref int
// pos)` (ReflectionHelper.cs lines 225-243).
int ReadTypeParameterCount(std::string_view reflectionTypeName, int& pos)
{
    const int startPos = pos;
    while (pos < static_cast<int>(reflectionTypeName.length())) {
        const char c = reflectionTypeName[static_cast<std::size_t>(pos)];
        if (c < '0' || c > '9')
            break;
        pos++;
    }
    // The C# `int.TryParse(reflectionTypeName.Substring(startPos, pos - startPos), out tpc)`
    // -- an empty slice (no digits consumed) or an overflowing run both fail, and the throw
    // reports the position the scan stopped at.
    int tpc = 0;
    if (!TryParseInt32(std::string(
            reflectionTypeName.substr(static_cast<std::size_t>(startPos),
                                      static_cast<std::size_t>(pos - startPos))),
                       tpc))
        throw ReflectionNameParseException(pos, "Expected type parameter count");
    return tpc;
}

} // namespace ILSpy::Decompiler::TypeSystem
