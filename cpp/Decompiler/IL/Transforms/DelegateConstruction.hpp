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

// Port of the MatchDelegateConstruction helper from
// ICSharpCode.Decompiler/IL/Transforms/DelegateConstruction.cs. The full
// DelegateConstruction transform (which rewrites a recognised delegate
// construction into a lambda / closure) is deferred; this ports just the
// pattern-matching helper the next in-order transform
// (CachedDelegateInitialization) and the later DelegateConstruction transform
// consume. It is a tested-but-not-yet-wired foundation (no pipeline consumer
// yet), following the NullableLiftingTransform precedent.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

#include <string>

namespace ILSpy::Decompiler::IL {

class ILInstruction;

// Match result for MatchDelegateConstruction. Mirrors the C# out-params
// (targetMethod, target, delegateType): target is the NewObj's first argument
// (the receiver captured by the delegate), delegateType is the delegate type
// (the constructor's declaring type), and targetMethod is this port's stand-in
// for the C# IMethod -- the resolved display name of the ldftn/ldvirtftn
// argument (empty when the call is not a delegate construction).
struct DelegateConstructionMatch {
    ILInstruction* target = nullptr;
    TypeSystem::ITypePtr delegateType;
    std::string targetMethod;
};

class DelegateConstruction {
public:
    // Match a `newobj DelegateType(target, ldftn method)` delegate construction
    // (the C# `case NewObj call:`). The call must be a newobj (Call::IsNewObj)
    // with exactly two arguments, the second an ldftn or ldvirtftn, and its
    // declaring type's Kind must be Delegate or Unknown (the C# also accepts
    // Unknown for an unresolvable type, e.g. a cross-assembly TypeRef). Returns
    // false for a null declaring type (the C# null DeclaringTypeDefinition).
    // allowTransformed (the C# ILFunction-arg case, which arises after the
    // DelegateConstruction transform rewrites a NewObj into an ILFunction
    // closure) is accepted but is a no-op here: this port has not ported the
    // DelegateConstruction transform yet, so no ILFunction argument arises and
    // that case never matches.
    static bool MatchDelegateConstruction(ILInstruction* inst, DelegateConstructionMatch& out,
                                          bool allowTransformed = false);
};

} // namespace ILSpy::Decompiler::IL
