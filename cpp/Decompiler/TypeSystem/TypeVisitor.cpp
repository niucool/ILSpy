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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Out-of-line TypeVisitor defaults for VisitTypeDefinition / VisitTypeParameter:
// the inline `type.VisitChildren(this)` needs the ITypeDefinition / ITypeParameter
// interfaces complete for the virtual dispatch, so these two defaults live in a
// .cpp that includes the interface headers (unlike the concrete IType.hpp types
// whose inline defaults compile with IType complete in TypeVisitor.hpp). In the
// minimal port the ITypeDefinition / ITypeParameter interfaces inherit IType's
// VisitChildren default (shared_from_this) -- the faithful "return this" for a
// type with no visited children; the real MetadataTypeDefinition /
// MetadataTypeParameter VisitChildren (visiting type parameters / constraints)
// lands with the rest of Phase 2.

#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"

namespace ILSpy::Decompiler::TypeSystem {

ITypePtr TypeVisitor::VisitTypeDefinition(ITypeDefinition& type) {
    return type.VisitChildren(*this);
}

ITypePtr TypeVisitor::VisitTypeParameter(ITypeParameter& type) {
    return type.VisitChildren(*this);
}

} // namespace ILSpy::Decompiler::TypeSystem
