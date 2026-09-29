// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The facade's member-modifier render: the type-level body renders the
// declaration modifiers (accessibility, static, the readonly/const/volatile
// field bits, the virtual family, and the body-less abstract/extern
// declaration form) -- the C# GetMemberModifiers + ConvertField composition
// over the entities the module resolves.

#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

namespace CSharp = ::ILSpy::Decompiler::CSharp;
namespace Metadata = ::ILSpy::Decompiler::Metadata;

// The locally-compiled modifier fixture (the full modifier matrix; see
// ModifierFixture.cs in the fixture directory for the build recipe).
constexpr const char* kModifierFixture =
    "/home/jim/ilspy-test-fixtures/modifier_fixture/ModifierFixture.dll";

// Renders one named type through the static type-level entry. False when the
// fixture is absent or the type is not found. The name comparison uses the
// bare form (the metadata name's arity suffix stripped) so generic types
// are found by their source name.
bool RenderType(const char* fixturePath, const char* typeName,
                std::string& text) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(fixturePath, ec))
        return false;
    Metadata::MetadataFile file(fixturePath);
    if (!file.IsValid())
        return false;
    std::string requested = typeName;
    auto tick = requested.find('`');
    if (tick != std::string::npos)
        requested = requested.substr(0, tick);
    for (const auto& t : file.TypeDefs()) {
        std::string name = t.Name;
        tick = name.find('`');
        if (tick != std::string::npos)
            name = name.substr(0, tick);
        if (name != requested)
            continue;
        return CSharp::CSharpDecompiler::DecompileTypeToString(file, t.Token,
                                                                text);
    }
    return false;
}

} // namespace

// The connid corpus's Page1: the field and method declarations carry their
// accessibility (the oracle's `public Button _okButton;` /
// `internal Button _cancelButton;` / `private bool _contentLoaded;` /
// `public void InitializeComponent()`), and the explicit interface
// implementation carries none (the oracle's
// `void IComponentConnector.Connect(...)`).
TEST(FacadeMemberModifiersTest, ConnidFieldsAndMethodsCarryAccessibility)
{
    std::string path = ::ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    std::string text;
    for (const auto& t : file.TypeDefs()) {
        if (std::string(t.Name) != "Page1")
            continue;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(file,
                                                                    t.Token,
                                                                    text));
        break;
    }
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.find("public Button _okButton;"), std::string::npos)
        << text;
    EXPECT_NE(text.find("internal Button _cancelButton;"), std::string::npos)
        << text;
    EXPECT_NE(text.find("private bool _contentLoaded;"), std::string::npos)
        << text;
    EXPECT_NE(text.find("public void InitializeComponent()"), std::string::npos)
        << text;
    // The explicit interface implementation carries no accessibility.
    EXPECT_EQ(text.find("public void System.Windows.Markup"),
              std::string::npos)
        << text;
}

// The field modifier matrix over the modifier fixture: the visibility
// spellings, static, readonly, const (which subsumes static), and volatile.
TEST(FacadeMemberModifiersTest, FieldModifiersRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public static int StaticField;"), std::string::npos)
        << text;
    EXPECT_NE(text.find("public static readonly int StaticReadonlyField;"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("private const int ConstField = 7;"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("public const string ConstStringField = \"hello\";"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("public const bool ConstBoolField = true;"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("public const long ConstLongField = 42L;"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("internal int InternalField;"), std::string::npos)
        << text;
    EXPECT_NE(text.find("protected internal int ProtectedInternalField;"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("private protected int PrivateProtectedField;"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("protected volatile bool VolatileField;"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("public readonly int ReadonlyField;"),
              std::string::npos)
        << text;
}

// The method modifier matrix: static, virtual, and the constructor's
// accessibility. The derived class's overrides render override and the
// sealed override.
TEST(FacadeMemberModifiersTest, MethodModifiersRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public static int StaticMethod()"), std::string::npos)
        << text;
    EXPECT_NE(text.find("public virtual int VirtualMethod()"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("public ModifierShapes()"), std::string::npos)
        << text;
    // The explicit interface implementation renders without accessibility
    // and with the interface qualifier (the C#
    // GetExplicitInterfaceType over the first implemented member's
    // declaring type, the name after the last dot).
    EXPECT_NE(text.find("int IShape.Area()"), std::string::npos) << text;
    EXPECT_EQ(text.find("public int IShape.Area()"), std::string::npos)
        << "no accessibility on the explicit implementation: " << text;
    EXPECT_EQ(text.find("ModifierFixture.IShape.Area"), std::string::npos)
        << "the dotted metadata name does not render: " << text;
}

// The derived class: override and the sealed override.
TEST(FacadeMemberModifiersTest, OverrideAndSealedOverrideRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "Derived", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public override int VirtualMethod()"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("protected sealed override int ProtectedVirtualMethod()"),
              std::string::npos)
        << text;
}

// The abstract method renders as a body-less declaration (the C#
// DoDecompileMethod's abstract arm: the declaration with no body, no
// extern).
TEST(FacadeMemberModifiersTest, AbstractMethodRendersAsDeclaration)
{
    std::string text;
    if (!RenderType(kModifierFixture, "AbstractShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public abstract int AbstractMethod();"),
              std::string::npos)
        << text;
}

// The interface members render without an accessibility modifier (the C#
// NeedsAccessibility interface arm) and body-less interface methods render
// as declarations.
TEST(FacadeMemberModifiersTest, InterfaceMembersRenderWithoutAccessibility)
{
    std::string text;
    if (!RenderType(kModifierFixture, "IInterface", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("int InterfaceMethod();"), std::string::npos)
        << text;
    EXPECT_NE(text.find("int InterfaceProperty { get; }"), std::string::npos)
        << text;
    EXPECT_EQ(text.find("public int InterfaceMethod"), std::string::npos)
        << text;
}

// The property and event declarations carry the member modifiers (the
// accessor-visibility and accessor-body forms are the documented
// stand-in gap; the member-level modifiers are this slice's surface).
TEST(FacadeMemberModifiersTest, PropertyAndEventModifiersRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public int VirtualProperty"), std::string::npos)
        << text;
    EXPECT_NE(text.find("public virtual event"), std::string::npos) << text;
}

// The type declaration modifiers: the accessibility, static/abstract/
// sealed, and the kind adjustments (a struct/enum drops sealed, an
// interface drops abstract), derived from the type entity the same way
// the members' modifiers are.
TEST(FacadeMemberModifiersTest, TypeModifiersRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_TRUE(text.find("public class ModifierShapes") != std::string::npos)
        << text;
    EXPECT_TRUE(text.find("public partial class ModifierShapes") ==
                std::string::npos)
        << "a type with no registered partial half carries no partial: "
        << text;
    std::string derived;
    if (!RenderType(kModifierFixture, "Derived", derived))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(derived.find("public sealed class Derived"), std::string::npos)
        << derived;
    std::string abstractShapes;
    if (!RenderType(kModifierFixture, "AbstractShapes", abstractShapes))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(abstractShapes.find("public abstract class AbstractShapes"),
              std::string::npos)
        << abstractShapes;
    std::string iface;
    if (!RenderType(kModifierFixture, "IShape", iface))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(iface.find("public interface IShape"), std::string::npos)
        << iface;
    EXPECT_EQ(iface.find("public abstract interface IShape"),
              std::string::npos)
        << iface;
}

// The base-type list: the entity's direct base types minus the elided
// System.Object, the struct's System.ValueType, and the enum's
// System.Enum (replaced by the underlying type when not int).
TEST(FacadeMemberModifiersTest, BaseTypesRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("class ModifierShapes : IShape"), std::string::npos)
        << text;
    std::string derived;
    if (!RenderType(kModifierFixture, "Derived", derived))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(derived.find("class Derived : ModifierShapes"),
              std::string::npos)
        << derived;
    // The connid corpus: Page1's Application base and the
    // IComponentConnector interface.
    std::string path = ::ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    std::string page1;
    for (const auto& t : file.TypeDefs()) {
        if (std::string(t.Name) != "Page1")
            continue;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            file, t.Token, page1));
        break;
    }
    ASSERT_FALSE(page1.empty());
    EXPECT_NE(page1.find("class Page1 : Application, IComponentConnector"),
              std::string::npos)
        << page1;
}

// The base-list nameability filter (the C# f41b12c01 fix for #3230): an
// interface the base list cannot name drops from the rendered base list
// (a private nested interface of an unrelated type -- the shape the
// transitive interface-impl propagation produces); the public control
// interface stays.
TEST(FacadeMemberModifiersTest, UnnameableBaseListInterfaceDrops)
{
    std::string text;
    if (!RenderType("/home/jim/ilspy-test-fixtures/baselist_fixture/"
                    "BaseListSynth.dll",
                    "Unrelated", text))
        GTEST_SKIP() << "the base list fixture is not provisioned";
    EXPECT_NE(text.find("class Unrelated : IReal"), std::string::npos)
        << text;
    EXPECT_EQ(text.find("IHidden"), std::string::npos)
        << "the unnameable interface drops: " << text;
}

// The enum keyword: the type-header switch's enum arm (the earlier
// stand-in rendered `struct`), with the underlying type in the base list
// when it is not int.
TEST(FacadeMemberModifiersTest, EnumKeywordAndUnderlyingTypeRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "Color", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public enum Color"), std::string::npos) << text;
    EXPECT_EQ(text.find("struct Color"), std::string::npos) << text;
    std::string cs;
    if (!RenderType(kModifierFixture, "CS", cs))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(cs.find("internal enum CS : uint"), std::string::npos) << cs;
}

// The enum members render as the C# enum member list (the bare names for
// the consecutive-from-zero display mode; the first-only mode for a
// non-zero start; the special value__ field never renders).
TEST(FacadeMemberModifiersTest, EnumMembersRenderAsNames)
{
    std::string text;
    if (!RenderType(kModifierFixture, "Color", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("Red,"), std::string::npos) << text;
    EXPECT_NE(text.find("Blue"), std::string::npos) << text;
    EXPECT_EQ(text.find("value__"), std::string::npos) << text;
    EXPECT_EQ(text.find("const Color Red"), std::string::npos) << text;
    std::string cs;
    if (!RenderType(kModifierFixture, "CS", cs))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(cs.find("A = 1"), std::string::npos) << cs;
    EXPECT_EQ(cs.find("value__"), std::string::npos) << cs;
}

// The builtin type keywords in base lists: a generic interface
// instantiated over a builtin renders the keyword form (`IConsumer<uint>`
// from the entity-resolved SimpleType, whose reflection name is
// System.UInt32).
TEST(FacadeMemberModifiersTest, BuiltinKeywordsInBaseListsRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "UIntConsumer", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("class UIntConsumer : IConsumer<uint>"),
              std::string::npos)
        << text;
    EXPECT_EQ(text.find("UInt32"), std::string::npos) << text;
}

// The readonly struct modifier (the C# ConvertTypeDefinition struct arm).
TEST(FacadeMemberModifiersTest, ReadonlyStructRenders)
{
    std::string text;
    if (!RenderType("/home/jim/ilspy-test-fixtures/refreadonly_fixture/"
                    "RefReadOnlyFixture.dll",
                    "Point", text))
        GTEST_SKIP() << "the refreadonly fixture is not provisioned";
    EXPECT_NE(text.find("public readonly struct Point"), std::string::npos)
        << text;
}

// The member attributes render as the declaration's leading sections
// (the C# ConvertAttributes: one section per attribute, the fixed and
// named arguments as their constant literals).
TEST(FacadeMemberModifiersTest, MemberAttributesRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("[Obsolete(\"Use NewMethod instead.\")]"),
              std::string::npos)
        << text;
    // The attribute precedes its member declaration.
    std::size_t attrPos =
        text.find("[Obsolete(\"Use NewMethod instead.\")]");
    std::size_t methodPos = text.find("public static int StaticMethod()");
    ASSERT_NE(attrPos, std::string::npos);
    ASSERT_NE(methodPos, std::string::npos);
    EXPECT_LT(attrPos, methodPos) << text;
}

// The connid corpus's type and members carry the XamlGen GeneratedCode
// attribute (the oracle renders it on the type header and each member).
TEST(FacadeMemberModifiersTest, ConnidMemberAttributesRender)
{
    std::string path = ::ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    std::string text;
    for (const auto& t : file.TypeDefs()) {
        if (std::string(t.Name) != "Page1")
            continue;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            file, t.Token, text));
        break;
    }
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.find("[GeneratedCode(\"XamlGen\", \"4.8.0.0\")]"),
              std::string::npos)
        << text;
    // The attribute precedes the member declaration it decorates.
    std::size_t attrPos =
        text.find("[GeneratedCode(\"XamlGen\", \"4.8.0.0\")]");
    std::size_t methodPos = text.find("public void InitializeComponent()");
    ASSERT_NE(methodPos, std::string::npos);
    EXPECT_LT(attrPos, methodPos) << text;
}

// The accessor visibility renders when it differs from the property's
// (the C# ConvertAccessor: `protected set` on a public auto-property).
TEST(FacadeMemberModifiersTest, AccessorVisibilityRenders)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public int VirtualProperty { get; protected set; }"),
              std::string::npos)
        << text;
}

// A property whose accessor has a real multi-statement body renders the
// accessor block (the C# renders the decompiled accessor as its block):
// the auto-property stub form only applies when the property has its
// compiler-generated `<Name>k__BackingField` field, and the
// single-return getter takes the expression-bodied form.
TEST(FacadeMemberModifiersTest, AccessorBodyRenders)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    // The conditional getter's multi-statement body renders as the
    // accessor block.
    std::size_t propPos = text.find("public int ConditionalProperty");
    ASSERT_NE(propPos, std::string::npos) << text;
    std::string accessorRegion = text.substr(propPos);
    EXPECT_NE(accessorRegion.find("get"), std::string::npos)
        << "the getter renders: " << accessorRegion;
    EXPECT_NE(accessorRegion.find("if ("), std::string::npos)
        << "the getter body's statements render: " << accessorRegion;
    EXPECT_NE(accessorRegion.find("> 0)"), std::string::npos)
        << "the condition renders: " << accessorRegion;
    EXPECT_EQ(text.find("ConditionalProperty { get; }"), std::string::npos)
        << "a real body does not render the stub form: " << text;
}

// The getter-only property with a single-return body renders the C#
// expression-bodied form (NormalizeBlockStatements's
// SimplifyPropertyDeclaration under
// UseExpressionBodyForCalculatedGetterOnlyProperties).
TEST(FacadeMemberModifiersTest, CalculatedGetterOnlyPropertyRendersArrowBody)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("protected virtual int VirtualPropertyValue => 5;"),
              std::string::npos)
        << text;
}

// The interface property (no backing field, body-less accessors) keeps the
// stub form.
TEST(FacadeMemberModifiersTest, InterfacePropertyKeepsTheStubForm)
{
    std::string text;
    if (!RenderType(kModifierFixture, "IInterface", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("int InterfaceProperty { get; }"), std::string::npos)
        << text;
}

// The nested types render inside their declaring type's braces (the C#
// DoDecompile's member order: the NestedTypes lead the member list), and
// the whole-module render carries them once (not duplicated as top-level
// types in the root group).
TEST(FacadeMemberModifiersTest, NestedTypesRenderInsideTheirDeclaringType)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public abstract class AbstractShapes"),
              std::string::npos)
        << "the nested type renders inside: " << text.substr(0, 300);
    EXPECT_NE(text.find("public sealed class Derived : ModifierShapes"),
              std::string::npos)
        << "the nested type renders inside: " << text.substr(0, 300);
    // The whole-module render: the nested types appear exactly once each
    // (inside their parent), and the namespace grouping is unbroken.
    ::ILSpy::Decompiler::Metadata::MetadataFile file(kModifierFixture);
    ASSERT_TRUE(file.IsValid());
    ::ILSpy::Decompiler::DecompilerSettings settings;
    CSharp::CSharpDecompiler decompiler(file, settings);
    std::string whole = decompiler.DecompileWholeModuleToString();
    EXPECT_EQ(std::count(whole.begin(), whole.end(), 'D') >= 0, true);
    int derivedCount = 0;
    for (std::size_t pos = whole.find("class Derived");
         pos != std::string::npos;
         pos = whole.find("class Derived", pos + 1))
        derivedCount++;
    EXPECT_EQ(derivedCount, 1)
        << "the nested type renders once: " << whole.substr(0, 200);
}

// The event add/remove calls render as the C# compound assignment (the
// ReplaceMethodCallsWithOperators event arm): `recv.add_Click(handler)`
// -> `recv.Click += handler`, the delegate construction folding to the
// bare method group in the handler position.
TEST(FacadeMemberModifiersTest, EventAddRemoveCallsRenderAsCompoundAssignment)
{
    std::string path = ::ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    std::string text;
    for (const auto& t : file.TypeDefs()) {
        if (std::string(t.Name) != "Page1")
            continue;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            file, t.Token, text, /*wrapNamespace=*/false));
        break;
    }
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.find(".Click += OKButton_Click;"), std::string::npos)
        << text;
    EXPECT_EQ(text.find("add_Click"), std::string::npos)
        << "the add_ call form is gone: " << text;
    // A delegate construction in a non-event argument position renders
    // the method group inside the new-expression (the target argument
    // folds away).
    EXPECT_NE(text.find("new RoutedEventHandler(AttachedHandler_Click)"),
              std::string::npos)
        << text;
    EXPECT_EQ(text.find("this, AttachedHandler_Click"),
              std::string::npos)
        << "the delegate construction's target argument folds: " << text;
}

// The new-expression's type renders its short name (the C# name lookup
// through the using directives; the oracle's `new RoutedEventHandler(...)`
// over the full `new System.Windows.RoutedEventHandler(...)`).
TEST(FacadeMemberModifiersTest, NewExpressionTypesRenderShortNames)
{
    std::string path = ::ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    std::string text;
    for (const auto& t : file.TypeDefs()) {
        if (std::string(t.Name) != "Page1")
            continue;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            file, t.Token, text, /*wrapNamespace=*/false));
        break;
    }
    ASSERT_FALSE(text.empty());
    // The event-handler position folds to the bare method group (the +=
    // rewrite), so the short type name asserts over the remaining
    // delegate constructions (the AddHandler argument position).
    EXPECT_NE(text.find("new RoutedEventHandler(AttachedHandler_Click)"),
              std::string::npos)
        << text;
    EXPECT_EQ(text.find("new System.Windows.RoutedEventHandler"),
              std::string::npos)
        << "the new-expression type renders its short name: " << text;
}

// The body's static member accesses render unqualified within the
// declaring type (the C# name lookup's unqualified resolution: the
// oracle's .cctor renders `StaticReadonlyField = 42;`).
TEST(FacadeMemberModifiersTest, StaticMemberAccessesRenderUnqualifiedInsideTheType)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    // The .cctor's static field store.
    EXPECT_NE(text.find("StaticReadonlyField = 42;"), std::string::npos)
        << text;
    EXPECT_EQ(text.find("ModifierFixture.ModifierShapes.StaticReadonlyField"),
              std::string::npos)
        << "the own-type static access renders unqualified: " << text;
    // The conditional getter's static field read.
    EXPECT_NE(text.find("if (StaticField > 0)"), std::string::npos) << text;
    // The static method's return reads the field unqualified.
    EXPECT_NE(text.find("return StaticField;"), std::string::npos) << text;
}

// The single-type render carries the required using directives (the C#
// -t flow's IntroduceUsingDeclarations over the collected namespaces):
// `using System;` before the namespace header, the type's own namespace
// excluded, nothing when the type references nothing outside it.
TEST(FacadeMemberModifiersTest, SingleTypeRenderCarriesUsingDirectives)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_EQ(text.find("using System;\n\nnamespace ModifierFixture;\n\n"
                        "public class ModifierShapes"),
              0u)
        << text.substr(0, 200);
    // A type with no outside references carries no using lines.
    std::string color;
    if (!RenderType(kModifierFixture, "Color", color))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_EQ(color.find("namespace ModifierFixture;\n\npublic enum Color"),
              0u)
        << color.substr(0, 200);
}

// The single-type render carries its namespace header (the file-scoped
// form the oracle renders for a single type: `namespace X;` before the
// declaration).
TEST(FacadeMemberModifiersTest, SingleTypeRenderCarriesTheNamespaceHeader)
{
    std::string text;
    if (!RenderType(kModifierFixture, "Color", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_EQ(text.find("namespace ModifierFixture;\n\npublic enum Color"),
              0u)
        << text.substr(0, 200);
}

// The base-list qualification (the C# TypeSystemAstBuilder's short-name
// decision): a base type's short name survives only when the resolver's
// LookupSimpleNameOrTypeName over the render's using scope returns a
// non-error type matching the intended base type. When the short name is
// ambiguous -- the fixture implements IEnumerable while the net48
// mscorlib carries a duplicate (internal)
// System.Runtime.InteropServices.ComTypes.IEnumerable that mscorlib's
// InternalsVisibleTo friend list makes visible to this assembly (built
// under the name PresentationFramework for exactly that trigger) -- the
// base list renders the qualified form, exactly like the C#.
TEST(FacadeMemberModifiersTest, BaseListQualifiesAmbiguousTypeNames)
{
    constexpr const char* kAmbiguityFixture =
        "/home/jim/ilspy-test-fixtures/ambiguous_fixture/"
        "PresentationFramework.dll";
    std::string text;
    if (!RenderType(kAmbiguityFixture, "AmbiguousBase", text))
        GTEST_SKIP() << "the ambiguity fixture is not provisioned";
    EXPECT_NE(text.find(
                  "class AmbiguousBase : System.Collections.IEnumerable"),
              std::string::npos)
        << text;
}

// The base-list nested-type spelling (the C# ConvertTypeHelper's
// MakeSimpleType/MemberType composition): a sibling nested type resolves
// by its own name (the enclosing type's members are in the lookup scope);
// a nested type of another declaring type renders the declaring type
// through the same decision joined by '.'; a top-level type implementing
// a nested type names it through the declaring type's (short) name. The
// '+' reflection spelling never appears in a base list.
TEST(FacadeMemberModifiersTest, BaseListRendersNestedTypesThroughTheNameDecision)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string sibling, far, top;
    if (!RenderType(kNestedFixture, "SiblingImpl", sibling))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    ASSERT_TRUE(RenderType(kNestedFixture, "FarImpl", far));
    ASSERT_TRUE(RenderType(kNestedFixture, "TopImpl", top));
    EXPECT_NE(sibling.find("class SiblingImpl : INested"), std::string::npos)
        << sibling;
    EXPECT_NE(far.find("class FarImpl : Other.IFar"), std::string::npos)
        << far;
    EXPECT_NE(top.find("class TopImpl : Holder.INested"), std::string::npos)
        << top;
}

// The generic base-list argument goes through the same name decision as
// the base type: a sibling nested argument resolves by its own name, a
// nested argument of another declaring type renders the dotted form, and
// a type-parameter argument renders its declared name (never the
// reflection ``N`` spelling).
TEST(FacadeMemberModifiersTest, BaseListTypeArgumentsFollowTheNameDecision)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string sibling, top, param;
    if (!RenderType(kNestedFixture, "SiblingArg", sibling))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    ASSERT_TRUE(RenderType(kNestedFixture, "TopArg", top));
    ASSERT_TRUE(RenderType(kNestedFixture, "ParamArg", param));
    EXPECT_NE(sibling.find("class SiblingArg : Gen<INested>"),
              std::string::npos)
        << sibling;
    EXPECT_NE(top.find("class TopArg : Gen<Holder.INested>"),
              std::string::npos)
        << top;
    EXPECT_NE(param.find("class ParamArg<TItem> : Gen<TItem>"),
              std::string::npos)
        << param;
}

// The type-parameter constraint clauses (the C#
// ConvertTypeParameterConstraint): a parameter carrying a special
// constraint (`class`/`struct`/`new()`), a type constraint beyond
// Object/ValueType, or a nullability constraint renders its `where`
// clause on the declaration line; the constraint types go through the same
// name decision as the base list.
TEST(FacadeMemberModifiersTest, TypeParameterConstraintsRender)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string ref, val, type, multi, newC;
    if (!RenderType(kNestedFixture, "RefConstrained", ref))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    ASSERT_TRUE(RenderType(kNestedFixture, "ValConstrained", val));
    ASSERT_TRUE(RenderType(kNestedFixture, "TypeConstrained", type));
    ASSERT_TRUE(RenderType(kNestedFixture, "MultiConstrained", multi));
    ASSERT_TRUE(RenderType(kNestedFixture, "NewConstrained", newC));
    EXPECT_NE(ref.find("class RefConstrained<T> where T : class"),
              std::string::npos)
        << ref;
    EXPECT_NE(val.find("class ValConstrained<T> where T : struct"),
              std::string::npos)
        << val;
    EXPECT_NE(type.find("class TypeConstrained<T> where T : Holder.INested"),
              std::string::npos)
        << type;
    EXPECT_NE(
        multi.find("class MultiConstrained<T> where T : class, "
                  "Holder.INested"),
        std::string::npos)
        << multi;
    EXPECT_NE(newC.find("class NewConstrained<T> where T : new()"),
              std::string::npos)
        << newC;
}

// The member-signature qualification: the C#
// FullyQualifyAmbiguousTypeNamesVisitor re-renders every SimpleType with a
// resolve-result annotation, so the return types, parameter types, field
// types, and property types go through the same using-scope name decision
// as the base list.
TEST(FacadeMemberModifiersTest, MemberSignaturesQualifyAmbiguousNames)
{
    constexpr const char* kAmbiguityFixture =
        "/home/jim/ilspy-test-fixtures/ambiguous_fixture/"
        "PresentationFramework.dll";
    std::string text;
    if (!RenderType(kAmbiguityFixture, "AmbiguousBase", text))
        GTEST_SKIP() << "the ambiguity fixture is not provisioned";
    EXPECT_NE(text.find(
                  "public System.Collections.IEnumerator GetEnumerator()"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find(
                  "Consume(System.Collections.IEnumerable items)"),
              std::string::npos)
        << text;
    EXPECT_NE(
        text.find("private System.Collections.IEnumerable _list;"),
        std::string::npos)
        << text;
    EXPECT_NE(text.find("public System.Collections.IEnumerable Items"),
              std::string::npos)
        << text;
}

// The .override directive synthesis (the C# AddInterfaceImplHelpers):
// a plain-named method bound to an interface contract through a
// MethodImpl row (the VB-style explicit implementation C# source
// cannot express) renders a synthesized explicit-interface-
// implementation forwarder after its own declaration -- the interface
// qualifier through the name decision, the generated comment, and the
// forwarding call.
TEST(FacadeMemberModifiersTest, OverrideDirectiveRendersTheForwarder)
{
    constexpr const char* kOverrideFixture =
        "/home/jim/ilspy-test-fixtures/override_fixture/"
        "OverrideSynth.dll";
    std::string text;
    if (!RenderType(kOverrideFixture, "OverrideShape", text))
        GTEST_SKIP() << "the override fixture is not provisioned";
    EXPECT_NE(text.find("int IShape.GetValue()"), std::string::npos)
        << text;
    EXPECT_NE(text.find("ILSpy generated this explicit interface "
                        "implementation from .override directive in Impl"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("return this.Impl();"), std::string::npos)
        << text;
}

// The .override forwarder gate, the forwarded-interface arm: when the
// MethodDeclaration's interface resolves through a TYPE FORWARDER (the
// netstandard-facade shape of dnlib.dll's compiler-generated
// enumerators -- the scoped module forwards the type to another
// assembly), the real tool renders NO forwarder: the definition lands in
// a module other than the one the declaration is scoped to.
TEST(FacadeMemberModifiersTest, OverrideDirectiveSkipsForwardedInterface)
{
    constexpr const char* kFixture =
        "/home/jim/ilspy-test-fixtures/cross_override_fixture/fwd/"
        "CrossFwd.dll";
    std::string text;
    if (!RenderType(kFixture, "OverrideShape", text))
        GTEST_SKIP() << "the cross-override fixture is not provisioned";
    EXPECT_EQ(text.find("ILSpy generated this explicit interface "
                        "implementation"),
              std::string::npos)
        << text;
    EXPECT_EQ(text.find("IShape.GetValue()"), std::string::npos) << text;
}

// The .override forwarder gate, the direct cross-assembly arm: when the
// interface is DEFINED in the referenced assembly the declaration is
// scoped to (no forwarding hop), the forwarder still renders -- the
// same shape as the same-module fixture.
TEST(FacadeMemberModifiersTest, OverrideDirectiveRendersCrossAssemblyForwarder)
{
    constexpr const char* kFixture =
        "/home/jim/ilspy-test-fixtures/cross_override_fixture/direct/"
        "CrossOverride.dll";
    std::string text;
    if (!RenderType(kFixture, "OverrideShape", text))
        GTEST_SKIP() << "the cross-override fixture is not provisioned";
    EXPECT_NE(text.find("int IShape.GetValue()"), std::string::npos)
        << text;
    EXPECT_NE(text.find("ILSpy generated this explicit interface "
                        "implementation from .override directive in Impl"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("return this.Impl();"), std::string::npos)
        << text;
}

// The C# DoDecompileType worklist (EnqueueReferencedMembers): a hidden
// compiler-generated type whose declaring type's rendered members still
// reference it -- the state-machine attribute's typeof -- renders at its
// nested position despite the hidden predicate (the hidden members that
// are "still needed").
TEST(FacadeMemberModifiersTest, HiddenTypesReferencedByAttributesStillRender)
{
    constexpr const char* kWorklistFixture =
        "/home/jim/ilspy-test-fixtures/worklist_fixture/"
        "WorklistShape.dll";
    std::string text;
    if (!RenderType(kWorklistFixture, "WorklistShape", text))
        GTEST_SKIP() << "the worklist fixture is not provisioned";
    EXPECT_NE(text.find("private sealed class MyStateMachine"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("public void MoveNext()"), std::string::npos)
        << text;
    EXPECT_NE(
        text.find("public void SetStateMachine(IAsyncStateMachine "
                  "stateMachine)"),
        std::string::npos)
        << text;
}

// The parameter modifiers (the C# ConvertParameter's ReferenceKind +
// IsParams arms): ref/out/in render as the type's leading keyword, the
// params array as the leading `params`.
TEST(FacadeMemberModifiersTest, ParameterModifiersRender)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string text;
    if (!RenderType(kNestedFixture, "RefOut", text))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    EXPECT_NE(text.find("public void Shape(ref int a, out string b, "
                        "in double c)"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("public void Values(params int[] numbers)"),
              std::string::npos)
        << text;
}

// The parameter default values (the C# ConvertParameter's
// IsDefaultValueAssignmentAllowed + ConvertConstantValue): an optional
// parameter with a signature constant renders `= value`; the optional
// parameters must be trailing (a later non-optional parameter suppresses
// the earlier defaults).
TEST(FacadeMemberModifiersTest, ParameterDefaultValuesRender)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string text;
    if (!RenderType(kNestedFixture, "RefOut", text))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    EXPECT_NE(
        text.find("public void Def(int x = 5, string s = null, "
                  "double d = 1.5)"),
        std::string::npos)
        << text;
}

// The generic type-parameter list on the declaration header (the C#
// TypeDeclaration's TypeParameters): the type's OWN parameters only --
// the declaring chain's outer parameters are not restated (a nested
// `Outer<T>.Inner<U>` declares <U>; the compiler-generated state
// machines re-declare the enclosing generic's parameters, so they
// render without any).
TEST(FacadeMemberModifiersTest, NestedTypeParameterListSkipsOuterParameters)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string text;
    if (!RenderType(kNestedFixture, "Gen", text))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    // The nested-in-generic shapes: the re-declaring nested type renders
    // only its OWN parameters (the chain-merged surface carries the
    // declaring type's first).
    std::string shadow, ownParam;
    ASSERT_TRUE(RenderType(kNestedFixture, "Shadow", shadow));
    EXPECT_NE(shadow.find("private sealed class Shadow<TOther1, TOther2>"),
              std::string::npos)
        << shadow;
    ASSERT_TRUE(RenderType(kNestedFixture, "OwnParam", ownParam));
    EXPECT_NE(ownParam.find("public sealed class OwnParam<TItem>"),
              std::string::npos)
        << ownParam;
    // The top-level generic renders its own list.
    EXPECT_NE(text.find("class Gen<T>"), std::string::npos) << text;
}

// The delegate shape (the C# ConvertTypeDefinition's Delegate arm over
// GetDelegateInvokeMethod): the declaration renders the Invoke
// signature -- `delegate ReturnType Name(params);` -- never the class
// shape or the .ctor/Invoke/BeginInvoke/EndInvoke members, with the
// sealed modifier stripped and the type-parameter list + constraints
// carried over.
TEST(FacadeMemberModifiersTest, DelegateTypesRenderTheInvokeSignature)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string text;
    if (!RenderType(kNestedFixture, "CollectionCallback", text))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    EXPECT_NE(text.find("public delegate void CollectionCallback(int x, "
                        "string s);"),
              std::string::npos)
        << text;
    std::string generic;
    ASSERT_TRUE(RenderType(kNestedFixture, "Transformer", generic));
    EXPECT_NE(generic.find("public delegate T Transformer<T>(T input);"),
              std::string::npos)
        << generic;
}

// The indexer property (the C# ConvertProperty over IsIndexer): the
// declaration names `this[<index parameters>]`, never the metadata
// name (Item); the index parameters render through the same parameter
// builder as the methods.
TEST(FacadeMemberModifiersTest, IndexerPropertiesRenderTheThisForm)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string text;
    if (!RenderType(kNestedFixture, "Indexer", text))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    EXPECT_NE(text.find("public string this[int index]"),
              std::string::npos)
        << text;
}

// The XML documentation comments (the C# AddXmlDocumentationTransform
// over the type declarations): the adjacent .xml file's member content
// renders as the leading /// lines -- the first non-empty line's
// indentation stripped, the trailing empty lines dropped.
TEST(FacadeMemberModifiersTest, XmlDocumentationCommentsRenderOnTypeDeclarations)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string text;
    if (!RenderType(kNestedFixture, "RefOut", text))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    EXPECT_NE(text.find("/// <summary>\n/// The parameter modifier shapes: "
                        "ref/out/in and the params array.\n/// </summary>"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("/// <remarks>\n/// The member-name scanner reads "
                        "the adjacent XML file.\n/// </remarks>"),
              std::string::npos)
        << text;
    std::string generic;
    ASSERT_TRUE(RenderType(kNestedFixture, "Gen", generic));
    EXPECT_NE(generic.find("/// <summary>The generic container for the "
                           "typeof shapes.</summary>"),
              std::string::npos)
        << generic;
}

// The member documentation IDs (the C# IdStringProvider's M:/P:/F:
// forms): the declaring type's dotted doc name, the escaped member
// name (the explicit-implementation dots -> #), the method generic
// count, and the parameter list in the doc type-name spelling (the
// primitives' full names, the byref @, the array [], the generic
// instantiation's brace-distributed arguments).
TEST(FacadeMemberModifiersTest, XmlDocumentationCommentsRenderOnMembers)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string text;
    if (!RenderType(kNestedFixture, "RefOut", text))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    EXPECT_NE(
        text.find("/// <summary>The ref/out/in parameter shapes.</summary>\n"
                 "public void Shape(ref int a, out string b, in double c)"),
        std::string::npos)
        << text;
    EXPECT_NE(text.find("/// <summary>The params array shape.</summary>\n"
                        "public void Values(params int[] numbers)"),
              std::string::npos)
        << text;
    EXPECT_NE(
        text.find("/// <summary>The default values shape.</summary>\n"
                 "public void Def(int x = 5, string s = null, double d = 1.5)"),
        std::string::npos)
        << text;
    std::string indexer;
    ASSERT_TRUE(RenderType(kNestedFixture, "Indexer", indexer));
    EXPECT_NE(indexer.find("/// <summary>The indexer shape.</summary>\n"
                           "public string this[int index]"),
              std::string::npos)
        << indexer;
    std::string holder;
    ASSERT_TRUE(RenderType(kNestedFixture, "Holder", holder));
    EXPECT_NE(holder.find("/// <summary>A field doc.</summary>\n"
                          "public string Field;"),
              std::string::npos)
        << holder;
}

// The explicit-implementation event (the C# DoDecompileMember's event
// arm over IsExplicitImplementation): the interface-qualified name
// through the name decision (the metadata's dotted name's last segment
// + the implemented interface), and the add/remove accessor blocks
// (never the field-like `;` form).
TEST(FacadeMemberModifiersTest, ExplicitImplementationEventsRenderTheAccessorBlocks)
{
    constexpr const char* kNestedFixture =
        "/home/jim/ilspy-test-fixtures/nested_fixture/NestedBase.dll";
    std::string text;
    if (!RenderType(kNestedFixture, "EventImpl", text))
        GTEST_SKIP() << "the nested-base fixture is not provisioned";
    EXPECT_NE(text.find("event EventHandler IShape.Shape"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("add"), std::string::npos) << text;
    EXPECT_NE(text.find("remove"), std::string::npos) << text;
    // The field-like form never appears for the explicit implementation.
    EXPECT_EQ(text.find("IShape.Shape;"), std::string::npos) << text;
}

// The whole-module render's using header carries the module-wide
// required set (the C# IntroduceUsingDeclarations over the whole-module
// tree), not just the assembly-attribute namespaces: the connid module's
// header includes the WPF namespaces the type bodies reference, and the
// module's own namespace never appears.
TEST(FacadeMemberModifiersTest, WholeModuleUsingHeaderCarriesTheModuleWideSet)
{
    std::string path = ::ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    std::string whole =
        CSharp::CSharpDecompiler::DecompileWholeModuleToString(file);
    EXPECT_NE(whole.find("using System.Windows;\n"), std::string::npos)
        << whole.substr(0, 300);
    EXPECT_NE(whole.find("using System.CodeDom.Compiler;\n"),
              std::string::npos)
        << whole.substr(0, 300);
    EXPECT_EQ(whole.find("using MyApp;"), std::string::npos)
        << "the module's own namespace never appears: " << whole.substr(0, 300);
    // The header precedes the attribute sections.
    EXPECT_LT(whole.find("using System.Windows;"),
              whole.find("[assembly:"));
}

// The whole-module render groups the types by namespace (the C#
// DoDecompileTypes' NamespaceDeclaration emission): consecutive
// same-namespace types nest under one `namespace X { }` block; types
// with no namespace render at the root.
TEST(FacadeMemberModifiersTest, WholeModuleGroupsTypesByNamespace)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(kModifierFixture, ec))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    Metadata::MetadataFile file(kModifierFixture);
    ASSERT_TRUE(file.IsValid());
    ::ILSpy::Decompiler::DecompilerSettings settings;
    CSharp::CSharpDecompiler decompiler(file, settings);
    std::string whole = decompiler.DecompileWholeModuleToString();
    // The fixture's single namespace renders file-scoped (the C#
    // NormalizeBlockStatements rule): `namespace X;` once, no braces.
    std::size_t nsPos = whole.find("namespace ModifierFixture;");
    EXPECT_NE(nsPos, std::string::npos) << whole.substr(0, 400);
    // The type headers follow the file-scoped namespace declaration.
    std::size_t typePos = whole.find("public class ModifierShapes");
    ASSERT_NE(typePos, std::string::npos);
    EXPECT_LT(nsPos, typePos) << whole.substr(0, 400);
    // One declaration, not one per type.
    EXPECT_EQ(whole.find("namespace ModifierFixture;", nsPos + 10),
              std::string::npos)
        << "the consecutive same-namespace types share one declaration";
}

// The constructor's implicit no-argument base call does not render (the
// C# constructor-initializer convention: a base ctor call renders only
// with arguments; the no-arg form is the implicit default).
TEST(FacadeMemberModifiersTest, ImplicitBaseConstructorCallDoesNotRender)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public ModifierShapes()"), std::string::npos)
        << "the constructor renders: " << text;
    EXPECT_EQ(text.find("base();"), std::string::npos)
        << "the implicit no-arg base call is elided: " << text;
}

// The static constructor (the .cctor the static readonly field's
// initializer produces) renders with the static modifier and no
// accessibility (the C# NeedsAccessibility static-ctor arm), while the
// instance constructor carries its own accessibility.
TEST(FacadeMemberModifiersTest, StaticConstructorRendersWithoutAccessibility)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    EXPECT_NE(text.find("public ModifierShapes()"), std::string::npos)
        << "the instance constructor carries its accessibility";
    EXPECT_NE(text.find("static ModifierShapes()"), std::string::npos)
        << "the static constructor renders" << text;
    EXPECT_EQ(text.find("public static ModifierShapes()"),
              std::string::npos)
        << "the static constructor carries no accessibility" << text;
}
