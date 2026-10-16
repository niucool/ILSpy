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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `DefaultAttribute` (see the header for the port conventions).

#include "Decompiler/TypeSystem/Implementation/DefaultAttribute.hpp"

#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `DefaultAttribute(IType attributeType, ...)` -- asserts the type non-null
// (the ArgumentNullException convention (a)), stores the argument snapshots.
DefaultAttribute::DefaultAttribute(ITypePtr attributeType,
                                   std::vector<CustomAttributeTypedArgument> fixedArguments,
                                   std::vector<CustomAttributeNamedArgument> namedArguments)
    : attributeType_(std::move(attributeType)),
      fixedArguments_(std::move(fixedArguments)),
      namedArguments_(std::move(namedArguments))
{
    // The C# `if (attributeType == null) throw new ArgumentNullException(...)` (the
    // assert-then-move D424 convention).
    assert(attributeType_ != nullptr && "attributeType must not be null");
}

// The C# `DefaultAttribute(IMethod constructor, ...)` -- asserts the constructor non-null,
// derives the attribute type from its declaring type (the `SpecialType.UnknownType`
// fallback for a null declaring type), and validates the positional-argument count.
DefaultAttribute::DefaultAttribute(const IMethod* constructor,
                                   std::vector<CustomAttributeTypedArgument> fixedArguments,
                                   std::vector<CustomAttributeNamedArgument> namedArguments)
    : fixedArguments_(std::move(fixedArguments)),
      namedArguments_(std::move(namedArguments))
{
    // The C# `if (constructor == null) throw new ArgumentNullException(...)`.
    assert(constructor != nullptr && "constructor must not be null");
    constructor_ = constructor;
    // The C# `this.attributeType = constructor.DeclaringType ?? SpecialType.UnknownType`
    // -- an empty ITypePtr is the C# null, mapped to the `UnknownType()` null object
    // (the free-function `UnknownType()` returning a fresh `SpecialType(TypeKind::Unknown)`,
    // the D417 convention).
    auto declaringType = constructor->DeclaringType();
    attributeType_ = declaringType ? std::move(declaringType) : UnknownType();
    // The C# `if (fixedArguments.Length != constructor.Parameters.Count) throw new
    // ArgumentException("Positional argument count must match the constructor's
    // parameter count")` -- mapped to `std::invalid_argument` with the exact message
    // (convention (d)).
    if (fixedArguments_.size() != constructor->Parameters().size()) {
        throw std::invalid_argument(
            "Positional argument count must match the constructor's parameter count");
    }
}

// The C# `IType AttributeType { get; }` -- the stored non-null type.
const IType& DefaultAttribute::AttributeType() const
{
    return *attributeType_;
}

// The C# lazy `IMethod Constructor { get; }`:
//   `IMethod ctor = this.constructor;`
//   `if (ctor == null) {`
//   `    foreach (IMethod candidate in this.AttributeType.GetConstructors(`
//   `        m => m.Parameters.Count == FixedArguments.Length)) {`
//   `        if (candidate.Parameters.Select(p => p.Type).SequenceEqual(`
//   `            this.FixedArguments.Select(a => a.Type))) { ctor = candidate; break; }`
//   `    }`
//   `    this.constructor = ctor;`
//   `}`
//   `return ctor;`
// The port mirrors the flow: read the cache slot, and on a miss scan the attribute
// type's constructors (the filter is applied by `GetConstructors` itself), match the
// parameter types element-wise against the fixed-argument types (`IType::Equals`, the
// EqualityComparer<IType>.Default equivalent), cache the first match (or null), and
// return it. A null fixed-argument type is simply a non-match (the C# `Equals(null)`
// is false; the port skips the dereference).
const IMethod* DefaultAttribute::Constructor() const
{
    const IMethod* ctor = constructor_;
    if (ctor == nullptr) {
        const auto fixedCount = fixedArguments_.size();
        // The C# `m => m.Parameters.Count == FixedArguments.Length` filter.
        auto candidates = attributeType_->GetConstructors(
            [fixedCount](const IMethod* m) {
                return m->Parameters().size() == fixedCount;
            });
        for (const IMethod* candidate : candidates) {
            const auto parameters = candidate->Parameters();
            bool match = parameters.size() == fixedCount;
            for (std::size_t i = 0; match && i < parameters.size(); i++) {
                const ITypePtr& argumentType = fixedArguments_[i].Type();
                // The C# `p.Type.Equals(a.Type)`: a null argument type never matches
                // (no dereference of the empty shared_ptr).
                match = argumentType && parameters[i]->Type().Equals(*argumentType);
            }
            if (match) {
                ctor = candidate;
                break;
            }
        }
        constructor_ = ctor;
    }
    return ctor;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
