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
    // The explicit interface implementation renders without accessibility.
    EXPECT_NE(text.find("int ModifierFixture.IShape.Area()"), std::string::npos)
        << text;
    EXPECT_EQ(text.find("public int ModifierFixture.IShape.Area()"),
              std::string::npos)
        << text;
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
