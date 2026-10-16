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

// Tests for AutoEventDecompiler -- recognizing automatic (field-like) events by
// structurally matching the compiler-generated add/remove accessor ILAst. The
// synthetic tests build the three recognized shapes (the Roslyn compare-exchange
// loop, the mcs compare-exchange loop, and the pre-4.0 combine assignment) over a
// hand-built block tree; the metadata-backed tests exercise FindBackingField and
// the IsAutomaticEvent memoization over a real module.

#include "Decompiler/CSharp/AutoEventDecompiler.hpp"

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/EventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCache.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultAttribute.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace IL = ::ILSpy::Decompiler::IL;
namespace CSharp = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace TM = ::ILSpy::Decompiler::Metadata;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// A trivial compilation over the fake type system for the synthetic matcher tests.
struct FieldFixture {
    TS::SimpleCompilation compilation{ Impl::MinimalCorlib::Instance(), {} };
    TS::ITypePtr fieldType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    std::shared_ptr<Impl::FakeField> makeField() {
        auto field = std::make_shared<Impl::FakeField>(compilation);
        field->SetReturnType(fieldType);
        return field;
    }
    std::shared_ptr<Impl::FakeMethod> makeInstanceAccessor() {
        auto accessor = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        accessor->SetIsStatic(false);
        return accessor;
    }
};

IL::ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

IL::ILVariablePtr MakeParameter(int index, std::string name) {
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Parameter, nullptr, index);
    v->Name = std::move(name);
    return v;
}

// An instance field load: ldobj(T, ldflda(field, ldloc(this))).
std::unique_ptr<IL::ILInstruction> LoadField(IL::ILVariablePtr thisVar,
                                             const std::shared_ptr<TS::IField>& field,
                                             TS::ITypePtr type) {
    auto ldflda = std::make_unique<IL::LdFlda>(
        std::make_unique<IL::LdLoc>(thisVar), "F");
    ldflda->Field = field;
    return std::make_unique<IL::LdObj>(std::move(ldflda), std::move(type));
}

// A castclass over the given argument.
std::unique_ptr<IL::ILInstruction> Cast(TS::ITypePtr type,
                                        std::unique_ptr<IL::ILInstruction> argument) {
    return std::make_unique<IL::CastClass>(std::move(type), std::move(argument));
}

// Delegate.Combine/Remove(comparand, value).
std::unique_ptr<IL::ILInstruction> DelegateCombine(IL::ILVariablePtr comparand,
                                                   IL::ILVariablePtr value,
                                                   bool isAdd) {
    auto call = std::make_unique<IL::Call>(
        isAdd ? "System.Delegate::Combine" : "System.Delegate::Remove");
    call->AddArg(std::make_unique<IL::LdLoc>(comparand));
    call->AddArg(std::make_unique<IL::LdLoc>(value));
    return call;
}

// Interlocked.CompareExchange(ref field, newValue, comparand).
std::unique_ptr<IL::ILInstruction> CompareExchange(IL::ILVariablePtr thisVar,
                                                   const std::shared_ptr<TS::IField>& field,
                                                   IL::ILVariablePtr newValue,
                                                   IL::ILVariablePtr comparand) {
    auto call = std::make_unique<IL::Call>(
        "System.Threading.Interlocked::CompareExchange");
    auto ldflda = std::make_unique<IL::LdFlda>(
        std::make_unique<IL::LdLoc>(thisVar), "F");
    ldflda->Field = field;
    call->AddArg(std::move(ldflda));
    call->AddArg(std::make_unique<IL::LdLoc>(newValue));
    call->AddArg(std::make_unique<IL::LdLoc>(comparand));
    return call;
}

// The three recognized accessor shapes share this fixture: a function body whose
// single container holds one block.
struct AccessorFixture {
    FieldFixture fields;
    std::shared_ptr<Impl::FakeField> field = fields.makeField();
    std::shared_ptr<Impl::FakeMethod> accessor = fields.makeInstanceAccessor();
    IL::ILVariablePtr thisVar = MakeParameter(-1, "this");
    IL::ILVariablePtr valueVar = MakeParameter(0, "value");

    std::unique_ptr<IL::BlockContainer> functionBody =
        std::make_unique<IL::BlockContainer>();
    IL::Block* body = nullptr;

    AccessorFixture() {
        auto b = std::make_unique<IL::Block>();
        body = b.get();
        functionBody->AddBlock(std::move(b));
    }

    bool Match(bool isAdd) {
        return CSharp::AutoEventDecompiler::MatchAutomaticAccessorBody(
            *body, *functionBody, *accessor, *field, isAdd);
    }
};

// The Roslyn compare-exchange loop:
//   T oldValue = this.field;
//   while (true) {
//       T comparand = oldValue;
//       T newValue = (T)Delegate.Combine(comparand, value);
//       oldValue = Interlocked.CompareExchange(ref this.field, newValue, comparand);
//       if (oldValue == comparand) break;
//   }
//   leave;
void BuildCompareExchangeLoop(AccessorFixture& f, bool isAdd) {
    IL::ILVariablePtr oldValue = MakeLocal("oldValue");
    IL::ILVariablePtr comparand = MakeLocal("comparand");
    IL::ILVariablePtr newValue = MakeLocal("newValue");

    f.body->Add(std::make_unique<IL::StLoc>(
        oldValue, LoadField(f.thisVar, f.field, f.fields.fieldType)));

    auto loop = std::make_unique<IL::BlockContainer>();
    loop->Kind = IL::ContainerKind::Loop;
    auto loopBody = std::make_unique<IL::Block>();
    IL::Block* loopBodyPtr = loopBody.get();
    loop->AddBlock(std::move(loopBody));
    IL::BlockContainer* loopPtr = loop.get();
    f.body->Add(std::move(loop));

    loopBodyPtr->Add(std::make_unique<IL::StLoc>(
        comparand, std::make_unique<IL::LdLoc>(oldValue)));
    loopBodyPtr->Add(std::make_unique<IL::StLoc>(
        newValue, Cast(f.fields.fieldType,
                       DelegateCombine(comparand, f.valueVar, isAdd))));
    loopBodyPtr->Add(std::make_unique<IL::StLoc>(
        oldValue, CompareExchange(f.thisVar, f.field, newValue, comparand)));
    loopBodyPtr->Add(std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::Comp>(std::make_unique<IL::LdLoc>(oldValue),
                                   std::make_unique<IL::LdLoc>(comparand),
                                   IL::ComparisonKind::Equality),
        std::make_unique<IL::Leave>(loopPtr)));
    loopBodyPtr->SetFinal(std::make_unique<IL::Branch>(loopBodyPtr));

    f.body->SetFinal(std::make_unique<IL::Leave>(f.functionBody.get()));
}

// The mcs compare-exchange loop:
//   T oldValue = this.field;
//   while (true) {
//       T comparand = oldValue;
//       oldValue = Interlocked.CompareExchange(ref this.field,
//                    (T)Delegate.Combine(comparand, value), oldValue);
//       if (oldValue == comparand) break;
//   }
void BuildCompareExchangeLoopMcs(AccessorFixture& f, bool isAdd) {
    IL::ILVariablePtr oldValue = MakeLocal("oldValue");
    IL::ILVariablePtr comparand = MakeLocal("comparand");

    f.body->Add(std::make_unique<IL::StLoc>(
        oldValue, LoadField(f.thisVar, f.field, f.fields.fieldType)));

    auto loop = std::make_unique<IL::BlockContainer>();
    loop->Kind = IL::ContainerKind::Loop;
    auto loopBody = std::make_unique<IL::Block>();
    IL::Block* loopBodyPtr = loopBody.get();
    loop->AddBlock(std::move(loopBody));
    IL::BlockContainer* loopPtr = loop.get();
    f.body->Add(std::move(loop));

    loopBodyPtr->Add(std::make_unique<IL::StLoc>(
        comparand, std::make_unique<IL::LdLoc>(oldValue)));
    auto exchange = std::make_unique<IL::Call>(
        "System.Threading.Interlocked::CompareExchange");
    {
        auto ldflda = std::make_unique<IL::LdFlda>(
            std::make_unique<IL::LdLoc>(f.thisVar), "F");
        ldflda->Field = f.field;
        exchange->AddArg(std::move(ldflda));
    }
    exchange->AddArg(Cast(f.fields.fieldType,
                          DelegateCombine(comparand, f.valueVar, isAdd)));
    exchange->AddArg(std::make_unique<IL::LdLoc>(oldValue));
    loopBodyPtr->Add(std::make_unique<IL::StLoc>(oldValue, std::move(exchange)));
    loopBodyPtr->Add(std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::Comp>(std::make_unique<IL::LdLoc>(oldValue),
                                   std::make_unique<IL::LdLoc>(comparand),
                                   IL::ComparisonKind::Equality),
        std::make_unique<IL::Leave>(loopPtr)));
    loopBodyPtr->SetFinal(std::make_unique<IL::Branch>(loopBodyPtr));

    f.body->SetFinal(std::make_unique<IL::Leave>(f.functionBody.get()));
}

// The pre-4.0 combine assignment:
//   this.field = (T)Delegate.Combine(this.field, value);
void BuildSimpleCombineAssignment(AccessorFixture& f, bool isAdd) {
    auto stobj = std::make_unique<IL::StObj>(
        [&] {
            auto ldflda = std::make_unique<IL::LdFlda>(
                std::make_unique<IL::LdLoc>(f.thisVar), "F");
            ldflda->Field = f.field;
            return std::unique_ptr<IL::ILInstruction>(std::move(ldflda));
        }(),
        Cast(f.fields.fieldType,
             [&] {
                 auto call = std::make_unique<IL::Call>(
                     isAdd ? "System.Delegate::Combine" : "System.Delegate::Remove");
                 call->AddArg(LoadField(f.thisVar, f.field, f.fields.fieldType));
                 call->AddArg(std::make_unique<IL::LdLoc>(f.valueVar));
                 return std::unique_ptr<IL::ILInstruction>(std::move(call));
             }()),
        f.fields.fieldType);
    f.body->Add(std::move(stobj));
    f.body->SetFinal(std::make_unique<IL::Leave>(f.functionBody.get()));
}

} // namespace

// ---- The Roslyn compare-exchange loop ----

TEST(AutoEventDecompilerTest, MatchesCompareExchangeLoopForAddAccessor) {
    AccessorFixture f;
    BuildCompareExchangeLoop(f, true);
    EXPECT_TRUE(f.Match(true));
}

TEST(AutoEventDecompilerTest, MatchesCompareExchangeLoopForRemoveAccessor) {
    AccessorFixture f;
    BuildCompareExchangeLoop(f, false);
    EXPECT_TRUE(f.Match(false));
}

TEST(AutoEventDecompilerTest, RejectsCompareExchangeLoopWithWrongCombineKind) {
    AccessorFixture f;
    BuildCompareExchangeLoop(f, true);
    // The remove accessor must call Delegate.Remove, not Delegate.Combine.
    EXPECT_FALSE(f.Match(false));
}

TEST(AutoEventDecompilerTest, RejectsCompareExchangeLoopWithoutInit) {
    AccessorFixture f;
    BuildCompareExchangeLoop(f, true);
    // Drop the leading `oldValue = this.field` statement.
    f.body->Instructions.erase(f.body->Instructions.begin());
    EXPECT_FALSE(f.Match(true));
}

// ---- The mcs compare-exchange loop ----

TEST(AutoEventDecompilerTest, MatchesCompareExchangeLoopMcs) {
    AccessorFixture f;
    BuildCompareExchangeLoopMcs(f, true);
    EXPECT_TRUE(f.Match(true));
}

TEST(AutoEventDecompilerTest, RejectsMcsLoopWithWrongField) {
    AccessorFixture f;
    BuildCompareExchangeLoopMcs(f, true);
    auto other = f.fields.makeField();
    // Re-point the compare-exchange address-of at a different field.
    auto* loopBlock = static_cast<IL::BlockContainer*>(f.body->Instructions[1].get())
                          ->EntryPoint();
    auto* stloc = dynamic_cast<IL::StLoc*>(loopBlock->Instructions[1].get());
    ASSERT_NE(stloc, nullptr);
    auto* call = dynamic_cast<IL::Call*>(stloc->Value.get());
    ASSERT_NE(call, nullptr);
    auto* ldflda = dynamic_cast<IL::LdFlda*>(call->Arguments[0].get());
    ASSERT_NE(ldflda, nullptr);
    ldflda->Field = other;
    EXPECT_FALSE(f.Match(true));
}

// ---- The simple combine assignment ----

TEST(AutoEventDecompilerTest, MatchesSimpleCombineAssignment) {
    AccessorFixture f;
    BuildSimpleCombineAssignment(f, true);
    EXPECT_TRUE(f.Match(true));
}

TEST(AutoEventDecompilerTest, RejectsSimpleCombineAssignmentWithWrongField) {
    AccessorFixture f;
    BuildSimpleCombineAssignment(f, true);
    auto* stobj = dynamic_cast<IL::StObj*>(f.body->Instructions[0].get());
    ASSERT_NE(stobj, nullptr);
    auto* ldflda = dynamic_cast<IL::LdFlda*>(stobj->Target.get());
    ASSERT_NE(ldflda, nullptr);
    ldflda->Field = f.fields.makeField();
    EXPECT_FALSE(f.Match(true));
}

// ---- FindBackingField / IsAutomaticEvent over real metadata ----

// The metadata-backed module and DecompileRun the recognition tests share.
struct MetadataFixture {
    class Compilation : public TS::ICompilation {
    public:
        void SetMainModule(const TS::IModule* module) { mainModule_ = module; }
        const TS::IModule& MainModule() const override { return *mainModule_; }
        std::vector<const TS::IModule*> Modules() const override {
            return { mainModule_ };
        }
        std::vector<const TS::IModule*> ReferencedModules() const override { return {}; }
        const TS::INamespace& RootNamespace() const override {
            return mainModule_->RootNamespace();
        }
        const TS::INamespace* GetNamespaceForExternAlias(const std::string&) const override {
            return nullptr;
        }
        const TS::IType& FindType(TS::KnownTypeCode code) const override {
            return knownTypes_.FindType(code);
        }
        const TS::StringComparer& NameComparer() const override {
            return TS::StringComparer::Ordinal();
        }
        const ::ILSpy::Decompiler::Util::CacheManager& CacheManager() const override {
            return cacheManager_;
        }
        TS::TypeSystemOptions TypeSystemOptions() const override {
            return TS::TypeSystemOptions::Default;
        }
    private:
        const TS::IModule* mainModule_ = nullptr;
        ::ILSpy::Decompiler::Util::CacheManager cacheManager_;
        TS::KnownTypeCache knownTypes_{ *this };
    };

    TM::MetadataFile file;
    Compilation compilation;
    TS::MetadataModule module;
    DecompilerSettings settings;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> root;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;

    explicit MetadataFixture(const char* path)
        : file(path),
          module(compilation, &file, TS::TypeSystemOptions::Default),
          root(std::make_shared<
               ::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(module)),
          usingScope(std::make_shared<
                     ::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope>(
              root, module.RootNamespace(),
              std::vector<const TS::INamespace*>{})),
          run(&settings, usingScope) {
        compilation.SetMainModule(&module);
    }

    const TS::IEvent* FindEvent(const std::string& typeName, const std::string& eventName) {
        const TS::ITypeDefinition* type =
            module.GetTypeDefinition(TS::TopLevelTypeName("System", typeName));
        if (type == nullptr)
            return nullptr;
        for (const TS::IEvent* e : type->Events())
            if (e->Name() == eventName)
                return e;
        return nullptr;
    }
};

// A real C# field-like event's backing field (a same-named, private, same-static-ness
// field the PropertyAndEventBackingFieldLookup associates with the event) is found.
TEST(AutoEventDecompilerTest, FindsRealFieldLikeEventBackingField) {
    const char* path = MscorlibPath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "mscorlib fixture not present";
    MetadataFixture fixture(path);
    ASSERT_TRUE(fixture.file.IsValid());
    const TS::IEvent* event = fixture.FindEvent("AppDomain", "AssemblyLoad");
    ASSERT_NE(event, nullptr);

    const TS::IField* backingField =
        CSharp::AutoEventDecompiler::FindBackingField(*event);
    ASSERT_NE(backingField, nullptr);
    EXPECT_EQ(backingField->Name(), "AssemblyLoad");
    EXPECT_EQ(backingField->Accessibility(), TS::Accessibility::Private);
}

// IsAutomaticEvent memoizes its verdict (including the negative one) in the run.
TEST(AutoEventDecompilerTest, MemoizesAutomaticEventVerdict) {
    const char* path = MscorlibPath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "mscorlib fixture not present";
    MetadataFixture fixture(path);
    ASSERT_TRUE(fixture.file.IsValid());
    const TS::IEvent* event = fixture.FindEvent("AppDomain", "AssemblyLoad");
    ASSERT_NE(event, nullptr);

    // The un-memoized verdict is the source of truth (the port's control-flow
    // pipeline does not produce the C# accessor shape for the real body, so this
    // is the negative memoization path).
    const TS::IField* direct = nullptr;
    bool expected = CSharp::AutoEventDecompiler::IsAutomaticEvent(
        fixture.file, *event, direct);

    const TS::IField* backingField = nullptr;
    EXPECT_EQ(CSharp::AutoEventDecompiler::IsAutomaticEvent(
                  fixture.run, fixture.file, *event, backingField),
              expected);
    EXPECT_EQ(backingField, expected ? direct : nullptr);
    // The verdict is now cached (present in the run's map, keyed by the event).
    const auto& cache = fixture.run.AutomaticEvents();
    ASSERT_EQ(cache.count(event), 1u);
    EXPECT_EQ(cache.at(event), backingField);
}

// An event with no declaring type (the fake-member shape) has no backing field.
TEST(AutoEventDecompilerTest, DoesNotFindBackingFieldWithoutDeclaringType) {
    TS::SimpleCompilation compilation{ Impl::MinimalCorlib::Instance(), {} };
    Impl::FakeEvent event(compilation);
    EXPECT_EQ(CSharp::AutoEventDecompiler::FindBackingField(event), nullptr);
}

// ---- AddFieldLikeEventAttributes -----------------------------------------------

namespace {

// An IType whose FullName is the exact removal-set string while its Name/Namespace
// render normally (the C# attribute-type FullName comparison). The LookupTypeDefinition
// FullName returns the first ctor argument, so the short-name field carries the
// rendered name.
std::shared_ptr<TS::TestSupport::LookupTypeDefinition> MakeAttributeType(
    const TS::ICompilation& compilation, const std::string& ns,
    const std::string& shortName) {
    const std::string fullName = ns.empty() ? shortName : ns + "." + shortName;
    return std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        fullName, ns, TS::FullTypeName(TS::TopLevelTypeName(ns, shortName, 0)),
        TS::TypeKind::Class, TS::Accessibility::Public, compilation, nullptr,
        TS::KnownTypeCode::None);
}

// A FakeMethod whose attribute list is configurable.
class AttributedMethod : public Impl::FakeMethod {
public:
    explicit AttributedMethod(const TS::ICompilation& compilation)
        : Impl::FakeMethod(compilation, TS::SymbolKind::Method) {}
    std::vector<const TS::IAttribute*> Attributes;
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return Attributes;
    }
};

// A FakeField whose attribute list is configurable.
class AttributedField : public Impl::FakeField {
public:
    explicit AttributedField(const TS::ICompilation& compilation)
        : Impl::FakeField(compilation) {}
    std::vector<const TS::IAttribute*> Attributes;
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return Attributes;
    }
};

// The attribute fixture: an add accessor plus backing field with configurable
// attributes, an event wired to the accessor, and the ast builder / event declaration
// AddFieldLikeEventAttributes consumes.
struct AutoEventAttributesFixture {
    TS::TestSupport::LookupCompilation compilation;
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> compilerGenerated =
        MakeAttributeType(compilation, "System.Runtime.CompilerServices",
                          "CompilerGeneratedAttribute");
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> debuggerBrowsable =
        MakeAttributeType(compilation, "System.Diagnostics",
                          "DebuggerBrowsableAttribute");
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> methodImpl =
        MakeAttributeType(compilation, "System.Runtime.CompilerServices",
                          "MethodImplAttribute");
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> customA =
        MakeAttributeType(compilation, "N", "CustomA");
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> customB =
        MakeAttributeType(compilation, "N", "CustomB");
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> customC =
        MakeAttributeType(compilation, "N", "CustomC");

    std::shared_ptr<AttributedMethod> addAccessor =
        std::make_shared<AttributedMethod>(compilation);
    std::shared_ptr<AttributedField> backingField =
        std::make_shared<AttributedField>(compilation);
    Impl::FakeEvent event{ compilation };
    Syntax::TypeSystemAstBuilder builder;
    Syntax::EventDeclaration eventDecl;

    AutoEventAttributesFixture() {
        event.SetAddAccessor(addAccessor.get());
    }

    // A DefaultAttribute over the given type, kept alive by the fixture (the
    // configurable attribute vectors hold non-owning pointers).
    const TS::IAttribute* Attr(
        const std::shared_ptr<TS::TestSupport::LookupTypeDefinition>& type) {
        ownedAttributes.push_back(std::make_shared<Impl::DefaultAttribute>(
            type, std::vector<TS::CustomAttributeTypedArgument>{},
            std::vector<TS::CustomAttributeNamedArgument>{}));
        return ownedAttributes.back().get();
    }

    void Run() {
        CSharp::AutoEventDecompiler::AddFieldLikeEventAttributes(
            eventDecl, builder, event, *backingField);
    }

private:
    std::vector<std::shared_ptr<TS::IAttribute>> ownedAttributes;
};

// The rendered leaf name of a section's single attribute type (the stripped short
// name).
std::string SectionMemberName(const Syntax::AttributeSection& section) {
    auto* memberType =
        dynamic_cast<Syntax::MemberType*>(section.Attributes()[0]->Type());
    return memberType != nullptr ? memberType->MemberName() : std::string();
}

} // namespace

// The accessor attributes become `method:` sections and the backing-field
// attributes become `field:` sections, in that order.
TEST(AutoEventDecompilerTest, AddFieldLikeEventAttributesTargetsMethodThenField) {
    AutoEventAttributesFixture f;
    f.addAccessor->Attributes = { f.Attr(f.customA) };
    f.backingField->Attributes = { f.Attr(f.customC) };

    f.Run();

    ASSERT_EQ(f.eventDecl.Attributes().Count(), 2);
    EXPECT_EQ(f.eventDecl.Attributes()[0]->AttributeTarget(), "method");
    EXPECT_EQ(f.eventDecl.Attributes()[1]->AttributeTarget(), "field");
    EXPECT_EQ(SectionMemberName(*f.eventDecl.Attributes()[0]), "CustomA");
    EXPECT_EQ(SectionMemberName(*f.eventDecl.Attributes()[1]), "CustomC");
}

// The accessor's [CompilerGenerated] is dropped; the other attribute survives.
TEST(AutoEventDecompilerTest, AddFieldLikeEventAttributesDropsAccessorCompilerGenerated) {
    AutoEventAttributesFixture f;
    f.addAccessor->Attributes = { f.Attr(f.compilerGenerated), f.Attr(f.customA) };

    f.Run();

    ASSERT_EQ(f.eventDecl.Attributes().Count(), 1);
    EXPECT_EQ(f.eventDecl.Attributes()[0]->AttributeTarget(), "method");
    EXPECT_EQ(SectionMemberName(*f.eventDecl.Attributes()[0]), "CustomA");
}

// The accessor's [MethodImpl] is dropped; the other attribute survives.
TEST(AutoEventDecompilerTest, AddFieldLikeEventAttributesDropsAccessorMethodImpl) {
    AutoEventAttributesFixture f;
    f.addAccessor->Attributes = { f.Attr(f.methodImpl), f.Attr(f.customB) };

    f.Run();

    ASSERT_EQ(f.eventDecl.Attributes().Count(), 1);
    EXPECT_EQ(SectionMemberName(*f.eventDecl.Attributes()[0]), "CustomB");
}

// The backing field's [CompilerGenerated] and [DebuggerBrowsable] are dropped.
TEST(AutoEventDecompilerTest, AddFieldLikeEventAttributesDropsFieldCompilerGeneratedAndBrowsable) {
    AutoEventAttributesFixture f;
    f.backingField->Attributes = { f.Attr(f.compilerGenerated),
                                   f.Attr(f.debuggerBrowsable),
                                   f.Attr(f.customC) };

    f.Run();

    ASSERT_EQ(f.eventDecl.Attributes().Count(), 1);
    EXPECT_EQ(f.eventDecl.Attributes()[0]->AttributeTarget(), "field");
    EXPECT_EQ(SectionMemberName(*f.eventDecl.Attributes()[0]), "CustomC");
}

// When every attribute is in the removal set, no sections are added.
TEST(AutoEventDecompilerTest, AddFieldLikeEventAttributesDropsAllRemovedAttributes) {
    AutoEventAttributesFixture f;
    f.addAccessor->Attributes = { f.Attr(f.compilerGenerated), f.Attr(f.methodImpl) };
    f.backingField->Attributes = { f.Attr(f.compilerGenerated) };

    f.Run();

    EXPECT_EQ(f.eventDecl.Attributes().Count(), 0);
}

// The kept attributes preserve their input order (WithoutAttributeTypes is a filter,
// not a reorder).
TEST(AutoEventDecompilerTest, AddFieldLikeEventAttributesPreservesKeptAccessorOrder) {
    AutoEventAttributesFixture f;
    f.addAccessor->Attributes = { f.Attr(f.customB), f.Attr(f.customA) };

    f.Run();

    ASSERT_EQ(f.eventDecl.Attributes().Count(), 2);
    EXPECT_EQ(SectionMemberName(*f.eventDecl.Attributes()[0]), "CustomB");
    EXPECT_EQ(SectionMemberName(*f.eventDecl.Attributes()[1]), "CustomA");
}
