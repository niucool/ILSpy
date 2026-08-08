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

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>

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

TEST(SignatureDecoder, OutOfRangeTokenIsGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    // Row 0 and a huge row must return nullopt, not throw.
    EXPECT_FALSE(file.GetMethodSignature(0x06000000u));
    EXPECT_FALSE(file.GetMethodSignature(0x06FFFFFFu));
}
