// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The out-of-line VarArgInstanceMethod members (the fresh-wrapper Specialize
// and the ToString render) -- see VarArgInstanceMethod.hpp. The .cpp exists
// because the Specialize body needs the complete TypeParameterSubstitution /
// TypeVisitor machinery (forward-declared as pointers in the header).

#include "Decompiler/TypeSystem/VarArgInstanceMethod.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

namespace {

// The `SymbolKind.ToString()` spelling (the C# `b.Append(this.SymbolKind)` in
// the ToString render): the byte-backed plain enum's exact-member-or-decimal
// `ToString` semantics (the MemberImplAttributes spelling precedent). The
// values reachable through a vararg call site are the method-family kinds; the
// full member table keeps the fallback exact for any kind.
std::string SpellSymbolKind(::ILSpy::Decompiler::TypeSystem::SymbolKind kind)
{
    switch (kind) {
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::None:
        return "None";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Module:
        return "Module";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition:
        return "TypeDefinition";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Field:
        return "Field";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Property:
        return "Property";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Indexer:
        return "Indexer";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Event:
        return "Event";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method:
        return "Method";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Operator:
        return "Operator";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor:
        return "Constructor";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Destructor:
        return "Destructor";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Accessor:
        return "Accessor";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace:
        return "Namespace";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Variable:
        return "Variable";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter:
        return "Parameter";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter:
        return "TypeParameter";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::Constraint:
        return "Constraint";
    case ::ILSpy::Decompiler::TypeSystem::SymbolKind::ReturnType:
        return "ReturnType";
    }
    // unreachable (the switch is exhaustive); kept for the .NET decimal
    // fallback shape
    return std::to_string(static_cast<int>(kind));
}

}  // namespace

// The C# `public IMethod Specialize(TypeParameterSubstitution substitution)`
// (the `IMember`/`IMethod` slots share this covariant override): a FRESH
// wrapper over the specialized base method, with the wrapper's own vararg
// parameter types run through the same substitution (convention (d)).
const IMethod* VarArgInstanceMethod::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    const IMethod* specializedBase = baseMethod_->Specialize(substitution);
    // The vararg types: the wrapper's parameters past the base's regular
    // count (`parameters.Skip(baseMethod.Parameters.Count - 1)`), each
    // substituted through the same visitor. `IType::AcceptVisitor` is
    // non-const (it may return `shared_from_this()`), so the const-snapshot
    // parameter type is const_cast at the visitor call (the
    // `GetMembersHelper::aliasMember` precedent -- the visitor never mutates
    // the visited type).
    std::vector<ITypePtr> substitutedVarArgs;
    for (std::size_t i = static_cast<std::size_t>(RegularParameterCount());
         i < parameters_.size(); i++)
    {
        substitutedVarArgs.push_back(const_cast<IType&>(parameters_[i]->Type())
                                         .AcceptVisitor(
                                             *const_cast<
                                                 TypeParameterSubstitution*>(
                                                 substitution)));
    }
    // The fresh wrapper's base-method handle co-owns THIS wrapper's baseMethod_
    // control block while pointing at the specialized base (the aliasing
    // shared_ptr -- the LocalFunctionMethod MemberDefinition precedent: the
    // specialized base stays alive through the same ownership graph the C# GC
    // reference maintains).
    auto rewrap = std::make_shared<VarArgInstanceMethod>(
        std::shared_ptr<IMethod>(baseMethod_,
                                 const_cast<IMethod*>(specializedBase)),
        std::move(substitutedVarArgs));
    rewraps_.push_back(std::move(rewrap));
    return rewraps_.back().get();
}

// The C# `public override string ToString()`: the
// `[KindDeclaringType.Name``N(params):ReturnType]` render (convention (e)).
std::string VarArgInstanceMethod::ToString() const
{
    std::string b = "[";
    b += SpellSymbolKind(SymbolKind());
    if (DeclaringType() != nullptr) {
        b += DeclaringType()->ReflectionName();
        b += '.';
    }
    b += Name();
    if (!TypeParameters().empty()) {
        b += "``";
        b += std::to_string(TypeParameters().size());
    }
    b += '(';
    std::vector<const IParameter*> params = Parameters();
    for (std::size_t i = 0; i < params.size(); i++) {
        if (i > 0) {
            b += ", ";
        }
        if (static_cast<int>(i) == RegularParameterCount()) {
            b += "..., ";
        }
        b += params[i]->Type().ReflectionName();
    }
    if (params.size() == static_cast<std::size_t>(RegularParameterCount())) {
        b += ", ...";
    }
    b += "):";
    b += ReturnType().ReflectionName();
    b += ']';
    return b;
}

} // namespace ILSpy::Decompiler::TypeSystem
