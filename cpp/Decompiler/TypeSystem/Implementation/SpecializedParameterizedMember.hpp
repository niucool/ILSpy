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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SpecializedMember.cs --
// `SpecializedParameterizedMember` (the abstract `SpecializedMember, IParameterizedMember`
// base adding the lazily-computed `Parameters` list). `SpecializedMethod` and
// `SpecializedProperty` derive from it; the `Parameters` getter builds the substituted
// parameter list ONCE (lazily, via `LazyInit`), each parameter a `SpecializedParameter`
// wrapping the base member's parameter with its type run through the member's
// substitution. `GetMembersHelper.GetMethodsImpl` / `GetPropertiesImpl` build the
// `SpecializedMethod` / `SpecializedProperty` whose `Parameters` this base supplies.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `public abstract class SpecializedParameterizedMember : SpecializedMember,
//      IParameterizedMember` ports to a C++ class
//      `SpecializedParameterizedMember : public SpecializedMember, public IParameterizedMember`
//      with a PROTECTED ctor (the port's "abstract-by-protected-ctor" mirror of `abstract
//      class`). Like `SpecializedField : SpecializedMember, IField`, this hits the TWO-
//      `IMember`-SUBOBJECT DIAMOND (`SpecializedMember : IMember` + `IParameterizedMember :
//      IMember` -- two `IMember` subobjects). But UNLIKE `SpecializedField` (CONCRETE,
//      `final`), this class is ABSTRACT: it overrides ONLY `Parameters()` (the
//      `IParameterizedMember`-own surface), leaving sub B's `IMember` pure-virtuals
//      (`Name` / `SymbolKind` / ...) UNRESOLVED (the `SpecializedMember` overrides resolve
//      sub A's only -- non-virtual inheritance means the two `IMember` subobjects have
//      SEPARATE vtables, so sub B's pure-virtuals stay). The class is therefore abstract
//      (cannot be instantiated directly); the CONCRETE derived leaves (`SpecializedMethod`
//      / `SpecializedProperty`) add the diamond overrides (one delegating override per
//      method name, the `SpecializedField` pattern). The test exercises this class via a
//      test-only public-ctor subclass that adds those diamond overrides.
//  (b) The C# `IReadOnlyList<IParameter> parameters` lazy-cached field (read via
//      `LazyInit.VolatileRead` / written via `LazyInit.GetOrSet`) ports to a
//      `mutable std::shared_ptr<std::vector<std::shared_ptr<IParameter>>> parameters_`
//      -- an OWNING cache: the `shared_ptr` owns the vector, the vector owns the
//      `SpecializedParameter` instances (each a `shared_ptr<IParameter>`). `mutable` for
//      the lazy write in the `const` `Parameters()` accessor (the `ArrayTypeReference::
//      resolved_` / `SpecializedMember::returnType_` precedent). The owning-vector design
//      is the port's faithful counterpart of the C# `IParameter[]` cache (the C# array
//      owns the `SpecializedParameter` references; the port's `shared_ptr<vector<shared_ptr>>`
//      owns the `SpecializedParameter` instances).
//  (c) `IParameterizedMember::Parameters()` returns `std::vector<const IParameter*>` (a
//      NON-OWNING snapshot, the port convention). The `SpecializedParameterizedMember::
//      Parameters()` override builds the owning cache (via `CreateParameters`) ONCE, then
//      returns a non-owning snapshot of the cached `SpecializedParameter` instances (their
//      `shared_ptr::get()`s). The snapshot is rebuilt each call (cheap -- a vector of raw
//      pointers); the EXPENSIVE part (building the `SpecializedParameter`s) is cached.
//  (d) `CreateParameters` (the C# `protected IParameter[] CreateParameters(Func<IType, IType>
//      substitution)`) ports to a `protected std::shared_ptr<std::vector<std::shared_ptr<
//      IParameter>>> CreateParameters(std::function<ITypePtr(const IType&)> substitution)
//      const`. It reads the base member's parameters via `dynamic_cast<const
//      IParameterizedMember*>(baseMember_.get())->Parameters()` (the C# `((IParameterizedMember)
//      baseMember).Parameters` -- a downcast; the port uses `dynamic_cast` for safety, a
//      null result treated as no parameters). For each base parameter `p` (a NON-OWNING
//      `const IParameter*` from the snapshot): `newType = substitution(p->Type())`;
//      `parameters[i] = make_shared<SpecializedParameter>(p, newType, this)` -- the
//      `SpecializedParameter` wraps the non-owning `p` (the D480 non-owning-`baseParameter`
//      design), owns the `newType`, and takes `this` as the non-owning `Owner`. The lambda
//      the `Parameters()` getter passes is `t => t.AcceptVisitor(this.Substitution)` (the
//      C#); the port uses the public `Substitution()` + `const_cast` (the `mutable
//      substitution_` is non-const in a `const` method, but `Substitution()` returns a
//      `const TypeParameterSubstitution*`; the `const_cast` to a non-const `TypeVisitor&`
//      is safe -- the underlying `mutable` field, and a substitution does not mutate in
//      `AcceptVisitor` reads -- the D482 `const_cast` precedent).
//  (e) OUT-OF-LINE: `Parameters()` / `CreateParameters` are in the `.cpp` (they need
//      `TypeParameterSubstitution` / `SpecializedParameter` / `IParameterizedMember`
//      complete); the class is added to the ilspy `CMakeLists.txt` (it has a `.cpp`).
//  (f) The C# `protected set` of `Parameters` (used for `LiftedUserDefinedOperator`, a special
//      case not a normal substitution -- the C# setter's own comment) ports to the protected
//      `SetParameters` member: assigns the owning cache directly, bypassing the lazy
//      computation. `ToString()` is DEFERRED (the C# `DeclaringType.ReflectionName` / `ReturnType.
//      ReflectionName` / `Parameters[i].ToString()` -- needs `IType::ReflectionName`, which
//      IS ported, but the shared `IParameter::ToString` is not, the `SpecializedParameter::
//      ToString` deferral precedent).

#pragma once

#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedMember.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The abstract `SpecializedMember, IParameterizedMember` base (see the header comment).
// Adds the lazily-computed `Parameters` list (a `SpecializedParameter` per base parameter,
// its type run through the member's substitution). The complex members are out-of-line in
// the .cpp.
class SpecializedParameterizedMember : public SpecializedMember, public IParameterizedMember {
public:
    // The C# `IReadOnlyList<IParameter> Parameters` -- lazily the substituted parameter
    // list, cached. Out-of-line (lazy + CreateParameters + dynamic_cast).
    std::vector<const IParameter*> Parameters() const override;

protected:
    // The C# `protected SpecializedParameterizedMember(IParameterizedMember memberDefinition)`.
    // `memberDefinition` is upcast to `shared_ptr<IMember>` for the `SpecializedMember` base.
    explicit SpecializedParameterizedMember(std::shared_ptr<IParameterizedMember> memberDefinition)
        : SpecializedMember(memberDefinition) {}

    // The C# `protected set` of `Parameters` (the C# property's setter) -- used for
    // `LiftedUserDefinedOperator`, a special case not a normal substitution. Assigns the
    // owning cache directly, bypassing the lazy `CreateParameters` computation (the C#
    // setter's own comment: used only during construction before the member is published).
    void SetParameters(
        std::shared_ptr<std::vector<std::shared_ptr<IParameter>>> parameters) {
        parameters_ = std::move(parameters);
    }

    // The C# `protected IParameter[] CreateParameters(Func<IType, IType> substitution)` --
    // builds the owning substituted parameter list. Each base parameter is wrapped in a
    // `SpecializedParameter` (non-owning base pointer, owning new type, `this` as the
    // non-owning owner). Out-of-line. The `substitution` lambda applies the member's
    // substitution to each parameter's type.
    std::shared_ptr<std::vector<std::shared_ptr<IParameter>>> CreateParameters(
        std::function<ITypePtr(const IType&)> substitution) const;

private:
    // `mutable`: the `const` `Parameters()` lazily writes the owning cache.
    mutable std::shared_ptr<std::vector<std::shared_ptr<IParameter>>> parameters_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
