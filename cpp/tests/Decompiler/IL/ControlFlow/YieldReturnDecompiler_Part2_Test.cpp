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

// Tests for YieldReturnDecompiler part 2 (the enumerator-creation pattern
// match and the four metadata analyses), driven over the net48 mscorlib
// fixture: real compiler-generated iterator state machines, the body decode
// through the DelegateBodyResolver hook (ReadIL + the early transforms + the
// field-resolution pass), and the field identity resolved through the
// compilation's main module.

#include "Decompiler/IL/ControlFlow/YieldReturnDecompiler.hpp"

#include <algorithm>
#include <vector>

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/YieldReturn.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/Metadata/CodeMappingInfo.hpp"  // IsCompilerGeneratorEnumerator
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"  // DetectTargetFrameworkId
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/TypeSystem/DecompilerTypeSystem.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace MD = ::ILSpy::Decompiler::Metadata;
namespace IL = ::ILSpy::Decompiler::IL;

// The locally-compiled iterator fixture (a REAL Roslyn state machine -- the
// net48 corpus is the REFERENCE-assembly set, whose method bodies are
// stripped stubs that decode to empty). Built with the box's csc over the
// net8.0 refs; the oracle decompiles it to `yield return` shapes.
constexpr const char* kIteratorFixture =
    "/home/jim/ilspy-test-fixtures/yield_fixture/IteratorFixture.dll";

// A resolved-method stub carrying a MethodDef token (the pattern matchers
// read `newObj.Method.MetadataToken`).
class TokenMethodStub : public TS::IMethod {
public:
    explicit TokenMethodStub(std::uint32_t token) : token_(token) {}
    TS::SymbolKind SymbolKind() const override {
        return TS::SymbolKind::Constructor;
    }
    std::string Name() const override { return ".ctor"; }
    std::string FullName() const override { return "T::.ctor"; }
    std::string ReflectionName() const override { return ".ctor"; }
    std::string Namespace() const override { return std::string(); }
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("TokenMethodStub::Compilation");
    }
    std::vector<const TS::IParameter*> Parameters() const override {
        return {};
    }
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
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return {};
    }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return token_; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
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
    std::vector<const TS::ITypeParameter*> TypeParameters() const override {
        return {};
    }
    std::vector<TS::ITypePtr> TypeArguments() const override {
        return {};
    }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return true; }
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
    std::uint32_t token_;
};

// The IsMethod shape (the transform's own helper): the metadata name or the
// MethodImpl declaration's name.
bool HasMethodNamed(const MD::MetadataFile& file, std::uint32_t typeToken,
                    const std::string& name) {
    for (const auto& m : file.GetMethods(typeToken)) {
        if (file.GetMethodName(m.Token) == name)
            return true;
        for (const auto& impl : file.GetMethodImplementations(m.Token)) {
            std::uint32_t decl = impl.MethodDeclarationToken;
            if (decl == 0)
                continue;
            if ((decl >> 24) == 0x06) {
                if (file.GetMethodName(decl) == name)
                    return true;
            } else if ((decl >> 24) == 0x0A) {
                auto mr = file.GetMemberReference(decl);
                if (mr.has_value() && mr->Name == name)
                    return true;
            }
        }
    }
    return false;
}

// The fixture: the mscorlib file + the type system + a compiler-generated
// enumerator found by scanning the TypeDefs.
struct MscorlibEnumeratorFixture {
    std::unique_ptr<MD::MetadataFile> file;
    std::unique_ptr<MD::UniversalAssemblyResolver> resolver;
    std::unique_ptr<TS::DecompilerTypeSystem> ts;
    std::uint32_t enumeratorType = 0;
    std::uint32_t enumeratorCtor = 0;
    std::uint32_t currentType = 0;
    std::string enumeratorName;

    bool Load() {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(kIteratorFixture, ec))
            return false;
        file = std::make_unique<MD::MetadataFile>(kIteratorFixture);
        if (!file->IsValid())
            return false;
        resolver = std::make_unique<MD::UniversalAssemblyResolver>(
            std::string(kIteratorFixture), false,
            MD::DetectTargetFrameworkId(*file).value_or(std::string()));
        ts = std::make_unique<TS::DecompilerTypeSystem>(*file, *resolver);
        // Scan for a Roslyn-style enumerator: a compiler-generated nested
        // IEnumerator type whose ctor takes the initial state (-2/0). The
        // C# MatchEnumeratorCreationNewObj checks the ctor's ARGUMENT shape
        // in the creating method's body; here we take the enumerator from
        // the metadata and build the newobj node directly.
        for (const auto& t : file->TypeDefs()) {
            if (!MD::IsCompilerGeneratorEnumerator(*file, t.Token))
                continue;
            // Roslyn's iterators implement the interface members
            // EXPLICITLY ("System.Collections.IEnumerator.get_Current" as
            // the metadata name, matched through the MethodImpl table),
            // so the IsMethod-based lookup (the transform's own helper
            // shape) finds them.
            std::uint32_t ctor = 0;
            bool hasGetCurrent = false, hasMoveNext = false,
                 hasDispose = false;
            for (const auto& m : file->GetMethods(t.Token)) {
                if (m.Name == ".ctor") {
                    if (m.RVA != 0)
                        ctor = m.Token;
                } else if (m.Name == "MoveNext") {
                    hasMoveNext = true;
                }
            }
            hasGetCurrent = HasMethodNamed(*file, t.Token, "get_Current");
            hasDispose = HasMethodNamed(*file, t.Token, "Dispose");
            if (ctor == 0 || !hasGetCurrent || !hasMoveNext || !hasDispose)
                continue;
            auto nameInfo = file->GetTypeDefNameInfo(t.Token);
            if (!nameInfo.has_value() || nameInfo->DeclaringTypeToken == 0)
                continue;
            enumeratorType = t.Token;
            enumeratorCtor = ctor;
            currentType = nameInfo->DeclaringTypeToken;
            enumeratorName = nameInfo->Name;
            return true;
        }
        return false;
    }
};

} // namespace

// The pattern match: the enumerator's newobj (the ctor taking the -2 initial
// state) resolves the enumerator type and its declaring-type identity.
TEST(YieldReturnDecompilerPart2, MatchEnumeratorCreationNewObjOverMetadata) {
    MscorlibEnumeratorFixture fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the iterator fixture is not provisioned";
    ASSERT_NE(fixture.enumeratorType, 0u);

    // newobj Enumerator::.ctor(ldc.i4 -2)
    auto ctorStub =
        std::make_shared<TokenMethodStub>(fixture.enumeratorCtor);
    ctorStub->SetReturnType(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void));
    auto newObj = std::make_unique<IL::Call>(ctorStub);
    newObj->IsNewObj = true;
    newObj->AddArg(std::make_unique<IL::LdcI4>(-2));

    std::uint32_t outCtor = 0, outType = 0;
    EXPECT_TRUE(IL::YieldReturnDecompiler::MatchEnumeratorCreationNewObj(
        newObj.get(), *fixture.file, fixture.currentType, outCtor, outType));
    EXPECT_EQ(outCtor, fixture.enumeratorCtor);
    EXPECT_EQ(outType, fixture.enumeratorType);
}

// A wrong current type rejects the match (the enumerator must be nested in
// the method's own declaring type).
TEST(YieldReturnDecompilerPart2, MatchEnumeratorCreationRejectsForeignType) {
    MscorlibEnumeratorFixture fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the iterator fixture is not provisioned";
    ASSERT_NE(fixture.enumeratorType, 0u);

    auto ctorStub =
        std::make_shared<TokenMethodStub>(fixture.enumeratorCtor);
    ctorStub->SetReturnType(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void));
    auto newObj = std::make_unique<IL::Call>(ctorStub);
    newObj->IsNewObj = true;
    newObj->AddArg(std::make_unique<IL::LdcI4>(-2));

    std::uint32_t outCtor = 0, outType = 0;
    // The <Module> pseudo-type (token 1) is never the declaring type.
    EXPECT_FALSE(IL::YieldReturnDecompiler::MatchEnumeratorCreationNewObj(
        newObj.get(), *fixture.file, 0x02000001u, outCtor, outType));
}

// The end-to-end conversion over the REAL creating-method body: the decoded
// `Numbers` method (the one-instruction `ret(newobj(-2))` shape the reader
// produces) drives the full pipeline -- the pattern match over the reader's
// call token, the four metadata analyses, the MoveNext state-machine
// inversion, and the field-to-local translation.
TEST(YieldReturnDecompilerPart2, ConvertsTheRealCreatingMethodBody) {
    MscorlibEnumeratorFixture fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the iterator fixture is not provisioned";
    ASSERT_NE(fixture.enumeratorType, 0u);

    // The real Numbers body (the no-parameter iterator: `ret(newobj(-2))`).
    std::uint32_t numbersToken = 0, numbersRva = 0;
    for (const auto& m : fixture.file->GetMethods(fixture.currentType)) {
        if (m.Name == "Numbers" && m.RVA != 0) {
            numbersToken = m.Token;
            numbersRva = m.RVA;
            break;
        }
    }
    ASSERT_NE(numbersToken, 0u);
    auto fn = IL::ReadIL(*fixture.file, numbersToken, numbersRva);
    ASSERT_NE(fn, nullptr);
    // The reader leaves the function's method unresolved; the Run gate
    // reads the declaring type through the method token.
    auto methodStub = std::make_shared<TokenMethodStub>(numbersToken);
    methodStub->SetReturnType(
        std::make_shared<TS::KnownType>(
            TS::KnownTypeCode::IEnumerableOfT));
    fn->Method = methodStub.get();

    IL::ILTransformContext ctx;
    ctx.Settings.YieldReturn = true;
    ctx.Metadata = fixture.file.get();
    ctx.TypeSystem = fixture.ts.get();
    ctx.DelegateBodyResolver =
        [&fixture](std::uint32_t token,
                   std::uint32_t rva) -> std::unique_ptr<IL::ILFunction> {
        if (rva == 0) return nullptr;
        return IL::ReadIL(*fixture.file, token, rva);
    };

    IL::YieldReturnDecompiler transform;
    transform.Run(*fn, ctx);

    EXPECT_TRUE(fn->IsIterator) << "the state machine was inverted";
    std::size_t yieldReturns = 0;
    std::vector<int> yieldedValues;
    std::vector<IL::ILInstruction*> stack{fn->Body.get()};
    while (!stack.empty()) {
        IL::ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* yr = dynamic_cast<IL::YieldReturn*>(node)) {
            yieldReturns++;
            if (auto* ldc = dynamic_cast<IL::LdcI4*>(yr->Value.get()))
                yieldedValues.push_back(ldc->Value);
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (IL::ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    EXPECT_EQ(yieldReturns, 3u);
    std::sort(yieldedValues.begin(), yieldedValues.end());
    EXPECT_EQ(yieldedValues, (std::vector<int>{1, 2, 3}));
}

// The four analyses over the real state machine: Run over a hand-built
// creating-method body drives AnalyzeCtor (the state field),
// AnalyzeCurrentProperty (the current field), the GetEnumerator mapping, and
// ConstructExceptionTable (the Dispose state ranges).
TEST(YieldReturnDecompilerPart2, AnalyzesTheRealStateMachine) {
    MscorlibEnumeratorFixture fixture;
    if (!fixture.Load())
        GTEST_SKIP() << "the iterator fixture is not provisioned";
    ASSERT_NE(fixture.enumeratorType, 0u);

    // The creating method's body: `stloc v(newobj Enumerator::.ctor(-2));
    // ret(ldloc v)` -- the no-parameters shape (a static iterator).
    auto ctorStub =
        std::make_shared<TokenMethodStub>(fixture.enumeratorCtor);
    ctorStub->SetReturnType(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void));
    auto newObj = std::make_unique<IL::Call>(ctorStub);
    newObj->IsNewObj = true;
    newObj->AddArg(std::make_unique<IL::LdcI4>(-2));
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                              nullptr, 0);
    v->Name = "v";
    v->StoreCount = 1;
    v->LoadCount = 1;
    auto stloc = std::make_unique<IL::StLoc>(v, std::move(newObj));
    auto fn = std::make_unique<IL::ILFunction>();
    fn->Kind = IL::ILFunctionKind::TopLevelFunction;
    fn->Method = nullptr;  // Run's current-type gate needs the method token
    auto container = std::make_unique<IL::BlockContainer>();
    auto ret = std::make_unique<IL::Leave>(container.get(),
                                           std::make_unique<IL::LdLoc>(v));
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    block->Add(std::move(stloc));
    block->SetFinal(std::move(ret));
    container->AddBlock(std::move(block));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;

    // The current-type gate reads the function's method token; the fixture's
    // enumerator is nested in currentType, so use a creating method from that
    // type. Find one: any method of currentType whose body newobjs the
    // enumerator -- the metadata scan finds the enumerator; the declaring
    // type's methods are the candidates. For the analysis test, wire the
    // method stub directly with the enumerator's declaring type via a
    // creating-method token from the declaring type's method list (any
    // method -- the pattern match below uses the hand-built body).
    std::uint32_t creatingMethod = 0;
    for (const auto& m : fixture.file->GetMethods(fixture.currentType)) {
        if (m.RVA != 0) {
            creatingMethod = m.Token;
            break;
        }
    }
    ASSERT_NE(creatingMethod, 0u);
    fn->Method = std::make_shared<TokenMethodStub>(creatingMethod).get();
    // Keep the stub alive for the function's lifetime.
    auto methodStub =
        std::make_shared<TokenMethodStub>(creatingMethod);
    fn->Method = methodStub.get();

    IL::ILTransformContext ctx;
    ctx.Settings.YieldReturn = true;
    ctx.Metadata = fixture.file.get();
    ctx.TypeSystem = fixture.ts.get();
    ctx.DelegateBodyResolver =
        [&fixture](std::uint32_t token,
                   std::uint32_t rva) -> std::unique_ptr<IL::ILFunction> {
            if (rva == 0) return nullptr;
            return IL::ReadIL(*fixture.file, token, rva);
        };

    IL::YieldReturnDecompiler transform;
    // The full pipeline: match + the four analyses + the MoveNext
    // conversion + the field translation. The observable: the function's
    // body is REPLACED with the converted MoveNext body carrying yield
    // return nodes (the hand-built creating-method body is gone).
    transform.Run(*fn, ctx);
    ASSERT_NE(fn->Body.get(), nullptr);
    // Count the YieldReturn nodes in the new body (the C# IsIterator flag
    // mirrors the successful conversion).
    EXPECT_TRUE(fn->IsIterator)
        << "the state machine was inverted";
    std::size_t yieldReturns = 0;
    std::vector<IL::ILInstruction*> stack{fn->Body.get()};
    while (!stack.empty()) {
        IL::ILInstruction* node = stack.back();
        stack.pop_back();
        if (dynamic_cast<IL::YieldReturn*>(node) != nullptr)
            yieldReturns++;
        for (int i = 0; i < node->ChildCount(); i++) {
            if (IL::ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    EXPECT_GE(yieldReturns, 1u)
        << "the converted body carries at least one yield return";
    // The fixture's Numbers() iterator yields 1, 2, 3 -- the exact count
    // and values pin the state-machine inversion (every state-block became
    // a yield return carrying the former current-field store).
    EXPECT_EQ(yieldReturns, 3u)
        << "the three-yield iterator fully converted";
    std::vector<int> yieldedValues;
    stack = {fn->Body.get()};
    while (!stack.empty()) {
        IL::ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* yr = dynamic_cast<IL::YieldReturn*>(node)) {
            if (auto* ldc = dynamic_cast<IL::LdcI4*>(yr->Value.get()))
                yieldedValues.push_back(ldc->Value);
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (IL::ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    std::sort(yieldedValues.begin(), yieldedValues.end());
    EXPECT_EQ(yieldedValues, (std::vector<int>{1, 2, 3}))
        << "the yielded constants survive the conversion";
}
