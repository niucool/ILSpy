// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the PropertyAndEventBackingFieldLookup tests: the compiler
// naming-convention map from backing-field rows to their property or event
// rows. The property arm runs over the connid corpus (the EventSetter
// <Name>k__BackingField rows); the event arm over mscorlib (a field-like
// event's same-named field), walking the corpus with the same enumeration
// surface the lookup itself reads.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

namespace MD = ::ILSpy::Decompiler::Metadata;

bool FileAvailable(const char* path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// The C# property convention: a `<Name>k__BackingField` field maps to its
// property. The connid corpus's EventSetter carries the <Event> and
// <Handler> backing fields.
TEST(PropertyAndEventBackingFieldLookupTest, PropertyBackingFieldMapsToProperty)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    MD::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    // Walk for a property with a matching <Name>k__BackingField field (the
    // same enumeration the lookup reads; a self-consistency probe).
    std::uint32_t propertyToken = 0;
    std::uint32_t fieldToken = 0;
    for (const auto& t : file.TypeDefs()) {
        auto fields = file.GetFields(t.Token);
        for (const auto& p : file.GetProperties(t.Token)) {
            for (const auto& f : fields) {
                if (f.Name == "<" + p.Name + ">k__BackingField") {
                    propertyToken = p.Token;
                    fieldToken = f.Token;
                }
            }
        }
    }
    ASSERT_NE(propertyToken, 0u)
        << "the corpus carries the backing-field property shape";
    const auto& lookup = file.GetPropertyAndEventBackingFieldLookup();
    std::uint32_t mapped = 0;
    EXPECT_TRUE(lookup.IsPropertyBackingField(fieldToken, &mapped));
    EXPECT_EQ(mapped, propertyToken);
}

// A field that is no convention form maps to nothing.
TEST(PropertyAndEventBackingFieldLookupTest, NonBackingFieldMapsToNothing)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    MD::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    // The corpus's _contentLoaded (the XAML code-behind field) is a plain
    // field, not a convention form.
    std::uint32_t plainField = 0;
    for (const auto& t : file.TypeDefs()) {
        for (const auto& f : file.GetFields(t.Token)) {
            if (f.Name == "_contentLoaded")
                plainField = f.Token;
        }
    }
    ASSERT_NE(plainField, 0u);
    const auto& lookup = file.GetPropertyAndEventBackingFieldLookup();
    std::uint32_t mapped = 0;
    EXPECT_FALSE(lookup.IsPropertyBackingField(plainField, &mapped));
    EXPECT_FALSE(lookup.IsEventBackingField(plainField, &mapped));
}

// The C# event convention: a field with the SAME NAME as an event of the
// same type maps to it. mscorlib carries the field-like event shape.
TEST(PropertyAndEventBackingFieldLookupTest, EventSameNameFieldMapsToEvent)
{
    if (!FileAvailable(MscorlibPath()))
        GTEST_SKIP() << "mscorlib fixture not available";
    MD::MetadataFile file(MscorlibPath());
    ASSERT_TRUE(file.IsValid());

    std::uint32_t eventToken = 0;
    std::uint32_t fieldToken = 0;
    for (const auto& t : file.TypeDefs()) {
        auto fields = file.GetFields(t.Token);
        for (const auto& e : file.GetEvents(t.Token)) {
            for (const auto& f : fields) {
                if (f.Name == e.Name) {
                    eventToken = e.Token;
                    fieldToken = f.Token;
                }
            }
        }
        if (eventToken != 0)
            break;
    }
    ASSERT_NE(eventToken, 0u)
        << "mscorlib carries the same-named field-like event shape";
    const auto& lookup = file.GetPropertyAndEventBackingFieldLookup();
    std::uint32_t mapped = 0;
    EXPECT_TRUE(lookup.IsEventBackingField(fieldToken, &mapped));
    EXPECT_EQ(mapped, eventToken);
}

} // namespace
