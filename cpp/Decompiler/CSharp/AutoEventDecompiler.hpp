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

// Port of ICSharpCode.Decompiler/CSharp/AutoEventDecompiler.cs -- recognizing
// automatic (field-like) events by structurally matching the ILAst of the
// compiler-generated add/remove accessors. The accessor bodies are decompiled
// with the fixed analysis settings (CSharpDecompiler.DecompileBodyForAnalysis),
// so recognition is independent of the user-visible settings that shape the
// final C# output.
//
// The three accessor shapes the C# matcher accepts:
//   - the csc 4 / Roslyn compare-exchange loop (MatchCompareExchangeLoop),
//   - the mcs compare-exchange loop (MatchCompareExchangeLoopMcs), and
//   - the pre-4.0 non-thread-safe combine assignment (MatchSimpleCombineAssignment).
//
// Divergence (documented at the call sites): the C# `call.Method` /
// `ldflda.Field` identities are resolved by the type system; the port's IL
// reader records only the raw token/name, so the matchers read `Call::Method`
// when it is set and fall back to `MethodName`/`IsInstanceCall`, and read
// `LdFlda::Field`/`LdsFlda::Field` when it is set (the resolved-field handle is
// populated by tests/transforms, not the reader). The real-body
// `IsAutomaticAccessor` path therefore depends on the control-flow pipeline
// producing the C# shape; the port's LoopDetection/ConditionDetection is less
// aggressive today, so the synthetic matcher tests pin the C# shape while the
// real `IsAutomaticEvent` verdict is only exercised through `FindBackingField`.
//
// The `AddFieldLikeEventAttributes` helper (the EventDeclaration attribute
// conversion the C# exposes for the field-like event declaration) is ported; its
// consumer is the type-declaration renderer.

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler {

class DecompileRun;

namespace Metadata {
class MetadataFile;
}

namespace TypeSystem {
class IEvent;
class IField;
class IMethod;
}

namespace IL {
class Block;
class BlockContainer;
}

namespace CSharp {

// The REAL type-system namespace alias: the CSharp/TypeSystem sub-namespace
// (CSharpTypeResolveContext/UsingScope) shadows the plain `TypeSystem::` lookup
// once a header pulls it in, so the class declarations go through the
// fully-qualified alias (the TypeSystemAstBuilder TS:: convention).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace Syntax {
class EventDeclaration;
class TypeSystemAstBuilder;
}

// The C# `static class AutoEventDecompiler`.
class AutoEventDecompiler {
public:
    // The C# `public static bool IsAutomaticEvent(IDecompilerTypeSystem typeSystem,
    // IEvent ev, DecompileRun decompileRun, CancellationToken cancellationToken,
    // out IField? backingField)`: memoizes the verdict in the run so all consumers
    // decide from the same analysis. `backingField` is null when the event is not
    // automatic (the C# null-out). The port passes the MetadataFile the accessor
    // bodies are read from instead of the IDecompilerTypeSystem (the port's
    // DecompileBodyForAnalysis reads through the file).
    static bool IsAutomaticEvent(DecompileRun& decompileRun,
                                 const Metadata::MetadataFile& file,
                                 const TS::IEvent& ev,
                                 const TS::IField*& backingField);

    // The C# `static bool IsAutomaticEvent(IDecompilerTypeSystem typeSystem,
    // IEvent ev, CancellationToken cancellationToken, out IField? backingField)`:
    // the un-memoized verdict. `backingField` is null when the event is not
    // automatic.
    static bool IsAutomaticEvent(const Metadata::MetadataFile& file,
                                 const TS::IEvent& ev,
                                 const TS::IField*& backingField);

    // The C# `static IField? FindBackingField(IEvent ev)`: the declaring type's
    // private (same-static-ness) field the PropertyAndEventBackingFieldLookup
    // associates with the event, or null.
    static const TS::IField* FindBackingField(const TS::IEvent& ev);

    // The C# `static bool IsAutomaticAccessor(IDecompilerTypeSystem typeSystem,
    // IMethod accessor, IField field, bool isAddAccessor, CancellationToken)`:
    // decompiles the accessor body with the analysis settings and matches it.
    static bool IsAutomaticAccessor(const Metadata::MetadataFile& file,
                                    const TS::IMethod& accessor,
                                    const TS::IField& field,
                                    bool isAddAccessor);

    // The pure body matcher behind IsAutomaticAccessor -- the three recognized
    // accessor shapes. Exposed so the structural matchers are testable without a
    // metadata-backed method handle.
    static bool MatchAutomaticAccessorBody(IL::Block& body,
                                           IL::BlockContainer& functionBody,
                                           const TS::IMethod& accessor,
                                           const TS::IField& field,
                                           bool isAddAccessor);

    // The C# `internal static void AddFieldLikeEventAttributes(EventDeclaration
    // eventDecl, TypeSystemAstBuilder astBuilder, IEvent ev, IField backingField)`:
    // adds the add-accessor and backing-field attributes of an automatic event to
    // its field-like declaration, as "method:" and "field:" sections, dropping the
    // attributes the compiler puts on automatic events (AutoEventDecompiler.cs).
    // The C# non-null `AddAccessor` is asserted; it is guaranteed by the
    // IsAutomaticEvent verdict the caller checks first.
    static void AddFieldLikeEventAttributes(Syntax::EventDeclaration& eventDecl,
                                            const Syntax::TypeSystemAstBuilder& astBuilder,
                                            const TS::IEvent& ev,
                                            const TS::IField& backingField);
};

} // namespace CSharp
} // namespace ILSpy::Decompiler
