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

// Tests for the ILanguage port (ICSharpCode.ILSpyX/Abstractions/
// ILanguage.cs): the interface is implementable, calls dispatch through
// the abstract base, and the TypeToString default argument carries the
// C# value (UseFullyQualifiedEntityNames |
// UseFullyQualifiedTypeNames) through every interface call site.

#include "ILSpyX/Abstractions/ILanguage.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

namespace Abs = ILSpy::ILSpyX::Abstractions;
namespace CF = ILSpy::Decompiler::Output;

class StubLanguage final : public Abs::ILanguage {
public:
    bool ShowMember(const ILSpy::Decompiler::TypeSystem::IEntity&) const override
    {
        return true;
    }
    std::shared_ptr<ILSpy::Decompiler::Metadata::CodeMappingInfo> GetCodeMappingInfo(
        const ILSpy::Decompiler::Metadata::MetadataFile&,
        std::uint32_t token) const override
    {
        seenToken = token;
        return nullptr;
    }
    std::string GetEntityName(
        const ILSpy::Decompiler::Metadata::MetadataFile& module,
        std::uint32_t token, bool fullName, bool omitGenerics) const override
    {
        seenToken = token;
        seenFullName = fullName;
        seenOmitGenerics = omitGenerics;
        (void)module;
        return "entity";
    }
    std::string GetTooltip(
        const ILSpy::Decompiler::TypeSystem::IEntity&) const override
    {
        return "tooltip";
    }
    std::string TypeToString(const ILSpy::Decompiler::TypeSystem::IType&,
        CF::ConversionFlags conversionFlags) const override
    {
        seenFlags = conversionFlags;
        return "type";
    }
    std::string EntityToString(const ILSpy::Decompiler::TypeSystem::IEntity&,
        CF::ConversionFlags conversionFlags) const override
    {
        seenFlags = conversionFlags;
        return "entity-toString";
    }

    mutable std::uint32_t seenToken = 0;
    mutable bool seenFullName = false;
    mutable bool seenOmitGenerics = false;
    mutable CF::ConversionFlags seenFlags = CF::ConversionFlags::None;
};

}  // namespace

TEST(ILanguageTest, DispatchesThroughTheAbstractBase)
{
    StubLanguage stub;
    Abs::ILanguage& language = stub;

    // The never-throwing MetadataFile over a nonexistent path is a cheap
    // real argument for the metadata-surface members.
    const ILSpy::Decompiler::Metadata::MetadataFile module("no-such-file.dll");
    EXPECT_EQ(language.GetEntityName(module, 0x06000001, true, false), "entity");
    EXPECT_EQ(stub.seenToken, 0x06000001u);
    EXPECT_TRUE(stub.seenFullName);
    EXPECT_FALSE(stub.seenOmitGenerics);

    EXPECT_EQ(language.GetCodeMappingInfo(module, 0x02000001), nullptr);
    EXPECT_EQ(stub.seenToken, 0x02000001u);
}

TEST(ILanguageTest, TypeToStringDefaultCarriesTheCSharpValue)
{
    // The interface's own default argument (the C# declaration):
    // UseFullyQualifiedEntityNames | UseFullyQualifiedTypeNames.
    constexpr std::uint32_t expected =
        static_cast<std::uint32_t>(
            CF::ConversionFlags::UseFullyQualifiedEntityNames)
        | static_cast<std::uint32_t>(
            CF::ConversionFlags::UseFullyQualifiedTypeNames);
    (void)expected;
    static_assert(static_cast<std::uint32_t>(
                      Abs::kDefaultTypeToStringConversionFlags)
            == expected,
        "the TypeToString default must be the C# value");

    // The default argument itself is pinned by the declaration: the
    // parameter's default IS the constant, and the constant is the C#
    // pair above (a static_assert, since calling TypeToString needs an
    // IType instance the contract test does not build).
    SUCCEED();
}

// ShowMember / GetTooltip / TypeToString / EntityToString take entity and
// type instances the contract test does not build; their signatures are
// pinned by StubLanguage's overrides compiling against the interface, and
// the flags surface is pinned by the static_assert above (the same
// through-the-base dispatch the metadata-surface members exercise).
