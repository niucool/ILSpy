// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for ProxyCallReplacer (the port of
// ICSharpCode.Decompiler/IL/Transforms/ProxyCallReplacer.cs): the rewrite of a
// compiler-generated pass-through method call into the method it forwards to.
// The C# decodes the proxy's body with a fresh IL reader and runs the early
// transform list over it; the port routes the decode through the context's
// DelegateBodyResolver hook (the CreateILReader bridge the delegate
// construction path established), so the tests hand the hook a built proxy
// body.

#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/Transforms/ProxyCallReplacer.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace IL = ::ILSpy::Decompiler::IL;

// A local for the fixture bodies (the DelegateConstruction MakeLocal pattern).
std::shared_ptr<IL::ILVariable> MakeLocal(const char* name) {
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                              nullptr, 0);
    v->Name = name;
    return v;
}

// A parameter variable (Kind Parameter, the given index -- the proxy body's
// ldloc checks read `var.Index == i - 1`, so `this` is -1 and the first
// parameter 0).
std::shared_ptr<IL::ILVariable> MakeParameter(const char* name, int index) {
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Parameter,
                                              nullptr, index);
    v->Name = name;
    v->StoreCount = 1;  // the parameter arrives with a value
    return v;
}

// A configurable resolved method (the DelegateTargetMethodStub pattern):
// carries a MethodDef token, the declaring type definition, the
// compiler-generated flag (the [CompilerGenerated] attribute read
// IsCompilerGeneratedOrIsInCompilerGeneratedClass makes), the parameter
// count, and the static flag.
class ProxyMethodStub : public TS::IMethod {
public:
    ProxyMethodStub(std::string name, std::uint32_t token,
                    const TS::ITypeDefinition* declaringType,
                    bool compilerGenerated, bool isStatic,
                    int parameterCount)
        : name_(std::move(name)), token_(token),
          declaringType_(declaringType),
          compilerGenerated_(compilerGenerated), isStatic_(isStatic),
          parameterCount_(parameterCount) {}

    // --- ISymbol / INamedElement ---
    TS::SymbolKind SymbolKind() const override {
        return TS::SymbolKind::Method;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return "T::" + name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return std::string(); }
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("ProxyMethodStub::Compilation");
    }
    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override {
        return parameters_;
    }
    void SetParameters(std::vector<const TS::IParameter*> p) {
        parameters_ = std::move(p);
    }
    // --- IMember ---
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *returnType_; }
    void SetReturnType(TS::ITypePtr type) { returnType_ = std::move(type); }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override {
        return &TS::TypeParameterSubstitution::Identity();
    }
    const TS::IMethod* Specialize(
        const TS::TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor*) const override {
        return obj == this;
    }
    // --- IMethod (the IEntity group) ---
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return {};
    }
    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return compilerGenerated_ &&
               attribute == TS::KnownAttribute::CompilerGenerated;
    }
    const TS::IAttribute* GetAttribute(
        TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Private;
    }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return token_; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return declaringType_;
    }
    TS::ITypePtr DeclaringType() const override { return nullptr; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetReturnTypeAttributes()
        const override {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    std::vector<const TS::ITypeParameter*> TypeParameters()
        const override {
        return {};
    }
    std::vector<TS::ITypePtr> TypeArguments() const override {
        return {};
    }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return true; }
    bool IsAccessor() const override { return false; }
    const TS::IMember* AccessorOwner() const override { return nullptr; }
    TS::MethodSemanticsAttributes AccessorKind() const override {
        return TS::MethodSemanticsAttributes::None;
    }
    const TS::IMethod* ReducedFrom() const override { return nullptr; }

private:
    TS::ITypePtr returnType_;
    std::string name_;
    std::uint32_t token_;
    const TS::ITypeDefinition* declaringType_;
    bool compilerGenerated_;
    bool isStatic_;
    int parameterCount_;
    std::vector<const TS::IParameter*> parameters_;
};

// The parameter stub (the CSharpResolverBinaryOperator_Test TestParameter
// pattern): a parameter named "p" of an unknown type.
class StubParameter : public TS::IParameter {
public:
    explicit StubParameter(TS::ITypePtr type) : type_(std::move(type)) {}
    TS::SymbolKind SymbolKind() const override {
        return TS::SymbolKind::Parameter;
    }
    std::string Name() const override { return "p"; }
    const TS::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return {};
    }
    TS::ReferenceKind ReferenceKind() const override {
        return TS::ReferenceKind::None;
    }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const TS::IParameterizedMember* Owner() const override {
        return nullptr;
    }
    TS::LifetimeAnnotation Lifetime() const override { return {}; }

private:
    TS::ITypePtr type_;
};

// The hand-wired Run fixture: an outer function whose single block holds a
// call to a compiler-generated proxy method; the context's decode hook hands
// back the proxy's body (a leave of the target call over the this/parameter
// loads -- the pass-through shape the transform looks for).
struct RunFixture {
    std::unique_ptr<IL::ILFunction> fn;
    std::unique_ptr<TS::TestSupport::LookupCompilation> compilation;
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> hostType;
    std::shared_ptr<ProxyMethodStub> containingMethod;
    std::shared_ptr<ProxyMethodStub> proxyMethod;
    std::shared_ptr<ProxyMethodStub> targetMethod;
    std::shared_ptr<IL::ILVariable> outerArgVar;
    IL::Block* entry = nullptr;
    IL::Call* proxyCall = nullptr;
    bool decodeHit = false;
    std::shared_ptr<StubParameter> paramStub_;

    RunFixture() {
        compilation =
            std::make_unique<TS::TestSupport::LookupCompilation>();
        hostType = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
            "Host", "",
            TS::FullTypeName(TS::TopLevelTypeName("", "Host", 0)),
            TS::TypeKind::Class, TS::Accessibility::Public, *compilation,
            nullptr);
        // `Task<int> M(int)`: the containing method (the function's Method).
        containingMethod = std::make_shared<ProxyMethodStub>(
            "M", 0x06000001u, hostType.get(), /*compilerGenerated=*/false,
            /*isStatic=*/false, /*parameterCount=*/1);
        // `<M>b__0_0(int)`: the compiler-generated proxy (a MethodDef token,
        // an instance method, one parameter).
        proxyMethod = std::make_shared<ProxyMethodStub>(
            "<M>b__0_0", 0x06000042u, hostType.get(),
            /*compilerGenerated=*/true, /*isStatic=*/false,
            /*parameterCount=*/1);
        // `Target(int)`: the method the proxy forwards to.
        targetMethod = std::make_shared<ProxyMethodStub>(
            "Target", 0x06000043u, hostType.get(),
            /*compilerGenerated=*/false, /*isStatic=*/false,
            /*parameterCount=*/1);
        auto returnType =
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
        containingMethod->SetReturnType(returnType);
        proxyMethod->SetReturnType(returnType);
        targetMethod->SetReturnType(returnType);
        auto paramType =
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
        auto param = std::make_shared<StubParameter>(paramType);
        std::vector<const TS::IParameter*> params{param.get()};
        containingMethod->SetParameters(params);
        proxyMethod->SetParameters(params);
        targetMethod->SetParameters(params);
        // Keep the parameter stub alive for the fixture's lifetime (the
        // method stubs store raw views).
        paramStub_ = std::move(param);

        // The outer function: `... proxyCall(arg) ...`.
        fn = std::make_unique<IL::ILFunction>();
        fn->Kind = IL::ILFunctionKind::TopLevelFunction;
        fn->Method = containingMethod.get();
        auto container = std::make_unique<IL::BlockContainer>();
        auto entryBlock = std::make_unique<IL::Block>();
        entry = entryBlock.get();
        container->AddBlock(std::move(entryBlock));
        fn->Body = std::move(container);
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;

        outerArgVar = MakeLocal("arg");
        fn->Variables.push_back(outerArgVar);
        auto call = std::make_unique<IL::Call>(proxyMethod);
        call->AddArg(std::make_unique<IL::LdLoc>(outerArgVar));
        call->ILStackWasEmpty = true;
        proxyCall = call.get();
        entry->Add(std::move(call));
    }

    // The decode hook's body: the proxy's method body -- a single-block
    // container whose one instruction is `leave(call Target(ldloc this,
    // ldloc p))` (the pass-through the early transform list leaves intact:
    // the parameter loads are not inlinable).
    std::unique_ptr<IL::ILFunction> MakeProxyBody() {
        decodeHit = true;
        auto proxyFn = std::make_unique<IL::ILFunction>();
        proxyFn->Kind = IL::ILFunctionKind::TopLevelFunction;
        auto container = std::make_unique<IL::BlockContainer>();
        auto block = std::make_unique<IL::Block>();
        auto thisVar = MakeParameter("this", -1);
        auto paramVar = MakeParameter("p", 0);
        proxyFn->Variables.push_back(thisVar);
        proxyFn->Variables.push_back(paramVar);
        auto target = std::make_unique<IL::Call>(targetMethod);
        target->AddArg(std::make_unique<IL::LdLoc>(thisVar));
        target->AddArg(std::make_unique<IL::LdLoc>(paramVar));
        auto leave = std::make_unique<IL::Leave>(
            container.get(), std::move(target));
        block->Add(std::move(leave));
        container->AddBlock(std::move(block));
        proxyFn->Body = std::move(container);
        proxyFn->Body->Parent = proxyFn.get();
        proxyFn->Body->ChildIndex = 0;
        return proxyFn;
    }
};

} // namespace

// The core rewrite: a compiler-generated pass-through proxy call is replaced
// with the target call carrying the proxy's substituted method, the copied
// flags, and the original call-site arguments.
TEST(ProxyCallReplacer, ReplacesCompilerGeneratedPassThrough) {
    RunFixture fixture;
    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;  // AsyncAwait defaults true
    ctx.DelegateBodyResolver =
        [&fixture](std::uint32_t token,
                   std::uint32_t rva) -> std::unique_ptr<IL::ILFunction> {
            EXPECT_EQ(token, 0x06000042u);
            (void)rva;
            return fixture.MakeProxyBody();
        };

    IL::ProxyCallReplacer transform;
    transform.Run(*fixture.fn, ctx);

    EXPECT_TRUE(fixture.decodeHit) << "the proxy body was decoded";
    // The proxy call was replaced with the target call.
    ASSERT_EQ(fixture.entry->Instructions.size(), 1u);
    auto* newCall =
        dynamic_cast<IL::Call*>(fixture.entry->Instructions[0].get());
    ASSERT_NE(newCall, nullptr) << "the proxy call was replaced with a Call";
    EXPECT_EQ(newCall->Method.get(), fixture.targetMethod.get());
    // The call-site arguments moved into the new call.
    ASSERT_EQ(newCall->Arguments.size(), 1u);
    auto* ldloc = dynamic_cast<IL::LdLoc*>(newCall->Arguments[0].get());
    ASSERT_NE(ldloc, nullptr);
    EXPECT_EQ(ldloc->Variable.get(), fixture.outerArgVar.get());
    // The ILStackWasEmpty flag is copied from the proxy call.
    EXPECT_TRUE(newCall->ILStackWasEmpty);
}

// The compiler-generated gate: a proxy call to a method without the
// [CompilerGenerated] attribute stays untouched (no decode either).
TEST(ProxyCallReplacer, LeavesNonCompilerGeneratedAlone) {
    RunFixture fixture;
    fixture.proxyMethod = std::make_shared<ProxyMethodStub>(
        "PlainHelper", 0x06000044u, fixture.hostType.get(),
        /*compilerGenerated=*/false, /*isStatic=*/false,
        /*parameterCount=*/1);
    fixture.proxyMethod->SetReturnType(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32));
    // Rewire the call site to the plain helper.
    fixture.entry->Instructions.clear();
    auto call = std::make_unique<IL::Call>(fixture.proxyMethod);
    call->AddArg(std::make_unique<IL::LdLoc>(fixture.outerArgVar));
    fixture.proxyCall = call.get();
    fixture.entry->Add(std::move(call));

    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    ctx.CSharpSettings = &settings;
    ctx.DelegateBodyResolver =
        [&fixture](std::uint32_t, std::uint32_t)
            -> std::unique_ptr<IL::ILFunction> {
            ADD_FAILURE() << "a non-generated proxy must not be decoded";
            return nullptr;
        };

    IL::ProxyCallReplacer transform;
    transform.Run(*fixture.fn, ctx);

    EXPECT_FALSE(fixture.decodeHit);
    ASSERT_EQ(fixture.entry->Instructions.size(), 1u);
    auto* callAfter =
        dynamic_cast<IL::Call*>(fixture.entry->Instructions[0].get());
    ASSERT_NE(callAfter, nullptr);
    EXPECT_EQ(callAfter->Method.get(), fixture.proxyMethod.get());
}

// The settings gate: AsyncAwait disabled leaves the proxy call untouched.
TEST(ProxyCallReplacer, SettingsGateBlocksWhenAsyncAwaitDisabled) {
    RunFixture fixture;
    IL::ILTransformContext ctx;
    ::ILSpy::Decompiler::DecompilerSettings settings;
    settings.SetAsyncAwait(false);
    ctx.CSharpSettings = &settings;
    ctx.DelegateBodyResolver =
        [&fixture](std::uint32_t, std::uint32_t)
            -> std::unique_ptr<IL::ILFunction> {
            ADD_FAILURE() << "a disabled transform must not decode";
            return nullptr;
        };

    IL::ProxyCallReplacer transform;
    transform.Run(*fixture.fn, ctx);

    EXPECT_FALSE(fixture.decodeHit);
    ASSERT_EQ(fixture.entry->Instructions.size(), 1u);
    auto* callAfter =
        dynamic_cast<IL::Call*>(fixture.entry->Instructions[0].get());
    ASSERT_NE(callAfter, nullptr);
    EXPECT_EQ(callAfter->Method.get(), fixture.proxyMethod.get());
}
