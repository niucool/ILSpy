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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the System.Reflection.Metadata AssemblyNameInfo port (cpp/Decompiler/
// Metadata/AssemblyNameInfo.{hpp,cpp}) -- the display-name parser and the canonical
// FullName render -- pinned against the real .NET 10 classes: every parse
// expectation, flag bit, key byte array and exception message was dumped from
// the .NET 10.0.8 runtime over the identical case matrix (the
// C:\temp-probe\TypeNameProbe public-API probe).

#include "Decompiler/Metadata/AssemblyNameInfo.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using ILSpy::Decompiler::Metadata::AssemblyContentType;
using ILSpy::Decompiler::Metadata::AssemblyNameFlags;
using ILSpy::Decompiler::Metadata::AssemblyNameInfo;
using ILSpy::Decompiler::Metadata::ProcessorArchitecture;
using ILSpy::Decompiler::TypeSystem::Version;

namespace {

std::shared_ptr<AssemblyNameInfo> MustParse(const char* input)
{
    std::shared_ptr<AssemblyNameInfo> result;
    if (!AssemblyNameInfo::TryParse(input, result)) {
        ADD_FAILURE() << "AssemblyNameInfo::TryParse failed for [" << input << "]";
        return nullptr;
    }
    return result;
}

std::string Hex(const std::optional<std::vector<std::uint8_t>>& key)
{
    if (!key.has_value())
        return "<unset>";
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t byte : *key) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0xF]);
    }
    return out;
}

template <typename F>
std::string WhatOf(F&& action)
{
    try {
        action();
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "<non-standard>";
    }
    return "<no-throw>";
}

TEST(AssemblyNameInfoParse, SimpleAndSegmentedForms)
{
    auto bare = MustParse("mscorlib");
    EXPECT_EQ(bare->Name(), "mscorlib");
    EXPECT_FALSE(bare->Version().has_value());
    EXPECT_FALSE(bare->CultureName().has_value());
    EXPECT_EQ(static_cast<std::int32_t>(bare->Flags()),
              static_cast<std::int32_t>(AssemblyNameFlags::None));
    EXPECT_FALSE(bare->PublicKeyOrToken().has_value());
    EXPECT_EQ(bare->FullName(), "mscorlib");

    auto version = MustParse("mscorlib, Version=4.0.0.0");
    ASSERT_TRUE(version->Version().has_value());
    EXPECT_EQ(*version->Version(), Version(4, 0, 0, 0));
    EXPECT_EQ(version->FullName(), "mscorlib, Version=4.0.0.0");

    auto full = MustParse(
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    ASSERT_TRUE(full->CultureName().has_value());
    EXPECT_EQ(*full->CultureName(), "");
    EXPECT_EQ(Hex(full->PublicKeyOrToken()), "b77a5c561934e089");
    EXPECT_EQ(
        full->FullName(),
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");

    // The full-name render is canonical: fixed segment order and canonical
    // spellings regardless of the input order, key casing or spacing.
    auto shuffled = MustParse(
        "x, culture=neutral, publickeytoken=null, version=1.2.3.4");
    EXPECT_EQ(shuffled->FullName(),
              "x, Version=1.2.3.4, Culture=neutral, PublicKeyToken=null");
    auto spaced = MustParse("x, Retargetable=Yes, ContentType=WindowsRuntime, "
                            "ProcessorArchitecture=amd64");
    EXPECT_EQ(static_cast<std::int32_t>(spaced->Flags()), 832);
    EXPECT_EQ(spaced->FullName(),
              "x, Retargetable=Yes, ContentType=WindowsRuntime");
}

TEST(AssemblyNameInfoParse, CultureForms)
{
    auto neutral = MustParse("mscorlib, Culture=neutral");
    ASSERT_TRUE(neutral->CultureName().has_value());
    EXPECT_EQ(*neutral->CultureName(), "");
    EXPECT_EQ(neutral->FullName(), "mscorlib, Culture=neutral");

    // "Neutral" folds case-insensitively to the empty culture.
    auto neutralUpper = MustParse("mscorlib, Culture=Neutral");
    EXPECT_EQ(*neutralUpper->CultureName(), "");

    auto specific = MustParse("mscorlib, Culture=de-DE");
    EXPECT_EQ(*specific->CultureName(), "de-DE");
    EXPECT_EQ(specific->FullName(), "mscorlib, Culture=de-DE");

    // The null-vs-empty distinction: no Culture attribute renders NO segment.
    EXPECT_EQ(MustParse("mscorlib")->FullName(), "mscorlib");
}

TEST(AssemblyNameInfoParse, KeyForms)
{
    auto nullToken = MustParse("mscorlib, PublicKeyToken=null");
    ASSERT_TRUE(nullToken->PublicKeyOrToken().has_value());
    EXPECT_EQ(nullToken->PublicKeyOrToken()->size(), 0u);
    EXPECT_EQ(nullToken->FullName(), "mscorlib, PublicKeyToken=null");

    auto token = MustParse("mscorlib, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(Hex(token->PublicKeyOrToken()), "b77a5c561934e089");
    EXPECT_EQ(static_cast<std::int32_t>(token->Flags()),
              static_cast<std::int32_t>(AssemblyNameFlags::None));

    auto publicKey = MustParse("mscorlib, PublicKey=b77a5c561934e089");
    EXPECT_EQ(static_cast<std::int32_t>(publicKey->Flags()),
              static_cast<std::int32_t>(AssemblyNameFlags::PublicKey));
    EXPECT_EQ(publicKey->FullName(), "mscorlib, PublicKey=b77a5c561934e089");

    auto shortKey = MustParse("mscorlib, PublicKey=0011");
    EXPECT_EQ(Hex(shortKey->PublicKeyOrToken()), "0011");

    // Uppercase hex decodes and re-renders lowercase.
    auto upper = MustParse("mscorlib, PublicKeyToken=B77A5C561934E089");
    EXPECT_EQ(upper->FullName(), "mscorlib, PublicKeyToken=b77a5c561934e089");

    // The attribute key comparison is case-insensitive.
    auto lowerKey = MustParse("mscorlib, publickeytoken=null");
    EXPECT_EQ(lowerKey->FullName(), "mscorlib, PublicKeyToken=null");
}

TEST(AssemblyNameInfoParse, FlagForms)
{
    auto retargetable = MustParse("mscorlib, Retargetable=Yes");
    EXPECT_EQ(static_cast<std::int32_t>(retargetable->Flags()),
              static_cast<std::int32_t>(AssemblyNameFlags::Retargetable));
    EXPECT_EQ(retargetable->FullName(), "mscorlib, Retargetable=Yes");

    // "No" clears the flag and renders no segment.
    EXPECT_EQ(MustParse("mscorlib, Retargetable=no")->FullName(), "mscorlib");

    auto contentType = MustParse("mscorlib, ContentType=WindowsRuntime");
    EXPECT_EQ(static_cast<std::int32_t>(contentType->Flags()), 512);
    EXPECT_EQ(contentType->FullName(), "mscorlib, ContentType=WindowsRuntime");

    // The architecture bits parse but never render.
    auto msil = MustParse("mscorlib, ProcessorArchitecture=MSIL");
    EXPECT_EQ(static_cast<std::int32_t>(msil->Flags()), 16);
    EXPECT_EQ(msil->FullName(), "mscorlib");
}

TEST(AssemblyNameInfoParse, VersionForms)
{
    struct Form {
        const char* input;
        const char* version;
        bool ok;
    };
    Form forms[] = {
        {"mscorlib, Version=1.2", "1.2", true},
        {"mscorlib, Version=1.2.3", "1.2.3", true},
        {"mscorlib, Version=1.2.3.4", "1.2.3.4", true},
        {"mscorlib, Version=1.2.3.4.5", "", false},
        {"mscorlib, Version=1", "", false},
        {"mscorlib, Version=+1.2", "", false},
        // The ushort sentinel collision: a 65535 component reads as absent.
        {"mscorlib, Version=1.2.65535", "1.2", true},
        {"mscorlib, Version=65535.1", "", false},
        {"mscorlib, Version=65535.65535", "", false},
        // NumberStyles.None still trims the surrounding whitespace INSIDE an
        // unquoted token.
        {"mscorlib, Version= 1.2 ", "1.2", true},
    };
    for (const Form& form : forms) {
        std::shared_ptr<AssemblyNameInfo> parsed;
        ASSERT_EQ(AssemblyNameInfo::TryParse(form.input, parsed), form.ok) << form.input;
        if (form.ok) {
            ASSERT_TRUE(parsed->Version().has_value()) << form.input;
            EXPECT_EQ(parsed->Version()->ToString(), form.version) << form.input;
        }
    }
}

TEST(AssemblyNameInfoParse, QuotedAndEscapedNames)
{
    // A name with an interior space survives the unquoted token trim.
    auto spaced = MustParse("my assembly");
    EXPECT_EQ(spaced->Name(), "my assembly");
    auto wrapped = MustParse(" my assembly ");
    EXPECT_EQ(wrapped->Name(), "my assembly");

    auto quoted = MustParse("\"my, assembly\"");
    EXPECT_EQ(quoted->Name(), "my, assembly");
    // The render escapes the ',' without re-quoting (AppendQuoted quotes only
    // for surrounding whitespace or quote characters).
    EXPECT_EQ(quoted->FullName(), "my\\, assembly");

    auto singleQuoted = MustParse("'my=assembly'");
    EXPECT_EQ(singleQuoted->Name(), "my=assembly");
    EXPECT_EQ(singleQuoted->FullName(), "my\\=assembly");

    auto escaped = MustParse("my\\,assembly");
    EXPECT_EQ(escaped->Name(), "my,assembly");
    EXPECT_EQ(escaped->FullName(), "my\\,assembly");

    auto doubleBackslash = MustParse("my\\\\assembly");
    EXPECT_EQ(doubleBackslash->Name(), "my\\assembly");
    EXPECT_EQ(doubleBackslash->FullName(), "my\\\\assembly");

    auto tab = MustParse("my\\tassembly");
    EXPECT_EQ(tab->Name(), "my\tassembly");
    EXPECT_EQ(tab->FullName(), "my\\tassembly");
}

TEST(AssemblyNameInfoParse, UnknownAttributesAreIgnoredDuplicatesFail)
{
    // Unknown attribute names fall through every IsAttribute arm and are
    // silently ignored -- including repeats (their seen-set bit never sets).
    auto ignored = MustParse("x, y=1, y=2");
    EXPECT_EQ(ignored->Name(), "x");
    EXPECT_EQ(ignored->FullName(), "x");

    auto unknown = MustParse("x, Unknown=1");
    EXPECT_EQ(unknown->FullName(), "x");

    // Duplicate KNOWN attributes fail (the seen-set).
    std::shared_ptr<AssemblyNameInfo> parsed;
    EXPECT_FALSE(AssemblyNameInfo::TryParse("x, Version=1.0, Version=2.0", parsed));

    // A quoted empty name fails the IsNullOrEmpty name gate.
    EXPECT_FALSE(AssemblyNameInfo::TryParse("\"\", Version=1.0", parsed));
}

TEST(AssemblyNameInfoParse, FailureMatrix)
{
    const char* failures[] = {
        "", " ", ",", "x,", "x, ", "x,,y=1", "x, y", "x, y=", "x, =1",
        "mscorlib, PublicKeyToken=", "mscorlib, PublicKey=",
        "mscorlib, Version=1.2.3.4.5", "mscorlib, Version=1",
        "mscorlib, Version=+1.2", "mscorlib, Version=65535.1",
        "mscorlib, Version=65535.65535",
        "mscorlib, Retargetable=maybe", "mscorlib, ContentType=Default",
        "mscorlib, ProcessorArchitecture=sparc",
        "my\\qassembly", "x, Version=1.0, Version=2.0",
    };
    for (const char* input : failures) {
        std::shared_ptr<AssemblyNameInfo> parsed;
        EXPECT_FALSE(AssemblyNameInfo::TryParse(input, parsed)) << "[" << input << "]";
    }
}

TEST(AssemblyNameInfoParse, TheParseEntryThrowsTheNetMessage)
{
    // The public Parse succeeds on an unknown-attribute name (gold).
    auto ok = AssemblyNameInfo::Parse("x, Unknown=1");
    EXPECT_EQ(ok->FullName(), "x");

    std::string message = WhatOf([] { AssemblyNameInfo::Parse(""); });
    EXPECT_EQ(message, "The given assembly name was invalid. (Parameter 'assemblyName')");
    message = WhatOf([] { AssemblyNameInfo::Parse("x, y="); });
    EXPECT_EQ(message, "The given assembly name was invalid. (Parameter 'assemblyName')");
}

TEST(AssemblyNameInfoCtor, BuildsAndRendersTheSegments)
{
    // The C# public ctor: the optional segments render in the fixed order.
    AssemblyNameInfo full("mscorlib", Version(4, 0, 0, 0), std::string(""),
                          AssemblyNameFlags::None,
                          std::vector<std::uint8_t>{ 0xb7, 0x7a, 0x5c, 0x56, 0x19, 0x34,
                                                     0xe0, 0x89 });
    EXPECT_EQ(
        full.FullName(),
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");

    AssemblyNameInfo bare("mscorlib");
    EXPECT_EQ(bare.FullName(), "mscorlib");

    // A public-key-typed key renders the PublicKey segment.
    AssemblyNameInfo publicKey("mscorlib", Version(4, 0, 0, 0), std::nullopt,
                               AssemblyNameFlags::PublicKey,
                               std::vector<std::uint8_t>{ 0x00, 0x11 });
    EXPECT_EQ(publicKey.FullName(), "mscorlib, Version=4.0.0.0, PublicKey=0011");

    // A public-key TOKEN longer than 8 bytes fails the render's guard.
    std::vector<std::uint8_t> nine(9, 0x01);
    AssemblyNameInfo tooLong("mscorlib", std::nullopt, std::nullopt,
                             AssemblyNameFlags::None, nine);
    std::string message = WhatOf([&tooLong] { tooLong.FullName(); });
    EXPECT_EQ(message, "Value does not fall within the expected range.");

    // The empty-name ArgumentNullException arm.
    message = WhatOf([] { AssemblyNameInfo empty(""); });
    EXPECT_EQ(message, "Value cannot be null. (Parameter 'name')");

    // The lazy FullName is stable across reads (the C# caches it).
    EXPECT_EQ(bare.FullName(), bare.FullName());
}

} // namespace
