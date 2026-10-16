// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/CSharp/Transforms/RemoveCLSCompliantAttribute.hpp"

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::AstType;
using Syntax::Attribute;
using Syntax::AttributeSection;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace {

// The C# `trr.Type.FullName` read. The port's `IType` does not carry the `AbstractType
// .FullName` property yet (`Namespace`/`FullName` are documented as a deferred part of the
// `IType` surface); the resolved definition's `FullName` is the faithful value for a real
// type reference, and `ReflectionName()` is the fallback for an unresolved one. The two
// coincide for the top-level, non-generic attribute types this transform matches.
std::string TypeFullName(const TS::IType& type) {
    if (const TS::ITypeDefinition* definition = type.GetDefinition())
        return definition->FullName();
    return type.ReflectionName();
}

// The C# `attribute.Type`, null-safe. The generated `Attribute` requires a `Type`, but a
// hand-built tree (or a partially constructed node) may leave it unset; the C# dereferences
// and would throw, while the port skips the malformed node (the `attribute.Type.Annotation
// <TypeResolveResult>()` null-conditional read's intent).
const Sem::TypeResolveResult* ResolveResultOf(const Attribute* attribute) {
    AstType* typeNode = attribute->Type();
    if (typeNode == nullptr)
        return nullptr;
    return typeNode->Annotation<Sem::TypeResolveResult>();
}

} // namespace

void RemoveCLSCompliantAttribute::Run(AstNode& rootNode, TransformContext& /*context*/) {
    // The C# `foreach (var section in rootNode.Children.OfType<AttributeSection>())`: the
    // direct children only, in document order. The `ChildEnumerator` captures the next
    // sibling before yielding, so removing the current section during the walk is safe (the
    // C# mutation-tolerant enumerator).
    for (AstNode* child : rootNode.Children()) {
        auto* section = dynamic_cast<AttributeSection*>(child);
        if (section == nullptr)
            continue;
        if (section->AttributeTarget() == "assembly")
            continue;
        auto& attributes = section->Attributes();
        for (int i = 0; i < attributes.Count(); ) {
            Attribute* attribute = attributes.At(i);
            const Sem::TypeResolveResult* trr = ResolveResultOf(attribute);
            if (trr != nullptr && TypeFullName(trr->Type()) == ClsCompliantAttributeFullName) {
                // The `Remove()` detaches the attribute from the collection, shifting the
                // following elements down; do not advance the index.
                attribute->Remove();
            } else {
                ++i;
            }
        }
        if (section->Attributes().Count() == 0)
            section->Remove();
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
