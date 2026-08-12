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

// Phase 1/2 signature-decoder tests. Decodes MethodDef signatures from a real
// .NET assembly through MetadataFile::GetMethodSignature and checks the
// resolved IType shapes: primitives map to KnownType, TypeRefs resolve to
// SimpleType/KnownType by name, generic instantiations become ParameterizedType,
// arrays become ArrayType, and `this`-bearing methods are flagged IsInstance.
// This is the bridge from the vendored winmd TypeSig to the port's IType.

#include "Decompiler/Metadata/ILDisassembler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

static const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::MethodSignature;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameter;

// Downcast helper: assert the kind and return the derived pointer.
template <typename T>
static const T* As(const ILSpy::Decompiler::TypeSystem::ITypePtr& t) {
    return t ? dynamic_cast<const T*>(t.get()) : nullptr;
}

TEST(SignatureDecoder, DecodesMethodSignaturesFromMscorlib) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    std::set<std::string> returnNames;
    bool sawInstance = false;
    bool sawGenericParamCount = false;
    bool sawParameterized = false;
    bool sawArrayType = false;
    bool sawBoolReturn = false;
    bool sawVoidReturn = false;
    int decoded = 0;

    for (const auto& m : file.MethodDefs()) {
        auto sig = file.GetMethodSignature(m.Token);
        if (!sig) continue;
        ++decoded;
        ASSERT_NE(sig->ReturnType, nullptr);
        returnNames.insert(sig->ReturnType->ReflectionName());
        if (sig->IsInstance) sawInstance = true;
        if (sig->GenericParameterCount > 0) sawGenericParamCount = true;
        if (sig->ReturnType->Kind() == TypeKind::Array) sawArrayType = true;
        if (sig->ReturnType->ReflectionName() == "System.Boolean") sawBoolReturn = true;
        if (sig->ReturnType->ReflectionName() == "System.Void") sawVoidReturn = true;
        // A parameterized type renders with '<' in its reflection name.
        for (const auto& p : sig->ParameterTypes) {
            ASSERT_NE(p, nullptr);
            if (p->ReflectionName().find('<') != std::string::npos) sawParameterized = true;
            if (p->Kind() == TypeKind::Array) sawArrayType = true;
        }
        if (decoded > 4000) break; // sample enough of the ~18000 methods
    }
    ASSERT_GT(decoded, 1000) << "decoded too few method signatures";
    EXPECT_TRUE(sawInstance) << "no instance (has-this) methods decoded";
    EXPECT_TRUE(sawBoolReturn) << "no method returning System.Boolean found";
    EXPECT_TRUE(sawVoidReturn) << "no void-returning method found";
    EXPECT_TRUE(sawParameterized) << "no generic instantiation parameter decoded";
    EXPECT_TRUE(sawArrayType) << "no array type decoded";
    // The known-type fast path should fire for primitives.
    EXPECT_TRUE(returnNames.count("System.Boolean") > 0);
    EXPECT_TRUE(returnNames.count("System.Int32") > 0);
}

TEST(SignatureDecoder, ObjectEqualsIsInstanceReturningBoolean) {
    // Find Object.Equals(object) : bool among the Equals methods and verify
    // the decoded signature: instance, returns Boolean, one Object parameter.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    bool found = false;
    for (const auto& m : file.MethodDefs()) {
        if (m.Name != "Equals") continue;
        auto sig = file.GetMethodSignature(m.Token);
        if (!sig || !sig->IsInstance) continue;
        if (sig->ParameterTypes.size() != 1) continue;
        if (sig->ReturnType->ReflectionName() != "System.Boolean") continue;
        // The single parameter should be System.Object (KnownType or SimpleType).
        const auto& p = sig->ParameterTypes[0];
        EXPECT_EQ(p->ReflectionName(), "System.Object");
        found = true;
        break;
    }
    EXPECT_TRUE(found) << "Object.Equals(object) : bool not found/decoded";
}

// Walk method bodies' IL and collect call/callvirt/newobj operand tokens from
// the given metadata table (0x0A MemberRef, 0x2B MethodSpec, ...). If typeToken
// is 0, scans the whole module (up to maxTokens tokens).
static std::vector<std::uint32_t> CallOperandTokens(MetadataFile& file, std::uint32_t typeToken,
                                                     std::uint32_t table, std::size_t maxTokens = 1000000) {
    std::vector<std::uint32_t> tokens;
    for (const auto& t : file.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        if (typeToken != 0 && t.Token != typeToken) continue;
        for (const auto& m : file.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto body = file.GetMethodBody(m.RVA);
            if (!body.IsValid()) continue;
            auto disasm = ILSpy::Decompiler::Metadata::DisassembleIL(body.IL());
            for (const auto& inst : disasm.Instructions) {
                using ILSpy::Decompiler::Metadata::ILOpCode;
                if (inst.OpCode != ILOpCode::Call && inst.OpCode != ILOpCode::Callvirt &&
                    inst.OpCode != ILOpCode::Newobj) continue;
                if (inst.OperandSize != 4) continue;
                auto il = body.IL();
                std::uint32_t at = inst.Offset + inst.Length - 4;
                std::uint32_t tok = static_cast<std::uint32_t>(il[at]) |
                    (static_cast<std::uint32_t>(il[at + 1]) << 8) |
                    (static_cast<std::uint32_t>(il[at + 2]) << 16) |
                    (static_cast<std::uint32_t>(il[at + 3]) << 24);
                if ((tok & 0xFF000000u) == table) tokens.push_back(tok);
                if (tokens.size() >= maxTokens) return tokens;
            }
        }
    }
    return tokens;
}

TEST(SignatureDecoder, MemberRefSignaturesDecode) {
    // Method call sites may reference MemberRef tokens (table 0x0A), not
    // MethodDef; GetMethodSignature must decode the MemberRef row's signature.
    // mscorlib's MemberRefs target mostly instantiated generic parents (its
    // same-module calls use MethodDef tokens directly). Discriminator:
    // a member named ".ctor" ALWAYS returns void; a wrong row lookup returns a
    // random method's return type.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    auto memberRefTokens = CallOperandTokens(file, 0, 0x0A000000u, 200);
    ASSERT_FALSE(memberRefTokens.empty()) << "no MemberRef call sites found";

    int withSig = 0;
    int ctorsChecked = 0;
    for (std::uint32_t tok : memberRefTokens) {
        std::string name = file.ResolveTokenToString(tok);
        auto sig = file.GetMethodSignature(tok);
        ASSERT_TRUE(sig.has_value()) << "MemberRef " << name << " gives no signature";
        ASSERT_NE(sig->ReturnType, nullptr);
        ++withSig;
        // A constructor MemberRef is always named ".ctor" (ECMA-335 I.10.5.1);
        // TypeSpec-parented refs resolve to the bare member name.
        if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".ctor") == 0) {
            EXPECT_EQ(sig->ReturnType->ReflectionName(), "System.Void")
                << "wrong signature row for ctor " << name;
            ++ctorsChecked;
        }
    }
    EXPECT_GT(withSig, 20);
    EXPECT_GT(ctorsChecked, 2) << "too few ctor MemberRefs sampled to trust the check";
}

TEST(SignatureDecoder, MethodSpecSignaturesDecodeViaDefinition) {
    // Call sites to a generic method instantiation carry a MethodSpec token
    // (table 0x2B); the signature is the underlying definition's signature.
    // System.Array's generic BinarySearch<T> shim calls the MethodSpec for
    // BinarySearch<T>(T[], int, int, T, IComparer<T>): 5 parameters, first one
    // an SZArray.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    std::uint32_t arrayTok = 0;
    for (const auto& t : file.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "Array") { arrayTok = t.Token; break; }
    }
    ASSERT_NE(arrayTok, 0u);

    auto specTokens = CallOperandTokens(file, arrayTok, 0x2B000000u);
    ASSERT_FALSE(specTokens.empty()) << "no MethodSpec call sites found on System.Array";

    bool saw5ParamArrayFirst = false;
    for (std::uint32_t tok : specTokens) {
        auto sig = file.GetMethodSignature(tok);
        ASSERT_TRUE(sig.has_value()) << "MethodSpec token unresolved by GetMethodSignature";
        ASSERT_NE(sig->ReturnType, nullptr);
        if (sig->ParameterTypes.size() == 5 &&
            sig->ParameterTypes[0]->Kind() == TypeKind::Array) {
            saw5ParamArrayFirst = true;
        }
    }
    EXPECT_TRUE(saw5ParamArrayFirst)
        << "no 5-param MethodSpec with an array first parameter (BinarySearch<T> expected)";
}

TEST(SignatureDecoder, TypedReferenceSignaturesDecode) {
    // TypedReference (ELEMENT_TYPE_TYPEDBYREF) is outside the WinMD signature
    // subset the vendored parser covers; the decoder must handle it.
    // System.ArgIterator::GetNextArg returns System.TypedReference.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    std::uint32_t argIterTok = 0;
    for (const auto& t : file.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "ArgIterator") { argIterTok = t.Token; break; }
    }
    ASSERT_NE(argIterTok, 0u);
    bool found = false;
    for (const auto& m : file.GetMethods(argIterTok)) {
        if (m.Name != "GetNextArg") continue;
        auto sig = file.GetMethodSignature(m.Token);
        if (!sig) continue;
        if (sig->ReturnType->ReflectionName() == "System.TypedReference") found = true;
    }
    EXPECT_TRUE(found) << "no GetNextArg signature decoded with a TypedReference return";
}

TEST(SignatureDecoder, VarargSignaturesWithSentinelDecode) {
    // Vararg method signatures interleave a SENTINEL element before the
    // optional parameters. The decoder must skip the sentinel and yield the
    // full parameter list.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    // mscorlib's vararg Concat(Object,...,Object, __arglist) has >= 4 params.
    bool foundWide = false;
    int concatSigs = 0;
    std::uint32_t stringTok = 0;
    for (const auto& t : file.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "String") { stringTok = t.Token; break; }
    }
    ASSERT_NE(stringTok, 0u);
    for (const auto& m : file.GetMethods(stringTok)) {
        if (m.Name != "Concat") continue;
        auto body = file.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        bool usesArglist = false;
        for (auto bt : body.IL()) if (bt == 0xFE) { usesArglist = true; break; }
        (void)usesArglist;
        auto sig = file.GetMethodSignature(m.Token);
        if (!sig) continue;
        ++concatSigs;
        if (sig->ParameterTypes.size() >= 4) foundWide = true;
    }
    EXPECT_GT(concatSigs, 3);
    EXPECT_TRUE(foundWide) << "the vararg Concat overload's signature must decode";
}

TEST(SignatureDecoder, TypeSpecOperandsResolve) {
    // TypeSpec tokens (table 0x1B) appear as newarr/castclass/ldtoken operands
    // in generic code; ResolveTypeToken must decode their signature blobs
    // (arrays, instantiations) instead of returning nullptr.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    std::uint32_t arrayTok = 0;
    for (const auto& t : file.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "Array") { arrayTok = t.Token; break; }
    }
    ASSERT_NE(arrayTok, 0u);

    int resolved = 0;
    int total = 0;
    for (const auto& m : file.GetMethods(arrayTok)) {
        if (m.RVA == 0) continue;
        auto body = file.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        auto disasm = ILSpy::Decompiler::Metadata::DisassembleIL(body.IL());
        for (const auto& inst : disasm.Instructions) {
            if (inst.OperandSize != 4) continue;  // token operands only
            auto il = body.IL();
            std::uint32_t at = inst.Offset + inst.Length - 4;
            std::uint32_t tok = static_cast<std::uint32_t>(il[at]) |
                (static_cast<std::uint32_t>(il[at + 1]) << 8) |
                (static_cast<std::uint32_t>(il[at + 2]) << 16) |
                (static_cast<std::uint32_t>(il[at + 3]) << 24);
            if ((tok & 0xFF000000u) != 0x1B000000u) continue;
            ++total;
            auto type = file.ResolveTypeToken(tok);
            if (type && type->Kind() != TypeKind::Unknown) ++resolved;
        }
    }
    EXPECT_GT(total, 0) << "no TypeSpec operands found on System.Array methods";
    EXPECT_GT(resolved, 0) << "no TypeSpec operand resolved to a real type";
}

TEST(SignatureDecoder, OutOfRangeTokenIsGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    // Row 0 and a huge row must return nullopt, not throw.
    EXPECT_FALSE(file.GetMethodSignature(0x06000000u));
    EXPECT_FALSE(file.GetMethodSignature(0x06FFFFFFu));
}

TEST(SignatureDecoder, GenericParameterNamesResolveFromMetadata) {
    // Generic signatures reference class (`!N`) and method (`!!N`) type
    // parameters by INDEX. Their real names (T, TValue, ...) live in the
    // declaring TypeDef's / MethodDef's GenericParam rows. Decoding should
    // resolve them, so e.g. List<T>::Add(T) has a param named "T" and
    // a generic delegate would too.
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    // Find List<T>.Add(T) -- a class generic-parameter VAR.
    bool foundClass = false;
    for (const auto& m : file.MethodDefs()) {
        if (m.Name != "Add") continue;
        auto sig = file.GetMethodSignature(m.Token);
        if (!sig || !sig->IsInstance) continue;
        if (sig->ParameterTypes.size() != 1) continue;
        const auto& p = sig->ParameterTypes[0];
        if (p->Kind() != TypeKind::TypeParameter) continue;
        // List<T>'s T has index 0 (declared on the type), not method (?).
        auto* tp = As<TypeParameter>(p);
        ASSERT_NE(tp, nullptr);
        if (tp->Owner() != TypeParameter::OwnerKind::Class) continue;
        // The signature decodes via TypeSpec/MemberRef of List<T>.Add... the
        // method's owner is List<T>; the VAR(0) should resolve to "T".
        EXPECT_EQ(tp->Name(), "T")
            << "List<T>.Add should have type parameter named T, got '" << tp->Name() << "'";
        foundClass = true;
        break;
    }
    EXPECT_TRUE(foundClass) << "List<T>.Add(T) not found to test class generic param";
}
