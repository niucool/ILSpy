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

// Tests for the SyntheticWpfModule port (ICSharpCode.BamlDecompiler/SyntheticWpfModule.cs)
// -- the artificial IModule stand-in for an unresolvable well-known BAML assembly. Every
// expectation is gold-pinned against the REAL internal SyntheticWpfModule from the
// installed ICSharpCode.BamlDecompiler.dll, driven through the C:/temp-probe/SwmProbe
// reflection probe (the internal class's public factory CreateReference, the public
// IModuleReference/IModule/INamespace/ITypeDefinition interfaces, and RegisterType via
// reflection over a real SimpleCompilation(MinimalCorlib) -- the probe's M1..M14 fact
// series). The FindType(KnownTypeCode.String) fixture compilation is the shared
// TestSupport::LookupCompilation with a registered System.String type definition (the
// MinimalCorlib stand-in the probe used); the IsMainModule-true arm uses the port's real
// SimpleCompilation over the synthetic reference (the probe's `new SimpleCompilation(
// reference)` shape).

#include "BamlDecompiler/SyntheticWpfModule.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Baml = ILSpy::BamlDecompiler;
// LookupStubs.hpp declares its stubs inside ILSpy::Decompiler::TypeSystem::TestSupport;
// the using-directive makes the `TestSupport::` qualifier resolvable (the DefaultAttribute
// / other LookupStubs-consumer convention).
using namespace ILSpy::Decompiler::TypeSystem;

const char* kPresentationXmlns =
    "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
const char* kFrameworkFullAssemblyName =
    "PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35";

// The C# probe resolved the reference over `new SimpleCompilation(MinimalCorlib.Instance)`
// (MinimalCorlib supplies FindType(KnownTypeCode.String)); the port has no MinimalCorlib,
// so the fixture compilation is the shared LookupCompilation with a registered
// System.String type definition (the same FindType result the gold pinned:
// ReflectionName "System.String").
class FixtureCompilation {
public:
    FixtureCompilation()
    {
        stringType_ = std::make_shared<TestSupport::LookupTypeDefinition>(
            "String", "System",
            TS::FullTypeName(TS::TopLevelTypeName("System", "String")),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation_, nullptr,
            TS::KnownTypeCode::String);
        compilation_.RegisterKnownType(TS::KnownTypeCode::String, stringType_.get());
    }

    TS::ICompilation& Compilation() { return compilation_; }
    const TS::IType* StringType() const { return stringType_.get(); }

private:
    TestSupport::LookupCompilation compilation_;
    std::shared_ptr<TestSupport::LookupTypeDefinition> stringType_;
};

// A resolved synthetic module: the framework reference over a fresh fixture compilation
// (each test gets its own registration state).
class ResolvedModule {
public:
    explicit ResolvedModule(std::optional<std::string> presentationXmlns
                                = std::optional<std::string>(kPresentationXmlns))
    {
        reference_ = Baml::SyntheticWpfModule::CreateReference(
            std::make_shared<ILSpy::Decompiler::Metadata::AssemblyNameReference>(
                ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                    kFrameworkFullAssemblyName)),
            std::move(presentationXmlns));
        TS::SimpleTypeResolveContext context(compilation_.Compilation());
        module_ = reference_->Resolve(context);
        // RegisterType is a SyntheticWpfModule member, NOT an IModule interface member
        // (the C# public method on the internal class, reachable only through the concrete
        // type); the Resolve return is the const interface pointer, so the concrete handle
        // is recovered once here (the single-inheritance downcast is exact).
        synthetic_ = const_cast<Baml::SyntheticWpfModule*>(
            static_cast<const Baml::SyntheticWpfModule*>(module_));
    }

    const TS::IModule* Module() const { return module_; }
    Baml::SyntheticWpfModule* Synthetic() const { return synthetic_; }
    TS::ICompilation& Compilation() { return compilation_.Compilation(); }
    const TS::IType* StringType() const { return compilation_.StringType(); }

    // Registers the probe's M3 trio: three namespaces (System.Windows.Controls /
    // System.Windows / the empty root namespace), the state the M6 attribute facts pin.
    void SeedThreeNamespaces()
    {
        RegisterType("System.Windows.Controls", "Button");
        RegisterType("System.Windows", "Window");
        RegisterType("", "RootType");
    }

    const TS::ITypeDefinition* RegisterType(const std::string& ns, const std::string& name)
    {
        return synthetic_->RegisterType(ns, name);
    }

private:
    FixtureCompilation compilation_;
    std::unique_ptr<TS::IModuleReference> reference_;
    const TS::IModule* module_ = nullptr;
    Baml::SyntheticWpfModule* synthetic_ = nullptr;
};

// A TypeVisitor recording which Visit* the dispatch reached (the probe's
// RecordingVisitor); the returned handle is the visited type itself.
class RecordingVisitor : public TS::TypeVisitor {
public:
    TS::ITypePtr VisitTypeDefinition(TS::ITypeDefinition& type) override
    {
        visited = "VisitTypeDefinition:" + type.ReflectionName();
        return type.shared_from_this();
    }
    TS::ITypePtr VisitOtherType(TS::IType& type) override
    {
        visited = "VisitOtherType:" + type.ReflectionName();
        return type.shared_from_this();
    }

    std::string visited;
};

std::string Join(const std::vector<const TS::ITypeDefinition*>& types)
{
    std::string joined;
    for (const auto* type : types) {
        if (!joined.empty())
            joined += '|';
        joined += type->ReflectionName();
    }
    return joined;
}

} // namespace

// ---------------------------------------------------------------------------
// M1: the module surface after CreateReference + Resolve -- the Module symbol kind, the
// assembly identity routed through the reference's IAssemblyReference, the null
// MetadataFile, IsMainModule false over a compilation whose main module is another
// module, and the empty type tables.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, ModuleSurfaceAfterResolve)
{
    ResolvedModule m;

    EXPECT_EQ(m.Module()->SymbolKind(), TS::SymbolKind::Module);
    EXPECT_EQ(m.Module()->Name(), "PresentationFramework");
    EXPECT_EQ(m.Module()->AssemblyName(), "PresentationFramework");
    EXPECT_EQ(m.Module()->FullAssemblyName(), kFrameworkFullAssemblyName);
    EXPECT_EQ(m.Module()->AssemblyVersion(), TS::Version(4, 0, 0, 0));
    EXPECT_EQ(m.Module()->MetadataFile(), nullptr);
    EXPECT_FALSE(m.Module()->IsMainModule());
    EXPECT_EQ(&m.Module()->Compilation(), &m.Compilation());
    EXPECT_TRUE(m.Module()->TopLevelTypeDefinitions().empty());
    EXPECT_TRUE(m.Module()->TypeDefinitions().empty());
    EXPECT_EQ(m.Module()->GetTypeDefinition(TS::TopLevelTypeName("System.Windows", "Button")),
              nullptr);
}

// ---------------------------------------------------------------------------
// M2: Resolve creates a FRESH module per call (no caching), each starting empty.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, ResolveCreatesFreshInstances)
{
    ResolvedModule m;
    TS::SimpleTypeResolveContext context(m.Compilation());
    auto reference = Baml::SyntheticWpfModule::CreateReference(
        std::make_shared<ILSpy::Decompiler::Metadata::AssemblyNameReference>(
            ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                kFrameworkFullAssemblyName)),
        std::optional<std::string>(kPresentationXmlns));

    const TS::IModule* second = reference->Resolve(context);
    const TS::IModule* third = reference->Resolve(context);

    EXPECT_NE(second, third);
    EXPECT_NE(second, m.Module());
    EXPECT_TRUE(second->TypeDefinitions().empty());
    EXPECT_TRUE(third->TypeDefinitions().empty());
    EXPECT_EQ(second->AssemblyName(), "PresentationFramework");
    EXPECT_EQ(&second->Compilation(), &m.Compilation());
}

// ---------------------------------------------------------------------------
// M3: RegisterType caches per (namespace, name), GetTypeDefinition finds the registered
// type, the tables enumerate in registration order, and a plain lookup never creates
// types.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, RegisterTypeCachesAndLooksUp)
{
    ResolvedModule m;

    const TS::ITypeDefinition* button = m.RegisterType("System.Windows.Controls", "Button");
    const TS::ITypeDefinition* buttonAgain = m.RegisterType("System.Windows.Controls", "Button");
    EXPECT_EQ(button, buttonAgain);
    EXPECT_EQ(m.Module()->GetTypeDefinition(
                  TS::TopLevelTypeName("System.Windows.Controls", "Button")), button);

    m.RegisterType("System.Windows", "Window");
    m.RegisterType("", "RootType");
    EXPECT_EQ(Join(m.Module()->TopLevelTypeDefinitions()),
              "System.Windows.Controls.Button|System.Windows.Window|RootType");
    EXPECT_EQ(Join(m.Module()->TypeDefinitions()),
              "System.Windows.Controls.Button|System.Windows.Window|RootType");
    EXPECT_EQ(m.Module()->GetTypeDefinition(TS::TopLevelTypeName("System.Windows", "Unregistered")),
              nullptr);
}

// ---------------------------------------------------------------------------
// M4: the registered type's ParentModule / Compilation back-pointers.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, RegisteredTypeBackPointers)
{
    ResolvedModule m;
    const TS::ITypeDefinition* button = m.RegisterType("System.Windows.Controls", "Button");

    EXPECT_EQ(button->ParentModule(), m.Module());
    EXPECT_EQ(&button->Compilation(), &m.Compilation());
}

// ---------------------------------------------------------------------------
// M5: the root namespace surface -- the Namespace symbol kind, the empty name/full
// name/extern alias, the null parent, the empty child namespaces, the Types enumeration
// filtered by namespace (only the empty-namespace RootType), the contributing module,
// the always-null GetChildNamespace, and the (name, arity) lookups.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, RootNamespaceSurface)
{
    ResolvedModule m;
    m.SeedThreeNamespaces();
    const TS::ITypeDefinition* rootType = m.Module()->GetTypeDefinition(TS::TopLevelTypeName("", "RootType"));

    const TS::INamespace& root = m.Module()->RootNamespace();
    EXPECT_EQ(root.SymbolKind(), TS::SymbolKind::Namespace);
    EXPECT_EQ(root.Name(), "");
    EXPECT_EQ(root.FullName(), "");
    EXPECT_EQ(root.ExternAlias(), "");
    EXPECT_EQ(root.ParentNamespace(), nullptr);
    EXPECT_TRUE(root.ChildNamespaces().empty());
    ASSERT_EQ(root.Types().size(), 1u);
    EXPECT_EQ(root.Types()[0], rootType);
    ASSERT_EQ(root.ContributingModules().size(), 1u);
    EXPECT_EQ(root.ContributingModules()[0], m.Module());
    EXPECT_EQ(root.GetChildNamespace("System"), nullptr);
    EXPECT_EQ(root.GetTypeDefinition("RootType", 0), rootType);
    EXPECT_EQ(root.GetTypeDefinition("RootType", 1), nullptr);
    EXPECT_EQ(root.GetTypeDefinition("Missing", 0), nullptr);
    EXPECT_EQ(&root.Compilation(), &m.Compilation());
}

// ---------------------------------------------------------------------------
// M6a/M6b: the XmlnsDefinitionAttribute reconstruction -- one DefaultAttribute per
// distinct seeded CLR namespace in first-occurrence (registration) order, each carrying
// the (presentation xmlns, namespace) fixed arguments typed as the compilation's String,
// no named arguments, no constructor, no decode errors.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, AssemblyAttributesXmlnsReconstruction)
{
    ResolvedModule m;
    m.SeedThreeNamespaces();

    auto attributes = m.Module()->GetAssemblyAttributes();
    ASSERT_EQ(attributes.size(), 3u);
    const std::string expectedNamespaces[] = {
        "System.Windows.Controls", "System.Windows", ""
    };
    for (std::size_t i = 0; i < attributes.size(); i++) {
        const TS::IAttribute* attribute = attributes[i];
        EXPECT_EQ(attribute->AttributeType().ReflectionName(),
                  "System.Windows.Markup.XmlnsDefinitionAttribute");
        EXPECT_FALSE(attribute->HasDecodeErrors());
        EXPECT_EQ(attribute->Constructor(), nullptr);
        EXPECT_TRUE(attribute->NamedArguments().empty());
        ASSERT_EQ(attribute->FixedArguments().size(), 2u);
        EXPECT_EQ(attribute->FixedArguments()[0].Type()->ReflectionName(), "System.String");
        EXPECT_EQ(std::any_cast<std::string>(attribute->FixedArguments()[0].Value()),
                  kPresentationXmlns);
        EXPECT_EQ(attribute->FixedArguments()[1].Type()->ReflectionName(), "System.String");
        EXPECT_EQ(std::any_cast<std::string>(attribute->FixedArguments()[1].Value()),
                  expectedNamespaces[i]);
    }
}

// ---------------------------------------------------------------------------
// M6c: the attribute set is CACHED -- repeated reads return the SAME attribute instances
// until a registration invalidates the cache.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, AssemblyAttributesCacheStability)
{
    ResolvedModule m;
    m.SeedThreeNamespaces();

    auto first = m.Module()->GetAssemblyAttributes();
    auto second = m.Module()->GetAssemblyAttributes();
    ASSERT_EQ(first.size(), second.size());
    ASSERT_EQ(first.size(), 3u);
    EXPECT_EQ(second[0], first[0]);
    EXPECT_EQ(second[1], first[1]);
    EXPECT_EQ(second[2], first[2]);
}

// ---------------------------------------------------------------------------
// M6d: registering a type in a NEW namespace invalidates the cache -- the next read
// returns a rebuilt set with the new namespace appended.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, AssemblyAttributesInvalidatedByNewNamespace)
{
    ResolvedModule m;
    m.SeedThreeNamespaces();

    auto before = m.Module()->GetAssemblyAttributes();
    ASSERT_EQ(before.size(), 3u);
    // Capture the first seeded namespace while `before` is still valid: invalidating the
    // cache frees the old cached instances, so the `before` pointers must not be
    // dereferenced after `RegisterType`.
    std::string firstNamespace =
        std::any_cast<std::string>(before[0]->FixedArguments()[1].Value());

    m.RegisterType("System.Windows.Input", "Cursor");
    auto after = m.Module()->GetAssemblyAttributes();
    ASSERT_EQ(after.size(), 4u);
    // The rebuilt set repeats the seeded namespaces in registration order and appends the
    // new one. Comparing the rebuilt pointers against the (dangling) `before` pointers is
    // not a valid freshness check -- the allocator may legitimately reuse those addresses.
    EXPECT_EQ(std::any_cast<std::string>(after[0]->FixedArguments()[1].Value()),
              firstNamespace);
    EXPECT_EQ(std::any_cast<std::string>(after[3]->FixedArguments()[1].Value()),
              "System.Windows.Input");
    EXPECT_NE(after[0], after[3]);
}

// ---------------------------------------------------------------------------
// M6e: GetModuleAttributes is always empty.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, ModuleAttributesEmpty)
{
    ResolvedModule m;
    m.SeedThreeNamespaces();
    EXPECT_TRUE(m.Module()->GetModuleAttributes().empty());
}

// ---------------------------------------------------------------------------
// M7: InternalsVisibleTo is self-only -- false for another synthetic module and false
// for the compilation's main module.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, InternalsVisibleToSelfOnly)
{
    ResolvedModule m;
    TS::SimpleTypeResolveContext context(m.Compilation());
    auto reference = Baml::SyntheticWpfModule::CreateReference(
        std::make_shared<ILSpy::Decompiler::Metadata::AssemblyNameReference>(
            ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                kFrameworkFullAssemblyName)),
        std::optional<std::string>(kPresentationXmlns));
    const TS::IModule* other = reference->Resolve(context);

    EXPECT_TRUE(m.Module()->InternalsVisibleTo(*m.Module()));
    EXPECT_FALSE(m.Module()->InternalsVisibleTo(*other));
    EXPECT_FALSE(m.Module()->InternalsVisibleTo(m.Compilation().MainModule()));
}

// ---------------------------------------------------------------------------
// M8: a reference created WITHOUT a presentation xmlns never reconstructs attributes,
// even after registrations; its assembly identity routes through its own reference.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, NullPresentationXmlnsNoAttributes)
{
    FixtureCompilation compilation;
    auto reference = Baml::SyntheticWpfModule::CreateReference(
        std::make_shared<ILSpy::Decompiler::Metadata::AssemblyNameReference>(
            ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                "System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089")),
        std::optional<std::string>());
    TS::SimpleTypeResolveContext context(compilation.Compilation());
    const TS::IModule* module = reference->Resolve(context);

    // RegisterType is a concrete-class member (the ResolvedModule fixture's note).
    auto* synthetic = const_cast<Baml::SyntheticWpfModule*>(
        static_cast<const Baml::SyntheticWpfModule*>(module));
    synthetic->RegisterType("System.Xml", "XmlReader");

    EXPECT_EQ(module->AssemblyName(), "System.Xml");
    EXPECT_TRUE(module->GetAssemblyAttributes().empty());
}

// ---------------------------------------------------------------------------
// M9: when the synthetic reference IS the compilation's main assembly (the probe's
// `new SimpleCompilation(reference)`), IsMainModule is true and the main module is a
// fresh synthetic instance (distinct from any previously resolved one).
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, IsMainModuleTrueWhenMainAssembly)
{
    auto reference = Baml::SyntheticWpfModule::CreateReference(
        std::make_shared<ILSpy::Decompiler::Metadata::AssemblyNameReference>(
            ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                kFrameworkFullAssemblyName)),
        std::optional<std::string>(kPresentationXmlns));

    TS::SimpleCompilation compilation(*reference, {});
    const TS::IModule& main = compilation.MainModule();
    EXPECT_TRUE(main.IsMainModule());
    EXPECT_EQ(main.SymbolKind(), TS::SymbolKind::Module);
    EXPECT_EQ(main.AssemblyName(), "PresentationFramework");
}

// ---------------------------------------------------------------------------
// M10: the full SyntheticTypeDefinition surface over a registered type -- identity,
// kind, nullability, the empty member families and member lookups (the
// KnownThings.InitMember GetProperties shape), the entity flags, and the token.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, SyntheticTypeDefinitionSurface)
{
    ResolvedModule m;
    const TS::ITypeDefinition* button = m.RegisterType("System.Windows.Controls", "Button");

    // Identity.
    EXPECT_EQ(button->Name(), "Button");
    EXPECT_EQ(button->MetadataName(), "Button");
    EXPECT_EQ(button->ReflectionName(), "System.Windows.Controls.Button");
    EXPECT_EQ(button->FullName(), "System.Windows.Controls.Button");
    EXPECT_EQ(button->Namespace(), "System.Windows.Controls");
    EXPECT_EQ(button->FullTypeName().ReflectionName(), "System.Windows.Controls.Button");

    // Kind and nullability: materialized WPF types are nullability-oblivious reference-type
    // classes.
    EXPECT_EQ(button->Kind(), TS::TypeKind::Class);
    EXPECT_EQ(button->IsReferenceType(), std::optional<bool>(true));
    EXPECT_FALSE(button->IsByRefLike());
    EXPECT_EQ(button->Nullability(), TS::Nullability::Oblivious);
    EXPECT_EQ(button->NullableContext(), TS::Nullability::Oblivious);
    EXPECT_EQ(button->TypeParameterCount(), 0);
    EXPECT_TRUE(button->TypeParameters().empty());
    EXPECT_TRUE(button->DirectBaseTypes().empty());

    // The member families and member lookups are all empty (the C# EmptyList explicit
    // implementations, the IType defaults' behavior).
    EXPECT_TRUE(button->NestedTypes().empty());
    EXPECT_TRUE(button->Members().empty());
    EXPECT_TRUE(button->Fields().empty());
    EXPECT_TRUE(button->Methods().empty());
    EXPECT_TRUE(button->Properties().empty());
    EXPECT_TRUE(button->Events().empty());
    EXPECT_TRUE(button->GetProperties(
                    [](const TS::IProperty* p) { return p->Name() == "X"; },
                    TS::GetMemberOptions::IgnoreInheritedMembers)
                    .empty());
    EXPECT_TRUE(button->GetMembers(nullptr, TS::GetMemberOptions::IgnoreInheritedMembers)
                    .empty());
    EXPECT_TRUE(button->GetMethods().empty());
    EXPECT_TRUE(button->GetNestedTypes().empty());
    EXPECT_TRUE(button->GetConstructors().empty());
    EXPECT_TRUE(button->GetAccessors().empty());
    EXPECT_TRUE(button->GetFields().empty());
    EXPECT_TRUE(button->GetEvents().empty());

    // The type-definition flags.
    EXPECT_EQ(button->KnownTypeCode(), TS::KnownTypeCode::None);
    ASSERT_NE(button->EnumUnderlyingType(), nullptr);
    EXPECT_EQ(button->EnumUnderlyingType()->ReflectionName(), "?");
    EXPECT_FALSE(button->HasExtensions());
    EXPECT_EQ(button->ExtensionInfo(), nullptr);
    EXPECT_FALSE(button->IsReadOnly());
    EXPECT_FALSE(button->IsRecord());

    // The entity surface.
    EXPECT_EQ(button->SymbolKind(), TS::SymbolKind::TypeDefinition);
    EXPECT_EQ(button->Accessibility(), TS::Accessibility::Public);
    EXPECT_FALSE(button->IsStatic());
    EXPECT_FALSE(button->IsAbstract());
    EXPECT_FALSE(button->IsSealed());
    EXPECT_EQ(button->DeclaringTypeDefinition(), nullptr);
    EXPECT_EQ(button->DeclaringType(), nullptr);
    EXPECT_TRUE(button->GetAttributes().empty());
    EXPECT_FALSE(button->HasAttribute(TS::KnownAttribute::Obsolete));
    EXPECT_EQ(button->GetAttribute(TS::KnownAttribute::Obsolete), nullptr);
    EXPECT_EQ(button->MetadataToken(), 0x02000000u);
    EXPECT_EQ(button->ParentModule(), m.Module());

    // GetDefinition returns the type itself.
    EXPECT_EQ(button->GetDefinition(), button);

    // The visitor dispatch: AcceptVisitor routes to VisitTypeDefinition and hands the
    // type back; VisitChildren returns this. The port's IType::AcceptVisitor /
    // ChangeNullability / VisitChildren are NON-CONST (the D406 convention), so the
    // const registered pointer is cast (the CSharpConversions const_cast precedent).
    auto* buttonMutable = const_cast<TS::ITypeDefinition*>(button);
    RecordingVisitor visitor;
    auto visited = buttonMutable->AcceptVisitor(visitor);
    EXPECT_EQ(visitor.visited, "VisitTypeDefinition:System.Windows.Controls.Button");
    EXPECT_EQ(visited.get(), button);
    RecordingVisitor childrenVisitor;
    EXPECT_EQ(buttonMutable->VisitChildren(childrenVisitor).get(), button);
    EXPECT_EQ(buttonMutable->ChangeNullability(TS::Nullability::Oblivious).get(), button);
    EXPECT_EQ(buttonMutable->ChangeNullability(TS::Nullability::Nullable).get(), button);

    // The C# ToString override (the nested class is public in the port so the test can
    // pin it directly -- the reflection-probe-equivalent access).
    const auto* asSynthetic =
        dynamic_cast<const Baml::SyntheticWpfModule::SyntheticTypeDefinition*>(button);
    ASSERT_NE(asSynthetic, nullptr);
    EXPECT_EQ(asSynthetic->ToString(), "[SyntheticWpfType System.Windows.Controls.Button]");
}

// ---------------------------------------------------------------------------
// M11: Equals is REFERENCE equality (the AbstractType default `this == other`).
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, EqualsIsReferenceEquality)
{
    ResolvedModule m;
    const TS::ITypeDefinition* button = m.RegisterType("System.Windows.Controls", "Button");
    const TS::ITypeDefinition* window = m.RegisterType("System.Windows", "Window");
    const TS::IType& stringType = m.Compilation().FindType(TS::KnownTypeCode::String);

    EXPECT_TRUE(button->Equals(*button));
    EXPECT_FALSE(button->Equals(*window));
    EXPECT_FALSE(button->Equals(stringType));
}

// ---------------------------------------------------------------------------
// M12: the XmlnsDefinitionAttribute type the reconstruction builds is NOT registered --
// a plain GetTypeDefinition lookup for it returns null, and it is not among the module's
// TypeDefinitions.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, AttributeTypeIsNotRegistered)
{
    ResolvedModule m;
    m.SeedThreeNamespaces();

    auto attributes = m.Module()->GetAssemblyAttributes();
    const TS::ITypeDefinition* attributeType =
        dynamic_cast<const TS::ITypeDefinition*>(&attributes[0]->AttributeType());
    ASSERT_NE(attributeType, nullptr);
    EXPECT_EQ(m.Module()->GetTypeDefinition(
                  TS::TopLevelTypeName("System.Windows.Markup", "XmlnsDefinitionAttribute")),
              nullptr);
    for (const auto* type : m.Module()->TypeDefinitions()) {
        EXPECT_NE(type, attributeType);
    }
}

// ---------------------------------------------------------------------------
// M14: the GetTypeDefinition lookup is by the FULL TopLevelTypeName including the type
// parameter count -- a name registered with arity 0 is not found under arity 1.
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, ArityMismatchLookupMisses)
{
    ResolvedModule m;
    m.RegisterType("System.Windows.Controls", "Button");

    EXPECT_EQ(m.Module()->GetTypeDefinition(
                  TS::TopLevelTypeName("System.Windows.Controls", "Button", 1)),
              nullptr);
}

// ---------------------------------------------------------------------------
// The three nested classes and the module are `final` (the C# `sealed`).
// ---------------------------------------------------------------------------
TEST(SyntheticWpfModuleTest, SealedClassTraits)
{
    static_assert(std::is_final_v<Baml::SyntheticWpfModule>,
                  "the C# SyntheticWpfModule is sealed");
    static_assert(std::is_final_v<Baml::SyntheticWpfModule::SyntheticModuleReference>,
                  "the C# SyntheticModuleReference is sealed");
    static_assert(std::is_final_v<Baml::SyntheticWpfModule::SyntheticNamespace>,
                  "the C# SyntheticNamespace is sealed");
    static_assert(std::is_final_v<Baml::SyntheticWpfModule::SyntheticTypeDefinition>,
                  "the C# SyntheticTypeDefinition is sealed");
    static_assert(std::is_polymorphic_v<Baml::SyntheticWpfModule>,
                  "IModule is a polymorphic interface");
    static_assert(std::has_virtual_destructor_v<Baml::SyntheticWpfModule>,
                  "the ISymbol base has a virtual destructor");
}
