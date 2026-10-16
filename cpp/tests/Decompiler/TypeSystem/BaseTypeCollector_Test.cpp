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

// Tests for `BaseTypeCollector` (the port of
// ICSharpCode.Decompiler/TypeSystem/Implementation/BaseTypeCollector.cs -- the
// helper behind `GetAllBaseTypes()` / `GetNonInterfaceBaseTypes()` /
// `GetAllBaseTypeDefinitions()` / `IsDerivedFrom()`). The load-bearing cruxes
// are: the depth-first ADD-AT-END traversal (a type is output only after all
// its base types -- the C# comment pins that add-at-start+reverse is NOT
// equivalent for diamonds), the duplicate suppression (a diamond's shared base
// is output once), the active-types cycle guard (`class C<X> : C<C<X>> {}`
// would not be caught by the duplicate check yet must terminate), and the
// `SkipImplementedInterfaces` option (interfaces are skipped only when the
// current kind chain root's definition is neither an interface nor a type
// parameter -- kept for interface/type-parameter inputs). The tests exercise
// the collector through plain `IType` stubs whose `DirectBaseTypes()` graph is
// hand-wired; a type with no `GetDefinition()` override uses itself as the
// definition (the `GetDefinition() ?? type` fallback), so the
// interface/type-parameter-input shapes need no full `ITypeDefinition` stubs.

#include "Decompiler/TypeSystem/Implementation/BaseTypeCollector.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

// A minimal concrete `IType` stand-in carrying a kind, a name, and a hand-wired
// `DirectBaseTypes` graph. `GetDefinition()` is inherited (nullptr), so the
// collector's `GetDefinition() ?? type` fallback uses the stub itself as the
// definition-marked active type -- the shapes the `SkipImplementedInterfaces`
// interface/type-parameter guards consult.
class StubType : public TS::IType {
public:
    StubType(std::string name, TS::TypeKind kind)
        : name_(std::move(name)), kind_(kind) {}

    TS::TypeKind Kind() const override { return kind_; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }

    void AddBaseType(std::shared_ptr<StubType> base) { bases_.push_back(std::move(base)); }
    void AddBaseType(TS::ITypePtr base) { bases_.push_back(std::move(base)); }
    std::vector<TS::ITypePtr> DirectBaseTypes() const override { return bases_; }

    // The C# `type.GetDefinition() ?? type` ports to the IType virtual-with-default
    // (nullptr) OR the stamped definition below (a type whose definition is a
    // distinct object, e.g. a parameterized type's generic definition).
    void SetDefinitionPtr(const TS::ITypeDefinition* def) { def_ = def; }
    const TS::ITypeDefinition* GetDefinition() const override { return def_; }

protected:
    bool StructuralEquals(const TS::IType& other) const override
    {
        return this == &other; // identity equality for the test stub
    }

private:
    std::string name_;
    TS::TypeKind kind_;
    std::vector<TS::ITypePtr> bases_;
    const TS::ITypeDefinition* def_ = nullptr;
};

std::shared_ptr<StubType> MakeType(const std::string& name, TS::TypeKind kind = TS::TypeKind::Class)
{
    return std::make_shared<StubType>(name, kind);
}

std::vector<std::string> Names(const std::vector<const TS::IType*>& types)
{
    std::vector<std::string> names;
    for (const TS::IType* t : types)
        names.push_back(t->Name());
    return names;
}

} // namespace

// ---------------------------------------------------------------------------
// Class shape -- final (the C# is sealed), not copyable into a base (there is none).
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, IsFinal)
{
    static_assert(std::is_final<TS::Implementation::BaseTypeCollector>::value,
                  "BaseTypeCollector is final (the C# is sealed)");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// A type with no base types: the output is just the type itself (added at the end).
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, TypeWithNoBaseTypesOutputsItself)
{
    auto object = MakeType("Object");
    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*object);
    ASSERT_EQ(collector.Types().size(), 1u);
    EXPECT_EQ(collector.Types()[0], object.get());
}

// ---------------------------------------------------------------------------
// DFS post-order: every base type is output before its derived type.
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, BaseTypesComeBeforeDerivedTypes)
{
    auto object = MakeType("Object");
    auto comparable = MakeType("IComparable", TS::TypeKind::Interface);
    auto stream = MakeType("Stream");
    stream->AddBaseType(object);
    stream->AddBaseType(comparable);

    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*stream);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "Object", "IComparable", "Stream" }));
}

TEST(BaseTypeCollectorTest, TransitiveBaseChainIsOrderedBaseFirst)
{
    auto c = MakeType("C");
    auto b = MakeType("B");
    auto a = MakeType("A");
    b->AddBaseType(c);
    a->AddBaseType(b);

    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*a);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "C", "B", "A" }));
}

// ---------------------------------------------------------------------------
// Diamond: the shared base is collected once (add-at-the-end order, NOT
// add-at-start+reverse: Object, ILeft, IRight, Impl -- the C# comment pins this).
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, DiamondSharedBaseIsOutputOnceInPostOrder)
{
    auto object = MakeType("Object");
    auto ileft = MakeType("ILeft", TS::TypeKind::Interface);
    auto iright = MakeType("IRight", TS::TypeKind::Interface);
    auto impl = MakeType("Impl");
    ileft->AddBaseType(object);
    iright->AddBaseType(object);
    impl->AddBaseType(ileft);
    impl->AddBaseType(iright);

    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*impl);
    EXPECT_EQ(Names(collector.Types()),
              (std::vector<std::string>{ "Object", "ILeft", "IRight", "Impl" }));
}

// ---------------------------------------------------------------------------
// Cyclic inheritance terminates (the active-types definition stack catches the
// back-edge the duplicate check cannot; the output stays finite).
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, CyclicInheritanceTerminates)
{
    auto c1 = MakeType("C1");
    auto c2 = MakeType("C2");
    c1->AddBaseType(c2);
    c2->AddBaseType(c1);

    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*c1);
    // C2 added at the end of its own collection, then C1 at the end of the outer call.
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "C2", "C1" }));
}

// ---------------------------------------------------------------------------
// A self-referencing type parameter shape (T : S where S : T): terminates.
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, CyclicTypeParameterConstraintsTerminate)
{
    auto t = MakeType("T", TS::TypeKind::TypeParameter);
    auto s = MakeType("S", TS::TypeKind::TypeParameter);
    t->AddBaseType(s);
    s->AddBaseType(t);

    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*t);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "S", "T" }));
}

// ---------------------------------------------------------------------------
// SkipImplementedInterfaces: an implemented interface of a class is skipped;
// the non-interface base class is kept.
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, SkipImplementedInterfacesDropsInterfacesForClassInput)
{
    auto object = MakeType("Object");
    auto comparable = MakeType("IComparable", TS::TypeKind::Interface);
    auto stream = MakeType("Stream");
    stream->AddBaseType(object);
    stream->AddBaseType(comparable);

    TS::Implementation::BaseTypeCollector collector;
    collector.SkipImplementedInterfaces = true;
    collector.CollectBaseTypes(*stream);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "Object", "Stream" }));
}

TEST(BaseTypeCollectorTest, SkipImplementedInterfacesTransitiveInterfaceIsDroppedButClassBasesSurvive)
{
    // Stream : Object, IComparable; MemoryStream : Stream -- with skip enabled,
    // the interface vanishes but the Stream -> Object class chain survives.
    auto object = MakeType("Object");
    auto comparable = MakeType("IComparable", TS::TypeKind::Interface);
    auto stream = MakeType("Stream");
    auto memoryStream = MakeType("MemoryStream");
    stream->AddBaseType(object);
    stream->AddBaseType(comparable);
    memoryStream->AddBaseType(stream);

    TS::Implementation::BaseTypeCollector collector;
    collector.SkipImplementedInterfaces = true;
    collector.CollectBaseTypes(*memoryStream);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "Object", "Stream", "MemoryStream" }));
}

// ---------------------------------------------------------------------------
// SkipImplementedInterfaces for an interface input: the base interfaces are KEPT
// (the `def.Kind != Interface` guard -- "When `type` is an interface, this
// method will also return base interfaces"). The stub has no GetDefinition
// override, so the collector's `?? type` fallback marks the interface itself.
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, SkipImplementedInterfacesKeepsBaseInterfacesForInterfaceInput)
{
    auto comparable = MakeType("IComparable", TS::TypeKind::Interface);
    auto stream = MakeType("IStream", TS::TypeKind::Interface);
    stream->AddBaseType(comparable);

    TS::Implementation::BaseTypeCollector collector;
    collector.SkipImplementedInterfaces = true;
    collector.CollectBaseTypes(*stream);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "IComparable", "IStream" }));
}

TEST(BaseTypeCollectorTest, SkipImplementedInterfacesKeepsBasesForTypeParameterInput)
{
    // A type parameter's bases are its effective base class + interfaces; the
    // `def.Kind != TypeParameter` guard keeps them all for a type-parameter input.
    auto object = MakeType("Object");
    auto comparable = MakeType("IComparable", TS::TypeKind::Interface);
    auto t = MakeType("T", TS::TypeKind::TypeParameter);
    t->AddBaseType(object);
    t->AddBaseType(comparable);

    TS::Implementation::BaseTypeCollector collector;
    collector.SkipImplementedInterfaces = true;
    collector.CollectBaseTypes(*t);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "Object", "IComparable", "T" }));
}

// ---------------------------------------------------------------------------
// A TypeParameter-typed BASE of a class is not an interface, so skip does not
// drop it (the guard keys on the base's own Kind).
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, SkipImplementedInterfacesKeepsTypeParameterBaseOfClass)
{
    auto t = MakeType("T", TS::TypeKind::TypeParameter);
    auto c = MakeType("C");
    c->AddBaseType(t);

    TS::Implementation::BaseTypeCollector collector;
    collector.SkipImplementedInterfaces = true;
    collector.CollectBaseTypes(*c);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "T", "C" }));
}

// ---------------------------------------------------------------------------
// A null promise in a DirectBaseTypes snapshot is skipped, not dereferenced
// (the C# never yields a null IType; the port's robustness tenet).
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, NullEntryInDirectBaseTypesIsSkipped)
{
    auto object = MakeType("Object");
    auto c = MakeType("C");
    c->AddBaseType(TS::ITypePtr{}); // null promise
    c->AddBaseType(object);

    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*c);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "Object", "C" }));
}

// ---------------------------------------------------------------------------
// A stamped (distinct) definition object marks the active type: a cycle back
// through the definition's instance terminates via the definition identity.
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, ActiveTypeDedupUsesDefinitionIdentityWhenStamped)
{
    // C<X> : C<C<X>>: two distinct StubType instances share no definition, so
    // the duplicate-instance guard in the C# relies on actives; here both
    // instances stamp the SAME definition object, so the second collection of
    // the generic definition terminates immediately.
    // Opaque identity anchor for the shared definition: only pointer-compared
    // (the active-types dedup), never dereferenced.
    char defAnchor = 0;
    auto* stampedDef = reinterpret_cast<const TS::ITypeDefinition*>(&defAnchor);
    auto cx = MakeType("C<X>");
    auto ccx = MakeType("C<C<X>>");
    cx->SetDefinitionPtr(stampedDef);
    ccx->SetDefinitionPtr(stampedDef);
    cx->AddBaseType(ccx);

    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*cx);
    // C<X> actives the stamped definition; C<C<X>> re-enters with the same
    // definition and is a back-edge -> returns immediately; then C<X> itself is added.
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "C<X>" }));
}

// ---------------------------------------------------------------------------
// A reused collector accumulates: the second CollectBaseTypes call appends only
// the not-yet-seen instances (the duplicate guard is collector-lifetime).
// ---------------------------------------------------------------------------

TEST(BaseTypeCollectorTest, ReusedCollectorDoesNotDuplicatePreviouslyCollectedTypes)
{
    auto object = MakeType("Object");
    auto stream = MakeType("Stream");
    stream->AddBaseType(object);

    TS::Implementation::BaseTypeCollector collector;
    collector.CollectBaseTypes(*object);
    collector.CollectBaseTypes(*stream);
    EXPECT_EQ(Names(collector.Types()), (std::vector<std::string>{ "Object", "Stream" }));
}
