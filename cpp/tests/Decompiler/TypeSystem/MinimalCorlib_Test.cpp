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

// Tests for `MinimalCorlib` (the port of
// ICSharpCode.Decompiler/TypeSystem/Implementation/MinimalCorlib.cs) -- the
// artificial "assembly" containing all known types and no members, the module
// `MetadataExtensions.minimalCorlibTypeProvider` builds its provider over and the
// `DecompilerTypeSystem` missing-known-types fallback appends. Every expectation is
// gold-pinned against the REAL installed ICSharpCode.Decompiler 11.0 MinimalCorlib
// (the C:/temp-probe/McProbe probe drives the identical facts over
// `MinimalCorlib.Instance` / `CreateWithTypes` / `new SimpleCompilation(...)`, the
// saved gold_raw.txt). The cruxes:
//  - the fresh-instance-per-Resolve contract (two compilations over `Instance`
//    resolve distinct modules, each kept alive by the reference's registry)
//  - the module identity surface (corlib names, IsMainModule, MetadataFile null,
//    the reference-equality InternalsVisibleTo)
//  - the 59-type table in KnownTypeCode order with identity-stable lookups
//  - the never-populated root-namespace child list (zero children, zero types)
//  - the CorlibTypeDefinition surface (the kind matrix incl. Void's null
//    IsReferenceType, the no-arity FullName vs the arity ReflectionName, the dummy
//    type parameters, the FindType-driven DirectBaseTypes, the visit-children
//    no-op, the reference equality)

#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Impl = TS::Implementation;

namespace {

// Records which Visit* method the dispatch reached.
class RecordingVisitor : public TS::TypeVisitor {
public:
    std::string last = "<none>";
    TS::ITypePtr VisitTypeDefinition(TS::ITypeDefinition& type) override
    {
        last = "VisitTypeDefinition";
        return TS::TypeVisitor::VisitTypeDefinition(type);
    }
};

// A compilation over `MinimalCorlib::Instance()` with no referenced assemblies --
// the gold probe's `new SimpleCompilation(MinimalCorlib.Instance)` shape. The
// singleton reference outlives the compilation (process lifetime), so the
// compilation's non-owning module pointers stay valid.
struct CorlibFixture {
    TS::SimpleCompilation compilation;

    CorlibFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {})
    {
    }

    const TS::IModule& Module() const { return compilation.MainModule(); }
};

// The gold `TopLevelTypeName(ns, name, tpc)` lookup spelling.
TS::TopLevelTypeName Name(const std::string& ns, const std::string& name, int tpc = 0)
{
    return TS::TopLevelTypeName(ns, name, tpc);
}

} // namespace

TEST(MinimalCorlibTest, InstanceResolvesAFreshModulePerCompilation)
{
    CorlibFixture a;
    TS::SimpleCompilation b(Impl::MinimalCorlib::Instance(), {});
    // Gold: resolveDistinct=True -- every Resolve materializes a fresh module.
    EXPECT_NE(&a.Module(), &b.MainModule());
    // Gold: modulesSnapshotAllSame=True -- the snapshot holds the one main module.
    ASSERT_EQ(a.compilation.Modules().size(), 1u);
    EXPECT_EQ(a.compilation.Modules().front(), &a.Module());
    // The module IS the MinimalCorlib concrete class (gold: type=MinimalCorlib).
    EXPECT_NE(dynamic_cast<const Impl::MinimalCorlib*>(&a.Module()), nullptr);
}

TEST(MinimalCorlibTest, ModuleIdentitySurface)
{
    CorlibFixture f;
    const TS::IModule& m = f.Module();
    // Gold: asmName=corlib / asmVersion=0.0.0.0 / fullAsmName=corlib / name=corlib.
    EXPECT_EQ(m.AssemblyName(), "corlib");
    EXPECT_EQ(m.AssemblyVersion(), TS::Version(0, 0, 0, 0));
    EXPECT_EQ(m.FullAssemblyName(), "corlib");
    EXPECT_EQ(m.Name(), "corlib");
    EXPECT_EQ(m.SymbolKind(), TS::SymbolKind::Module);
    // Gold: isMain=True (it is the main module of its own compilation).
    EXPECT_TRUE(m.IsMainModule());
    // Gold: metadataFileNull=True.
    EXPECT_EQ(m.MetadataFile(), nullptr);
    // Gold: the reference-equality InternalsVisibleTo -- a SECOND minimal corlib
    // instance is NOT visible.
    TS::SimpleCompilation other(Impl::MinimalCorlib::Instance(), {});
    EXPECT_TRUE(m.InternalsVisibleTo(m));
    EXPECT_FALSE(m.InternalsVisibleTo(other.MainModule()));
    // Gold: asmAttrs=0 / modAttrs=0.
    EXPECT_TRUE(m.GetAssemblyAttributes().empty());
    EXPECT_TRUE(m.GetModuleAttributes().empty());
    // Gold: modulesCount=1 / referencedCount=0.
    EXPECT_EQ(f.compilation.Modules().size(), 1u);
    EXPECT_TRUE(f.compilation.ReferencedModules().empty());
}

TEST(MinimalCorlibTest, TypeDefinitionTableMatchesAllKnownTypes)
{
    CorlibFixture f;
    const TS::IModule& m = f.Module();
    const auto tds = m.TopLevelTypeDefinitions();
    const auto all = TS::KnownTypeReference::AllKnownTypes();
    // Gold: allKnownTypesCount=59 / topLevelCount=59 / typesCount=59.
    ASSERT_EQ(all.size(), 59u);
    ASSERT_EQ(tds.size(), 59u);
    EXPECT_EQ(m.TypeDefinitions().size(), 59u);
    // Gold: orderMatchesAllKnownTypes=True -- the table order IS the KnownTypeCode
    // order, and TypeDefinitions yields the SAME instances as TopLevelTypeDefinitions
    // (sameEnumerables=True).
    const auto types = m.TypeDefinitions();
    for (std::size_t i = 0; i < tds.size(); ++i) {
        EXPECT_EQ(tds[i], types[i]) << "instance identity at " << i;
        EXPECT_EQ(static_cast<int>(tds[i]->KnownTypeCode()),
                  static_cast<int>(all[i]->Code())) << "order at " << i;
    }
    // Spot gold rows: td[0]:Object:System.Object ... td[16]:String ...
    // td[42]:NullableOfT:System.Nullable`1 ... td[58]:Range:System.Range.
    EXPECT_EQ(tds[0]->KnownTypeCode(), TS::KnownTypeCode::Object);
    EXPECT_EQ(tds[0]->ReflectionName(), "System.Object");
    EXPECT_EQ(tds[16]->KnownTypeCode(), TS::KnownTypeCode::String);
    EXPECT_EQ(tds[16]->ReflectionName(), "System.String");
    EXPECT_EQ(tds[42]->KnownTypeCode(), TS::KnownTypeCode::NullableOfT);
    EXPECT_EQ(tds[42]->ReflectionName(), "System.Nullable`1");
    EXPECT_EQ(tds[58]->KnownTypeCode(), TS::KnownTypeCode::Range);
    EXPECT_EQ(tds[58]->ReflectionName(), "System.Range");
}

TEST(MinimalCorlibTest, GetTypeDefinitionLookupMatrix)
{
    CorlibFixture f;
    const TS::IModule& m = f.Module();
    // Gold: lookup(System.String)=System.String -- and the lookups are
    // identity-stable (equalsOtherLookup=True in the gold).
    const TS::ITypeDefinition* str = m.GetTypeDefinition(Name("System", "String"));
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->ReflectionName(), "System.String");
    EXPECT_EQ(m.GetTypeDefinition(Name("System", "String")), str);
    // Gold: lookup(System.Nullable`1)=System.Nullable`1 -- the metadata name is
    // "Nullable" with the arity in the type-parameter count.
    const TS::ITypeDefinition* nullable = m.GetTypeDefinition(Name("System", "Nullable", 1));
    ASSERT_NE(nullable, nullptr);
    EXPECT_EQ(nullable->ReflectionName(), "System.Nullable`1");
    // Gold misses: ("System", "Nullable") [wrong arity], ("System", "Nullable`1", 1)
    // [the arity is not part of the name], ("System", "String", 1), ("System",
    // "Bogus"), ("Bogus", "String").
    EXPECT_EQ(m.GetTypeDefinition(Name("System", "Nullable")), nullptr);
    EXPECT_EQ(m.GetTypeDefinition(Name("System", "Nullable`1", 1)), nullptr);
    EXPECT_EQ(m.GetTypeDefinition(Name("System", "String", 1)), nullptr);
    EXPECT_EQ(m.GetTypeDefinition(Name("System", "Bogus")), nullptr);
    EXPECT_EQ(m.GetTypeDefinition(Name("Bogus", "String")), nullptr);
}

TEST(MinimalCorlibTest, RootNamespaceIsTheNeverPopulatedQuirk)
{
    CorlibFixture f;
    const TS::IModule& m = f.Module();
    const TS::INamespace& root = m.RootNamespace();
    // Gold: rootType=CorlibNamespace (the nested class), empty name forms.
    EXPECT_NE(dynamic_cast<const Impl::MinimalCorlib::CorlibNamespace*>(&root), nullptr);
    EXPECT_EQ(root.FullName(), "");
    EXPECT_EQ(root.Name(), "");
    EXPECT_EQ(root.ExternAlias(), "");
    EXPECT_EQ(root.ParentNamespace(), nullptr);
    EXPECT_EQ(root.SymbolKind(), TS::SymbolKind::Namespace);
    // Gold: rootChildren=0 / rootTypes=0 / rootGetChildSystem=null -- the
    // childNamespaces list is never populated and no known type lives in the empty
    // namespace.
    EXPECT_TRUE(root.ChildNamespaces().empty());
    EXPECT_TRUE(root.Types().empty());
    EXPECT_EQ(root.GetChildNamespace("System"), nullptr);
    // Gold: rootContributing=1:same.
    const auto contributing = root.ContributingModules();
    ASSERT_EQ(contributing.size(), 1u);
    EXPECT_EQ(contributing.front(), &m);
    // Gold: rootLookupString=null -- the namespace's own lookup composes the
    // (empty) namespace FullName into the TopLevelTypeName, so "String" misses.
    EXPECT_EQ(root.GetTypeDefinition("String", 0), nullptr);
    // Gold: rootCompilationSame=True.
    EXPECT_EQ(&root.Compilation(), &f.compilation);
}

TEST(MinimalCorlibTest, CorlibTypeDefinitionKindMatrix)
{
    CorlibFixture f;
    const TS::IModule& m = f.Module();
    // Gold td(Object): kind=Class, isRef=True, directBases=[] (Object has no base).
    const TS::ITypeDefinition* obj = m.GetTypeDefinition(Name("System", "Object"));
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->Kind(), TS::TypeKind::Class);
    EXPECT_EQ(obj->IsReferenceType(), std::optional<bool>(true));
    EXPECT_TRUE(obj->DirectBaseTypes().empty());
    // Gold td(Int32): kind=Struct, isRef=False, isSealed=True,
    // directBases=[System.ValueType].
    const TS::ITypeDefinition* i32 = m.GetTypeDefinition(Name("System", "Int32"));
    ASSERT_NE(i32, nullptr);
    EXPECT_EQ(i32->Kind(), TS::TypeKind::Struct);
    EXPECT_EQ(i32->IsReferenceType(), std::optional<bool>(false));
    EXPECT_TRUE(i32->IsSealed());
    ASSERT_EQ(i32->DirectBaseTypes().size(), 1u);
    EXPECT_EQ(i32->DirectBaseTypes().front()->ReflectionName(), "System.ValueType");
    // Gold td(IDisposable): kind=Interface, isAbstract=True,
    // directBases=[System.Object].
    const TS::ITypeDefinition* disposable = m.GetTypeDefinition(Name("System", "IDisposable"));
    ASSERT_NE(disposable, nullptr);
    EXPECT_EQ(disposable->Kind(), TS::TypeKind::Interface);
    EXPECT_EQ(disposable->IsReferenceType(), std::optional<bool>(true));
    EXPECT_TRUE(disposable->IsAbstract());
    EXPECT_FALSE(disposable->IsSealed());
    ASSERT_EQ(disposable->DirectBaseTypes().size(), 1u);
    EXPECT_EQ(disposable->DirectBaseTypes().front()->ReflectionName(), "System.Object");
    // Gold td(Void): kind=Void, isRef= (null!) -- the kind switch's default arm.
    const TS::ITypeDefinition* void_ = m.GetTypeDefinition(Name("System", "Void"));
    ASSERT_NE(void_, nullptr);
    EXPECT_EQ(void_->Kind(), TS::TypeKind::Void);
    EXPECT_EQ(void_->IsReferenceType(), std::nullopt);
    // Gold td(MulticastDelegate): directBases=[System.Delegate].
    const TS::ITypeDefinition* mcd = m.GetTypeDefinition(Name("System", "MulticastDelegate"));
    ASSERT_NE(mcd, nullptr);
    ASSERT_EQ(mcd->DirectBaseTypes().size(), 1u);
    EXPECT_EQ(mcd->DirectBaseTypes().front()->ReflectionName(), "System.Delegate");
    // Gold td(TaskOfT): ns=System.Threading.Tasks, metadataName=Task`1,
    // directBases=[System.Threading.Tasks.Task] (the non-generic Task in the same
    // module, resolved through FindType).
    const TS::ITypeDefinition* taskOfT = m.GetTypeDefinition(Name("System.Threading.Tasks", "Task", 1));
    ASSERT_NE(taskOfT, nullptr);
    EXPECT_EQ(taskOfT->Namespace(), "System.Threading.Tasks");
    EXPECT_EQ(taskOfT->MetadataName(), "Task`1");
    ASSERT_EQ(taskOfT->DirectBaseTypes().size(), 1u);
    EXPECT_EQ(taskOfT->DirectBaseTypes().front()->ReflectionName(), "System.Threading.Tasks.Task");
}

TEST(MinimalCorlibTest, CorlibTypeDefinitionNameForms)
{
    CorlibFixture f;
    const TS::IModule& m = f.Module();
    // Gold td(NullableOfT): name=Nullable, reflectionName=System.Nullable`1 (WITH
    // the arity), fullName=System.Nullable (NO arity), metadataName=Nullable`1,
    // tpc=1, the dummy class type parameters.
    const TS::ITypeDefinition* nullable = m.GetTypeDefinition(Name("System", "Nullable", 1));
    ASSERT_NE(nullable, nullptr);
    EXPECT_EQ(nullable->Name(), "Nullable");
    EXPECT_EQ(nullable->ReflectionName(), "System.Nullable`1");
    EXPECT_EQ(nullable->FullName(), "System.Nullable");
    EXPECT_EQ(nullable->MetadataName(), "Nullable`1");
    EXPECT_EQ(nullable->TypeParameterCount(), 1);
    const auto tps = nullable->TypeParameters();
    ASSERT_EQ(tps.size(), 1u);
    EXPECT_EQ(tps.front()->Name(), "!0");
    EXPECT_EQ(tps.front()->ReflectionName(), "`0");
    // Gold td(String): metadataName=String (no arity), fullName=System.String.
    const TS::ITypeDefinition* str = m.GetTypeDefinition(Name("System", "String"));
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->MetadataName(), "String");
    EXPECT_EQ(str->FullName(), "System.String");
    EXPECT_TRUE(str->TypeParameters().empty());
    // Gold td(TaskOfT): fullName=System.Threading.Tasks.Task (the metadata name
    // without the arity).
    const TS::ITypeDefinition* taskOfT = m.GetTypeDefinition(Name("System.Threading.Tasks", "Task", 1));
    ASSERT_NE(taskOfT, nullptr);
    EXPECT_EQ(taskOfT->FullName(), "System.Threading.Tasks.Task");
}

TEST(MinimalCorlibTest, CorlibTypeDefinitionFlagAndMemberSurface)
{
    CorlibFixture f;
    const TS::IModule& m = f.Module();
    const TS::ITypeDefinition* str = m.GetTypeDefinition(Name("System", "String"));
    ASSERT_NE(str, nullptr);
    // Gold: isStatic=False / isAbstract=False / isSealed=False /
    // accessibility=Public / isRecord=False / isReadOnly=False /
    // hasExtensions=False / extensionInfoNull=True / isByRefLike=False /
    // nullability=Oblivious / nullableContext=Oblivious.
    EXPECT_FALSE(str->IsStatic());
    EXPECT_FALSE(str->IsAbstract());
    EXPECT_FALSE(str->IsSealed());
    EXPECT_EQ(str->Accessibility(), TS::Accessibility::Public);
    EXPECT_FALSE(str->IsRecord());
    EXPECT_FALSE(str->IsReadOnly());
    EXPECT_FALSE(str->HasExtensions());
    EXPECT_EQ(str->ExtensionInfo(), nullptr);
    EXPECT_FALSE(str->IsByRefLike());
    EXPECT_EQ(str->Nullability(), TS::Nullability::Oblivious);
    EXPECT_EQ(str->NullableContext(), TS::Nullability::Oblivious);
    // Gold: enumUnderlying=? (the SpecialType.UnknownType singleton).
    ASSERT_NE(str->EnumUnderlyingType(), nullptr);
    EXPECT_EQ(str->EnumUnderlyingType()->ReflectionName(), "?");
    // Gold: the nil TypeDef handle as the raw token 0x02000000.
    EXPECT_EQ(str->MetadataToken(), 0x02000000u);
    // Gold: toString=[MinimalCorlibType String] -- the enum member name.
    const auto* corlibStr = dynamic_cast<const Impl::MinimalCorlib::CorlibTypeDefinition*>(str);
    ASSERT_NE(corlibStr, nullptr);
    EXPECT_EQ(corlibStr->ToString(), "[MinimalCorlibType String]");
    const auto* corlibNullable = dynamic_cast<const Impl::MinimalCorlib::CorlibTypeDefinition*>(
        m.GetTypeDefinition(Name("System", "Nullable", 1)));
    ASSERT_NE(corlibNullable, nullptr);
    EXPECT_EQ(corlibNullable->ToString(), "[MinimalCorlibType NullableOfT]");
    // Gold: parentModuleSame=True / declaringTypeNull=True / declaringTypeDefNull=True.
    EXPECT_EQ(str->ParentModule(), &m);
    EXPECT_EQ(str->DeclaringType(), nullptr);
    EXPECT_EQ(str->DeclaringTypeDefinition(), nullptr);
    // Gold: members=0 / nested=0 / fields=0 / methods=0 / ctors=0 / attributes=0 /
    // hasAttr=False / getAttrNull=True.
    EXPECT_TRUE(str->NestedTypes().empty());
    EXPECT_TRUE(str->Members().empty());
    EXPECT_TRUE(str->GetFields().empty());
    EXPECT_TRUE(str->GetMethods().empty());
    EXPECT_TRUE(str->GetConstructors().empty());
    EXPECT_TRUE(str->GetAttributes().empty());
    EXPECT_FALSE(str->HasAttribute(TS::KnownAttribute::Obsolete));
    EXPECT_EQ(str->GetAttribute(TS::KnownAttribute::Obsolete), nullptr);
}

TEST(MinimalCorlibTest, NullabilityChangeEqualityAndVisitors)
{
    CorlibFixture f;
    const auto& compilation = f.compilation;
    const TS::ITypeDefinition* str = compilation.MainModule().GetTypeDefinition(Name("System", "String"));
    ASSERT_NE(str, nullptr);
    // `ChangeNullability` / `AcceptVisitor` / `VisitChildren` are NON-CONST on the
    // port's IType (the C# instance methods may return `this` as
    // shared_from_this); the module-owned definition is called through a mutable
    // handle (the const_cast is the probe-equivalent, the TypeProvider mutable-
    // handle precedent).
    TS::ITypeDefinition& strMut = const_cast<TS::ITypeDefinition&>(*str);
    // Gold: changeObliviousSame=True -- Oblivious returns this.
    EXPECT_EQ(strMut.ChangeNullability(TS::Nullability::Oblivious).get(), str);
    // Gold: changeAnnotatedType=NullabilityAnnotatedType with
    // annotatedNullability=Nullable / annotatedBase=System.String.
    const auto annotated = strMut.ChangeNullability(TS::Nullability::Nullable);
    EXPECT_NE(annotated.get(), str);
    EXPECT_EQ(annotated->Nullability(), TS::Nullability::Nullable);
    EXPECT_EQ(annotated->ReflectionName(), "System.String");
    // Gold: equalsSelf=True / equalsOtherLookup=True (the identity-stable lookup) /
    // equalsInt32=False.
    EXPECT_TRUE(str->Equals(*str));
    EXPECT_TRUE(str->Equals(*compilation.MainModule().GetTypeDefinition(Name("System", "String"))));
    EXPECT_FALSE(str->Equals(*compilation.MainModule().GetTypeDefinition(Name("System", "Int32"))));
    // Gold: getDefSelf=True.
    EXPECT_EQ(str->GetDefinition(), str);
    // Gold: acceptVisitor=VisitTypeDefinition.
    RecordingVisitor recording;
    strMut.AcceptVisitor(recording);
    EXPECT_EQ(recording.last, "VisitTypeDefinition");
    // Gold: visitChildren=<none> -- VisitChildren returns this WITHOUT calling any
    // Visit* method.
    RecordingVisitor recording2;
    const auto visited = strMut.VisitChildren(recording2);
    EXPECT_EQ(recording2.last, "<none>");
    EXPECT_EQ(visited.get(), str);
}

TEST(MinimalCorlibTest, FindTypeResolvesTheCorlibDefinitions)
{
    CorlibFixture f;
    const auto& compilation = f.compilation;
    // Gold: findTypeString=System.String / findTypeStringIsDef=True -- the
    // KnownTypeCache -> SearchType -> module.GetTypeDefinition path resolves the
    // CorlibTypeDefinition itself.
    const TS::IType& found = compilation.FindType(TS::KnownTypeCode::String);
    EXPECT_EQ(found.ReflectionName(), "System.String");
    EXPECT_EQ(found.GetDefinition(),
              compilation.MainModule().GetTypeDefinition(Name("System", "String")));
}

TEST(MinimalCorlibTest, CreateWithTypesMaterializesOnlyTheSubset)
{
    // Gold: a reference over {Object, String} only.
    auto subset = Impl::MinimalCorlib::CreateWithTypes(
        {TS::KnownTypeReference::Get(TS::KnownTypeCode::Object),
         TS::KnownTypeReference::Get(TS::KnownTypeCode::String)});
    // Gold: subsetDistinctRef=True (CreateWithTypes builds its own reference).
    EXPECT_NE(subset.get(), &Impl::MinimalCorlib::Instance());
    TS::SimpleCompilation compilation(*subset, {});
    const TS::IModule& m = compilation.MainModule();
    // Gold: subsetCount=2 / subsetString=True / subsetInt32=True (the lookup is
    // null).
    EXPECT_EQ(m.TopLevelTypeDefinitions().size(), 2u);
    EXPECT_NE(m.GetTypeDefinition(Name("System", "String")), nullptr);
    EXPECT_EQ(m.GetTypeDefinition(Name("System", "Int32")), nullptr);
    // Gold: subsetResolveDistinct=True -- distinct from the Instance module.
    CorlibFixture full;
    EXPECT_NE(&m, &full.Module());
    // Gold: subsetFindInt32=Unknown / subsetFindInt32ReflectionName=System.Int32 --
    // the KnownTypeCache falls to the UnknownType (namespaceKnown=true) arm.
    const TS::IType& i32 = compilation.FindType(TS::KnownTypeCode::Int32);
    EXPECT_EQ(i32.Kind(), TS::TypeKind::Unknown);
    EXPECT_EQ(i32.ReflectionName(), "System.Int32");
}

namespace {

// Pins the lifted `DummyTypeParameter::GetClassTypeParameterList` (the C# internal
// list cache the CorlibTypeDefinition's TypeParameters reads): the entry-i list is
// the first i class dummies, the instances are the per-index cache instances, and
// the same pointers come back for the same length.
TEST(MinimalCorlibTest, GetClassTypeParameterListGrowsLazilyAndIsStable)
{
    using TS::Implementation::DummyTypeParameter;
    // length 0: the empty list (the C# array initializer's EmptyList entry).
    EXPECT_TRUE(DummyTypeParameter::GetClassTypeParameterList(0).empty());
    // length 1: [dummy 0]; the SAME instance as GetClassTypeParameter(0).
    const auto list1 = DummyTypeParameter::GetClassTypeParameterList(1);
    ASSERT_EQ(list1.size(), 1u);
    EXPECT_EQ(list1.front(), DummyTypeParameter::GetClassTypeParameter(0).get());
    EXPECT_EQ(list1.front()->Name(), "!0");
    // length 2: [dummy 0, dummy 1] -- the earlier dummies are shared, not rebuilt.
    const auto list2 = DummyTypeParameter::GetClassTypeParameterList(2);
    ASSERT_EQ(list2.size(), 2u);
    EXPECT_EQ(list2[0], list1.front());
    EXPECT_EQ(list2[1], DummyTypeParameter::GetClassTypeParameter(1).get());
    EXPECT_EQ(list2[1]->Name(), "!1");
    // Stability: the same pointers come back for the same length.
    const auto again = DummyTypeParameter::GetClassTypeParameterList(2);
    ASSERT_EQ(again.size(), list2.size());
    for (std::size_t i = 0; i < again.size(); ++i)
        EXPECT_EQ(again[i], list2[i]);
    // A negative length throws (the C# grow-loop would throw IndexOutOfRange at the
    // final `tps[length]`).
    EXPECT_THROW(DummyTypeParameter::GetClassTypeParameterList(-1), std::out_of_range);
}

} // namespace

// The C#-shape static pins: the module and its three nested classes are sealed.
static_assert(std::is_final<Impl::MinimalCorlib>::value, "MinimalCorlib is sealed/final");
static_assert(std::is_base_of<TS::IModule, Impl::MinimalCorlib>::value,
              "MinimalCorlib implements IModule");
static_assert(std::is_base_of<TS::IModuleReference, Impl::MinimalCorlib::CorlibModuleReference>::value,
              "CorlibModuleReference implements IModuleReference");
static_assert(std::is_base_of<TS::INamespace, Impl::MinimalCorlib::CorlibNamespace>::value,
              "CorlibNamespace implements INamespace");
static_assert(std::is_base_of<TS::ITypeDefinition, Impl::MinimalCorlib::CorlibTypeDefinition>::value,
              "CorlibTypeDefinition implements ITypeDefinition");
