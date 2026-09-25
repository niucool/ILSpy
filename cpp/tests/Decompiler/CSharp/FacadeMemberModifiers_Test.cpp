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
// fixture is absent or the type is not found.
bool RenderType(const char* fixturePath, const char* typeName,
                std::string& text) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(fixturePath, ec))
        return false;
    Metadata::MetadataFile file(fixturePath);
    if (!file.IsValid())
        return false;
    for (const auto& t : file.TypeDefs()) {
        if (std::string(t.Name) != typeName)
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

// A property whose accessor has a real body renders the body (the C#
// renders the decompiled accessor as its block): the auto-property stub
// form only applies when the property has its compiler-generated
// `<Name>k__BackingField` field.
TEST(FacadeMemberModifiersTest, AccessorBodyRenders)
{
    std::string text;
    if (!RenderType(kModifierFixture, "ModifierShapes", text))
        GTEST_SKIP() << "the modifier fixture is not provisioned";
    // The manual getter body renders its statements.
    std::size_t propPos = text.find("protected virtual int VirtualPropertyValue");
    ASSERT_NE(propPos, std::string::npos) << text;
    std::size_t getPos = text.find("get", propPos);
    ASSERT_NE(getPos, std::string::npos) << text;
    std::string accessorRegion = text.substr(propPos);
    EXPECT_NE(accessorRegion.find("return 5;"), std::string::npos)
        << "the getter body renders: " << accessorRegion;
    EXPECT_EQ(text.find("VirtualPropertyValue { get; }"), std::string::npos)
        << "a real body does not render the stub form: " << text;
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
