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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `ReflectionHelper.ParseReflectionName`/`ResolveTypeName`/
// `ReadTypeParameterCount` port (ReflectionHelper.cs lines 131-243), the
// `ReflectionNameParseException` class, the `ICompilation.FindModuleByAssemblyNameInfo`
// extension (TypeSystemExtensions.cs line 840), and the `UnknownType(FullTypeName)` ctor
// the nested/simple fallbacks construct. Every expectation is pinned against the real
// installed ICSharpCode.Decompiler 11.0 driven over a SimpleCompilation of the real
// mscorlib 4.8 (main) + System.dll through the C:/temp-probe/RhpProbe gold probe
// (rhp_gold.txt); the port tests replicate each arm over the LookupStubs universe:
//
//   * the six ResolveTypeName arms (array / byref / constructed-generic / nested /
//     pointer / simple), the assembly-qualified scoping (a module hit that misses the
//     type takes the UnknownType fallback WITHOUT falling through; a module miss falls
//     through to the plain walk), the `N / ``N type-parameter arms (the wired context
//     slots and the DummyTypeParameter fallbacks), the arity-mismatch shapes (the null
//     tail entry flowing into the ParameterizedType -- the C# binds the internal
//     unchecked params-array ctor, so no ctor check fires; the write past the arity
//     throwing IndexOutOfRangeException), the parse failures (ReflectionNameParseException
//     at position 0 with "Invalid type name: <input>"), and ReadTypeParameterCount's
//     digit scan and "Expected type parameter count" throw.

#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/ReflectionNameParseException.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/Metadata/AssemblyNameInfo.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FindModuleByAssemblyNameInfo;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::ITypeResolveContext;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ParseReflectionName;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::ReadTypeParameterCount;
using ILSpy::Decompiler::TypeSystem::ReflectionNameParseException;
using ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupModule;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::Metadata::AssemblyNameInfo;

// The module universe: the main module (LookupTests, empty), LibA, and LibB -- the
// module walk visits [main, LibA, LibB] and the registrations pin which module each
// test's type answers from.
class ParseFixture {
public:
    ParseFixture()
        : libA_(compilation_, "LibA"), libB_(compilation_, "LibB")
    {
        compilation_.AddModule(&libA_);
        compilation_.AddModule(&libB_);
    }

    LookupCompilation& Compilation() { return compilation_; }

    // A shared_ptr-owned definition registered in `module` under the (namespace, name,
    // tpc) triple -- the shape IModule.GetTypeDefinition answers with.
    std::shared_ptr<LookupTypeDefinition> RegisterType(
        LookupModule& module, const std::string& ns, const std::string& name, int tpc)
    {
        auto def = std::make_shared<LookupTypeDefinition>(
            ns.empty() ? name : ns + "." + name, ns,
            FullTypeName(TopLevelTypeName(ns, name, tpc)),
            TypeKind::Class, Accessibility::Public, compilation_, &module);
        module.SetTypeDefinition(TopLevelTypeName(ns, name, tpc), def.get());
        return def;
    }

    LookupModule& LibA() { return libA_; }
    LookupModule& LibB() { return libB_; }

    SimpleTypeResolveContext Context() { return SimpleTypeResolveContext(compilation_); }

private:
    LookupCompilation compilation_;
    LookupModule libA_;
    LookupModule libB_;
};

// Runs `ParseReflectionName` and returns the parsed IType (asserting no throw).
ITypePtr MustParse(const std::string& name, const ITypeResolveContext& ctx)
{
    return ParseReflectionName(name, ctx);
}

// Runs `ParseReflectionName` and returns the thrown ReflectionNameParseException's
// (position, message) (asserting the throw).
ReflectionNameParseException MustReject(const std::string& name, const ITypeResolveContext& ctx)
{
    try {
        ParseReflectionName(name, ctx);
    } catch (const ReflectionNameParseException& ex) {
        return ReflectionNameParseException(ex.Position(), ex.what());
    }
    ADD_FAILURE() << "expected ReflectionNameParseException for: " << name;
    return ReflectionNameParseException(0, "<no-throw>");
}

TEST(ParseReflectionNameTest, SimpleArmModuleWalk)
{
    // Gold s1/s2: the plain walk over Compilation.Modules() -- the first module whose
    // GetTypeDefinition answers wins (the main module is first; a type registered in
    // a LATER module is still found after the earlier modules miss).
    ParseFixture f;
    auto foo = f.RegisterType(f.LibA(), "Test", "Foo", 0);
    auto bar = f.RegisterType(f.LibB(), "Test", "Bar", 0);

    auto ctx = f.Context();
    auto fooType = MustParse("Test.Foo", ctx);
    ASSERT_NE(fooType, nullptr);
    EXPECT_EQ(fooType.get(), static_cast<const IType*>(foo.get()));
    // Gold s4: an arity-suffixed name resolves to the matching definition (the
    // TopLevelTypeName ctor strips the `N into the lookup's type-parameter count).
    auto listDef = f.RegisterType(f.LibA(), "Test", "List", 1);
    auto listType = MustParse("Test.List`1", ctx);
    EXPECT_EQ(listType.get(), static_cast<const IType*>(listDef.get()));

    auto barType = MustParse("Test.Bar", ctx);
    EXPECT_EQ(barType.get(), static_cast<const IType*>(bar.get()));
}

TEST(ParseReflectionNameTest, SimpleArmUnknownFallback)
{
    // Gold s3: the walk miss constructs UnknownType(topLevelTypeName) -- the known
    // name (Name/ReflectionName/TypeParameterCount) over the parsed top-level name.
    ParseFixture f;
    auto ctx = f.Context();
    auto type = MustParse("Test.Missing", ctx);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->Kind(), TypeKind::Unknown);
    auto* unknown = dynamic_cast<class ILSpy::Decompiler::TypeSystem::UnknownType*>(type.get());
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->Name(), "Missing");
    EXPECT_EQ(unknown->ReflectionName(), "Test.Missing");
    EXPECT_EQ(unknown->TypeParameterCount(), 0);
    EXPECT_EQ(unknown->FullTypeName().ReflectionName(), "Test.Missing");
    EXPECT_EQ(unknown->FullTypeName().GetTopLevelTypeName().Namespace(), "Test");
    EXPECT_FALSE(unknown->FullTypeName().IsNested());
}

TEST(ParseReflectionNameTest, AssemblyQualifiedArm)
{
    // Gold a1-a7: the assembly-qualified arm routes through FindModuleByAssemblyNameInfo.
    ParseFixture f;
    auto fooA = f.RegisterType(f.LibA(), "Test", "Foo", 0);
    auto ctx = f.Context();

    // a1/a2: the Name pass (case-insensitive) finds LibA and the type answers.
    auto t1 = MustParse("Test.Foo, LibA", ctx);
    EXPECT_EQ(t1.get(), static_cast<const IType*>(fooA.get()));
    auto t2 = MustParse("Test.Foo, LIBA", ctx);
    EXPECT_EQ(t2.get(), static_cast<const IType*>(fooA.get()));

    // a3: the FullName pass (the module's FullAssemblyName vs the parsed FullName).
    f.LibA().SetFullAssemblyName("LibA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null");
    auto t3 = MustParse("Test.Foo, LibA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
                        ctx);
    EXPECT_EQ(t3.get(), static_cast<const IType*>(fooA.get()));

    // a5: a module HIT that misses the type takes the UnknownType fallback -- the walk
    // below is NOT consulted (Foo lives in LibA but the name asked for LibB).
    auto t4 = MustParse("Test.Foo, LibB", ctx);
    auto* unknown = dynamic_cast<class ILSpy::Decompiler::TypeSystem::UnknownType*>(t4.get());
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->Name(), "Foo");
    EXPECT_EQ(unknown->ReflectionName(), "Test.Foo");

    // a6: a module MISS falls through to the plain walk (Foo found in LibA).
    auto t5 = MustParse("Test.Foo, NoSuchAssembly", ctx);
    EXPECT_EQ(t5.get(), static_cast<const IType*>(fooA.get()));

    // a7: the hit-module type miss with a no-walk fallback.
    auto t6 = MustParse("Test.Missing, LibA", ctx);
    auto* unknown2 = dynamic_cast<class ILSpy::Decompiler::TypeSystem::UnknownType*>(t6.get());
    ASSERT_NE(unknown2, nullptr);
    EXPECT_EQ(unknown2->Name(), "Missing");
}

TEST(ParseReflectionNameTest, ArrayByRefPointerArms)
{
    // Gold r1/r2/r5/r6/r7: the decorator arms over the resolved element.
    ParseFixture f;
    auto foo = f.RegisterType(f.LibA(), "Test", "Foo", 0);
    auto ctx = f.Context();

    auto arr1 = MustParse("Test.Foo[]", ctx);
    auto* a1 = dynamic_cast<ArrayType*>(arr1.get());
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->Kind(), TypeKind::Array);
    EXPECT_EQ(a1->Rank(), 1);
    EXPECT_EQ(a1->Element().get(), static_cast<const IType*>(foo.get()));
    EXPECT_EQ(a1->ReflectionName(), "Test.Foo[]");

    auto arr2 = MustParse("Test.Foo[,]", ctx);
    auto* a2 = dynamic_cast<ArrayType*>(arr2.get());
    ASSERT_NE(a2, nullptr);
    EXPECT_EQ(a2->Rank(), 2);
    EXPECT_EQ(a2->ReflectionName(), "Test.Foo[,]");

    auto byRef = MustParse("Test.Foo&", ctx);
    auto* b = dynamic_cast<ByReferenceType*>(byRef.get());
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->Kind(), TypeKind::ByReference);
    EXPECT_EQ(b->Element().get(), static_cast<const IType*>(foo.get()));

    auto ptr = MustParse("Test.Foo*", ctx);
    auto* p = dynamic_cast<PointerType*>(ptr.get());
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->Kind(), TypeKind::Pointer);
    EXPECT_EQ(p->Element().get(), static_cast<const IType*>(foo.get()));

    // r7: the decorators nest through the element resolution (an array by reference).
    auto byRefArr = MustParse("Test.Foo[]&", ctx);
    auto* b2 = dynamic_cast<ByReferenceType*>(byRefArr.get());
    ASSERT_NE(b2, nullptr);
    auto* inner = dynamic_cast<ArrayType*>(b2->Element().get());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->Element().get(), static_cast<const IType*>(foo.get()));

    // r3: the [*,*] rank spelling does NOT parse -- the ReflectionNameParseException
    // carries position 0 and the "Invalid type name: <input>" message.
    auto ex = MustReject("Test.Foo[*,*]", ctx);
    EXPECT_EQ(ex.Position(), 0);
    EXPECT_STREQ(ex.what(), "Invalid type name: Test.Foo[*,*]");
}

TEST(ParseReflectionNameTest, ConstructedGenericArm)
{
    // Gold g1/g2: the constructed-generic arm builds a ParameterizedType over the
    // resolved generic definition and the resolved arguments.
    ParseFixture f;
    auto list = f.RegisterType(f.LibA(), "Test", "List", 1);
    auto dict = f.RegisterType(f.LibA(), "Test", "Dict", 2);
    auto foo = f.RegisterType(f.LibA(), "Test", "Foo", 0);
    auto bar = f.RegisterType(f.LibA(), "Test", "Bar", 0);
    auto ctx = f.Context();

    auto t1 = MustParse("Test.List`1[[Test.Foo]]", ctx);
    auto* pt1 = dynamic_cast<ParameterizedType*>(t1.get());
    ASSERT_NE(pt1, nullptr);
    EXPECT_EQ(pt1->GenericType().get(), static_cast<const IType*>(list.get()));
    ASSERT_EQ(pt1->TypeArguments().size(), 1u);
    EXPECT_EQ(pt1->TypeArguments()[0].get(), static_cast<const IType*>(foo.get()));
    EXPECT_EQ(pt1->TypeParameterCount(), 1);

    auto t2 = MustParse("Test.Dict`2[[Test.Foo],[Test.Bar]]", ctx);
    auto* pt2 = dynamic_cast<ParameterizedType*>(t2.get());
    ASSERT_NE(pt2, nullptr);
    EXPECT_EQ(pt2->GenericType().get(), static_cast<const IType*>(dict.get()));
    ASSERT_EQ(pt2->TypeArguments().size(), 2u);
    EXPECT_EQ(pt2->TypeArguments()[0].get(), static_cast<const IType*>(foo.get()));
    EXPECT_EQ(pt2->TypeArguments()[1].get(), static_cast<const IType*>(bar.get()));

    // g4: the degenerate arm -- a resolved generic with TypeParameterCount 0 is
    // returned AS-IS (the "System.Int32[[System.String]]" shape: the parser accepts
    // arguments after a non-generic name and the resolution drops them).
    auto t3 = MustParse("Test.Foo[[Test.Bar]]", ctx);
    EXPECT_EQ(t3.get(), static_cast<const IType*>(foo.get()));

    // g5: FEWER arguments than the resolved generic's arity -- the null tail entry
    // flows into the ParameterizedType (the C# binds the internal unchecked
    // params-array ctor, so no ctor check fires; the gold dump shows the constructed
    // type's own getters NRE-ing on the null -- the port renders a "?" there).
    auto genMiss = MustParse("Test.List`2[[Test.Foo]]", ctx);
    auto* pt5 = dynamic_cast<ParameterizedType*>(genMiss.get());
    ASSERT_NE(pt5, nullptr);
    // "Test.List`2" itself resolved to an UnknownType (no List of arity 2 is
    // registered), whose TypeParameterCount (2) sizes the argument vector.
    auto* genUnknown = dynamic_cast<class ILSpy::Decompiler::TypeSystem::UnknownType*>(pt5->GenericType().get());
    ASSERT_NE(genUnknown, nullptr);
    EXPECT_EQ(genUnknown->TypeParameterCount(), 2);
    ASSERT_EQ(pt5->TypeArguments().size(), 2u);
    EXPECT_EQ(pt5->TypeArguments()[0].get(), static_cast<const IType*>(foo.get()));
    EXPECT_EQ(pt5->TypeArguments()[1], nullptr);

    // g6: MORE arguments than the arity -- the C# IndexOutOfRangeException writing
    // past the array, mapped to std::out_of_range with the .NET message.
    bool threw = false;
    try {
        ParseReflectionName("Test.List`1[[Test.Foo],[Test.Bar]]", ctx);
    } catch (const std::out_of_range& ex) {
        threw = true;
        EXPECT_STREQ(ex.what(), "Index was outside the bounds of the array.");
    }
    EXPECT_TRUE(threw);
}

TEST(ParseReflectionNameTest, NestedArm)
{
    // Gold n1/n4: the nested arm resolves the declaring type, then scans its
    // NestedTypes for the plain name and the SUMMED type-parameter count.
    ParseFixture f;
    auto outer = f.RegisterType(f.LibA(), "Test", "Outer", 0);
    auto gen = f.RegisterType(f.LibA(), "Test", "Gen", 1);
    auto ctx = f.Context();

    auto inner = std::make_shared<LookupTypeDefinition>(
        "Test.Outer.Inner", "Test",
        FullTypeName(TopLevelTypeName("Test", "Outer", 0)).NestedType("Inner", 0),
        TypeKind::Class, Accessibility::Public, f.Compilation(), &f.LibA());
    outer->SetNestedTypes({ inner.get() });
    // The nested type under a GENERIC declaring type carries the declaring type's
    // parameters in its own count (type.TypeParameterCount == 0 + declaringTpc).
    auto genInner = std::make_shared<LookupTypeDefinition>(
        "Test.Gen.Inner", "Test",
        FullTypeName(TopLevelTypeName("Test", "Gen", 1)).NestedType("Inner", 0),
        TypeKind::Struct, Accessibility::Public, f.Compilation(), &f.LibA());
    auto genParam = std::make_shared<LookupTypeParameter>("T");
    genInner->SetTypeParameters({ genParam.get() });
    gen->SetNestedTypes({ genInner.get() });

    auto t1 = MustParse("Test.Outer+Inner", ctx);
    EXPECT_EQ(t1.get(), static_cast<const IType*>(inner.get()));
    auto t2 = MustParse("Test.Gen`1+Inner", ctx);
    EXPECT_EQ(t2.get(), static_cast<const IType*>(genInner.get()));

    // n2: a nested miss falls back to UnknownType(new FullTypeName(...)) -- the nested
    // chain is preserved in the name (Name = the innermost segment, ReflectionName the
    // full Outer+Inner form, TypeParameterCount the summed count, NestingLevel 1).
    auto t3 = MustParse("Test.Outer+Missing", ctx);
    auto* unknown = dynamic_cast<class ILSpy::Decompiler::TypeSystem::UnknownType*>(t3.get());
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->Name(), "Missing");
    EXPECT_EQ(unknown->ReflectionName(), "Test.Outer+Missing");
    EXPECT_EQ(unknown->TypeParameterCount(), 0);
    EXPECT_EQ(unknown->FullTypeName().NestingLevel(), 1);
    EXPECT_TRUE(unknown->FullTypeName().IsNested());

    // n3: a declaring type that itself resolves to an UnknownType has a null
    // GetDefinition() -- the walk is skipped and the same fallback taken.
    auto t4 = MustParse("Test.Missing+Inner", ctx);
    auto* unknown2 = dynamic_cast<class ILSpy::Decompiler::TypeSystem::UnknownType*>(t4.get());
    ASSERT_NE(unknown2, nullptr);
    EXPECT_EQ(unknown2->Name(), "Inner");
    EXPECT_EQ(unknown2->ReflectionName(), "Test.Missing+Inner");

    // n5 (gold): a nested miss under a GENERIC declaring type keeps the summed
    // type-parameter count (List`1+Key -> tpc 1).
    auto t5 = MustParse("Test.Gen`1+Key", ctx);
    auto* unknown3 = dynamic_cast<class ILSpy::Decompiler::TypeSystem::UnknownType*>(t5.get());
    ASSERT_NE(unknown3, nullptr);
    EXPECT_EQ(unknown3->Name(), "Key");
    EXPECT_EQ(unknown3->TypeParameterCount(), 1);
    EXPECT_EQ(unknown3->ReflectionName(), "Test.Gen`1+Key");
}

TEST(ParseReflectionNameTest, TypeParameterArms)
{
    // Gold t1-t9: the `N (class) and ``N (method) arms against the wired context slots
    // and the DummyTypeParameter fallbacks.
    ParseFixture f;
    auto ctx = f.Context();

    // t1-t3: the plain context has neither slot wired -- the cached dummies answer.
    auto t1 = MustParse("`0", ctx);
    auto* tp1 = dynamic_cast<ITypeParameter*>(t1.get());
    ASSERT_NE(tp1, nullptr);
    EXPECT_EQ(tp1->Kind(), TypeKind::TypeParameter);
    EXPECT_EQ(tp1->Name(), "!0");
    EXPECT_EQ(tp1->ReflectionName(), "`0");
    EXPECT_EQ(tp1->Index(), 0);

    auto t3 = MustParse("``3", ctx);
    auto* tp3 = dynamic_cast<ITypeParameter*>(t3.get());
    ASSERT_NE(tp3, nullptr);
    EXPECT_EQ(tp3->Name(), "!!3");
    EXPECT_EQ(tp3->ReflectionName(), "``3");
    EXPECT_EQ(tp3->Index(), 3);

    // t4/t5: a wired CurrentTypeDefinition answers within its TypeParameterCount and
    // falls to the class dummy past it.
    auto gen = f.RegisterType(f.LibA(), "Test", "Gen", 1);
    auto param = std::make_shared<LookupTypeParameter>("T");
    gen->SetTypeParameters({ param.get() });
    auto ctxDef = ctx.WithCurrentTypeDefinition(gen.get());
    auto t4 = MustParse("`0", *ctxDef);
    EXPECT_EQ(t4.get(), static_cast<const IType*>(param.get()));
    auto t5 = MustParse("`1", *ctxDef);
    auto* tp5 = dynamic_cast<ITypeParameter*>(t5.get());
    ASSERT_NE(tp5, nullptr);
    EXPECT_EQ(tp5->Name(), "!1");

    // t6: the method form with only the type definition wired -> the method dummy.
    auto t6 = MustParse("``0", *ctxDef);
    auto* tp6 = dynamic_cast<ITypeParameter*>(t6.get());
    ASSERT_NE(tp6, nullptr);
    EXPECT_EQ(tp6->Name(), "!!0");

    // t7/t8: a wired CurrentMember (an IMethod with one type parameter) answers the
    // method form within range and falls to the method dummy past it.
    LookupMethod method("M", f.Compilation());
    auto methodParam = std::make_shared<LookupTypeParameter>("T");
    method.SetTypeParameters({ methodParam.get() });
    auto ctxMethod = ctx.WithCurrentMember(&method);
    auto t7 = MustParse("``0", *ctxMethod);
    EXPECT_EQ(t7.get(), static_cast<const IType*>(methodParam.get()));
    auto t8 = MustParse("``1", *ctxMethod);
    auto* tp8 = dynamic_cast<ITypeParameter*>(t8.get());
    ASSERT_NE(tp8, nullptr);
    EXPECT_EQ(tp8->Name(), "!!1");

    // t9: the class form with only the member wired (CurrentTypeDefinition null) ->
    // the class dummy.
    auto t9 = MustParse("`0", *ctxMethod);
    auto* tp9 = dynamic_cast<ITypeParameter*>(t9.get());
    ASSERT_NE(tp9, nullptr);
    EXPECT_EQ(tp9->Name(), "!0");

    // The non-index backtick names fall THROUGH to the plain walk (the C# arms are
    // gated on int.TryParse): "`abc" resolves as a top-level type named "`abc".
    auto t10 = MustParse("`abc", ctx);
    auto* unknown = dynamic_cast<class ILSpy::Decompiler::TypeSystem::UnknownType*>(t10.get());
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->Name(), "`abc");
    EXPECT_EQ(unknown->TypeParameterCount(), 0);
}

TEST(ParseReflectionNameTest, ParseFailures)
{
    // Gold e1-e4: the TryParse failures throw ReflectionNameParseException(0, "Invalid
    // type name: " + input) -- unterminated generic arguments, the empty string, an
    // unterminated array rank, and a leading assembly separator.
    ParseFixture f;
    auto ctx = f.Context();
    for (const std::string& name : { "List`1[[", "", "Test.Foo[][", "," }) {
        auto ex = MustReject(name, ctx);
        EXPECT_EQ(ex.Position(), 0) << "input: " << name;
        EXPECT_STREQ(ex.what(), ("Invalid type name: " + name).c_str()) << "input: " << name;
    }
}

TEST(FindModuleByAssemblyNameInfoTest, TwoPassLookup)
{
    // The C# FindModuleByAssemblyNameInfo: the FullName pass (module.FullAssemblyName
    // vs assemblyName.FullName, ordinal-ignore-case), then the Name pass, then null.
    ParseFixture f;
    f.LibA().SetFullAssemblyName("LibA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null");

    // The FullName pass (exact).
    auto full = AssemblyNameInfo::Parse(
        "LibA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null");
    auto m1 = FindModuleByAssemblyNameInfo(f.Compilation(), *full);
    EXPECT_EQ(m1, static_cast<const ILSpy::Decompiler::TypeSystem::IModule*>(&f.LibA()));

    // The FullName pass (case-insensitive).
    auto fullCi = AssemblyNameInfo::Parse(
        "LIBA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null");
    auto m2 = FindModuleByAssemblyNameInfo(f.Compilation(), *fullCi);
    EXPECT_EQ(m2, static_cast<const ILSpy::Decompiler::TypeSystem::IModule*>(&f.LibA()));

    // The Name pass: the parsed FullName matches no module's FullAssemblyName, so the
    // short names are compared (the gold's "mscorlib, Version=9.9.9.9" case).
    auto shortName = AssemblyNameInfo::Parse("LibA");
    auto m3 = FindModuleByAssemblyNameInfo(f.Compilation(), *shortName);
    EXPECT_EQ(m3, static_cast<const ILSpy::Decompiler::TypeSystem::IModule*>(&f.LibA()));
    auto mismatchedVersion = AssemblyNameInfo::Parse("lIbA, Version=9.9.9.9");
    auto m4 = FindModuleByAssemblyNameInfo(f.Compilation(), *mismatchedVersion);
    EXPECT_EQ(m4, static_cast<const ILSpy::Decompiler::TypeSystem::IModule*>(&f.LibA()));

    // The main module answers too (its Name pass).
    auto mainName = AssemblyNameInfo::Parse("lookuptests");
    auto m5 = FindModuleByAssemblyNameInfo(f.Compilation(), *mainName);
    EXPECT_EQ(m5, &f.Compilation().MainModule());

    // The miss.
    auto miss = AssemblyNameInfo::Parse("NoSuchAssembly");
    auto m6 = FindModuleByAssemblyNameInfo(f.Compilation(), *miss);
    EXPECT_EQ(m6, nullptr);
}

TEST(ReadTypeParameterCountTest, DigitScan)
{
    // Gold rtc: the digit scan returns the parsed count and advances pos past the
    // digits (leading zeros allowed); a failed parse (no digits, or an overflow) throws
    // ReflectionNameParseException(pos, "Expected type parameter count") at the scan's
    // stop position.
    int pos = 0;
    EXPECT_EQ(ReadTypeParameterCount("12abc", pos), 12);
    EXPECT_EQ(pos, 2);

    pos = 0;
    EXPECT_EQ(ReadTypeParameterCount("007x", pos), 7);
    EXPECT_EQ(pos, 3);

    pos = 2;
    EXPECT_EQ(ReadTypeParameterCount("xx42", pos), 42);
    EXPECT_EQ(pos, 4);

    pos = 0;
    try {
        ReadTypeParameterCount("abc", pos);
        FAIL() << "expected ReflectionNameParseException";
    } catch (const ReflectionNameParseException& ex) {
        EXPECT_EQ(ex.Position(), 0);
        EXPECT_STREQ(ex.what(), "Expected type parameter count");
    }

    // The overflow: the 11-digit run parses as no int -- the throw position is past
    // the consumed digits.
    pos = 0;
    try {
        ReadTypeParameterCount("99999999999", pos);
        FAIL() << "expected ReflectionNameParseException";
    } catch (const ReflectionNameParseException& ex) {
        EXPECT_EQ(ex.Position(), 11);
    }

    pos = 0;
    try {
        ReadTypeParameterCount("", pos);
        FAIL() << "expected ReflectionNameParseException";
    } catch (const ReflectionNameParseException& ex) {
        EXPECT_EQ(ex.Position(), 0);
    }
}

TEST(ReflectionNameParseExceptionTest, PositionAndMessage)
{
    // The exception surface: Position rides along with the standard message.
    ReflectionNameParseException ex0(7);
    EXPECT_EQ(ex0.Position(), 7);
    ReflectionNameParseException ex1(14, "Expected type parameter count");
    EXPECT_EQ(ex1.Position(), 14);
    EXPECT_STREQ(ex1.what(), "Expected type parameter count");
}

TEST(UnknownTypeFullTypeNameTest, FullNameCtorPreservesNestedChain)
{
    // The C# UnknownType(FullTypeName) ctor -- the nested chain is preserved in the
    // stored name (Name = the innermost segment, ReflectionName the full chain, the
    // summed TypeParameterCount), with namespaceKnown = true (the "?" render is the
    // (null namespace, name, tpc) ctor's arm only).
    class ILSpy::Decompiler::TypeSystem::UnknownType nested(FullTypeName("Test.Gen`1+Key"));
    EXPECT_EQ(nested.Name(), "Key");
    EXPECT_EQ(nested.ReflectionName(), "Test.Gen`1+Key");
    EXPECT_EQ(nested.TypeParameterCount(), 1);
    EXPECT_TRUE(nested.FullTypeName().IsNested());
    EXPECT_EQ(nested.FullTypeName().NestingLevel(), 1);

    // The top-level form through the same ctor.
    class ILSpy::Decompiler::TypeSystem::UnknownType topLevel(FullTypeName(TopLevelTypeName("Test", "Missing", 2)));
    EXPECT_EQ(topLevel.Name(), "Missing");
    EXPECT_EQ(topLevel.ReflectionName(), "Test.Missing`2");
    EXPECT_EQ(topLevel.TypeParameterCount(), 2);

    // The (namespace, name, tpc) ctor is unchanged: the null namespace renders "?".
    class ILSpy::Decompiler::TypeSystem::UnknownType nullNs(std::nullopt, "Foo", 0);
    EXPECT_EQ(nullNs.Name(), "Foo");
    EXPECT_EQ(nullNs.ReflectionName(), "?");
    EXPECT_FALSE(nullNs.FullTypeName().IsNested());

    // The structural equality compares the stored FullTypeName (both ctor forms).
    class ILSpy::Decompiler::TypeSystem::UnknownType a(FullTypeName("Test.Outer+Inner"));
    class ILSpy::Decompiler::TypeSystem::UnknownType b(FullTypeName("Test.Outer+Inner"));
    EXPECT_TRUE(a.Equals(b));
    class ILSpy::Decompiler::TypeSystem::UnknownType c(FullTypeName("Test.Outer+Other"));
    EXPECT_FALSE(a.Equals(c));
    // The two ctor forms disagree when the (ns, name) form cannot carry nesting.
    class ILSpy::Decompiler::TypeSystem::UnknownType d(std::optional<std::string>("Test"), "Outer", 0);
    EXPECT_FALSE(a.Equals(d));
}

} // namespace
