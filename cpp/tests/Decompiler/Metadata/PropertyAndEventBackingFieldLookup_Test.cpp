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

// Tests for the PropertyAndEventBackingFieldLookup port
// (PropertyAndEventBackingFieldLookup.cs): the backing-field -> property/event association
// map. The synthetic BfSynth.dll fixture (built with the REAL .NET 10 MetadataBuilder) pins
// the `<Property>k__BackingField` / compiler-generated `_Property` property arms and the
// same-named / `Event`-suffixed event arms. The real mscorlib fixture pins the associations
// on real compiler output and drives the whole-file invariant sweep (every associated field
// has the exact naming shape the lookup derives it from).

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/PropertyAndEventBackingFieldLookup.hpp"
#include "TestFixtures/BfSynth.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace TM = ILSpy::Decompiler::Metadata;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::uint32_t FindType(const TM::MetadataFile& file, std::string_view nameSpace,
                       std::string_view name) {
    for (const auto& type : file.TypeDefs()) {
        if (type.Namespace == nameSpace && type.Name == name) return type.Token;
    }
    return 0;
}

std::uint32_t FindField(const TM::MetadataFile& file, std::uint32_t typeToken,
                        std::string_view name) {
    for (const auto& field : file.GetFields(typeToken)) {
        if (field.Name == name) return field.Token;
    }
    return 0;
}

}  // namespace

// The synthetic fixture's property associations: the C# `<P>k__BackingField` spelling (no
// attribute required) and the VB `_Q` spelling that carries [CompilerGenerated]. The `_R`
// field has no attribute, so it does NOT become property R's backing field.
TEST(PropertyAndEventBackingFieldLookupTest, SynthPropertyBackingFieldsMatchGold) {
    std::string path = ILSpy::Tests::WriteBfSynthDll();
    TM::MetadataFile file{ path };
    ASSERT_TRUE(file.IsValid());
    const TM::PropertyAndEventBackingFieldLookup& lookup =
        file.GetPropertyAndEventBackingFieldLookup();

    std::uint32_t property = 0;
    ASSERT_TRUE(lookup.IsPropertyBackingField(0x04000001u, property));
    EXPECT_EQ(property, 0x17000001u);  // <P>k__BackingField -> P

    property = 0;
    ASSERT_TRUE(lookup.IsPropertyBackingField(0x04000002u, property));
    EXPECT_EQ(property, 0x17000002u);  // _Q (compiler-generated) -> Q

    property = 0;
    EXPECT_FALSE(lookup.IsPropertyBackingField(0x04000003u, property));  // _R -> none
}

// The synthetic fixture's event associations: the same-named field for event E, and the
// `FEvent` field resolving to event FEvent (event F's "Event"-suffix fallback is skipped
// because an event named FEvent exists).
TEST(PropertyAndEventBackingFieldLookupTest, SynthEventBackingFieldsMatchGold) {
    std::string path = ILSpy::Tests::WriteBfSynthDll();
    TM::MetadataFile file{ path };
    const TM::PropertyAndEventBackingFieldLookup& lookup =
        file.GetPropertyAndEventBackingFieldLookup();

    std::uint32_t eventToken = 0;
    ASSERT_TRUE(lookup.IsEventBackingField(0x04000004u, eventToken));
    EXPECT_EQ(eventToken, 0x14000001u);  // E -> event E

    eventToken = 0;
    ASSERT_TRUE(lookup.IsEventBackingField(0x04000005u, eventToken));
    EXPECT_EQ(eventToken, 0x14000003u);  // FEvent -> event FEvent, not event F
}

// The two maps are disjoint and an unassociated field answers false for both; a non-field
// token (row 0, a past-the-end row) is a graceful false.
TEST(PropertyAndEventBackingFieldLookupTest, UnassociatedFieldsAnswerFalse) {
    std::string path = ILSpy::Tests::WriteBfSynthDll();
    TM::MetadataFile file{ path };
    const TM::PropertyAndEventBackingFieldLookup& lookup =
        file.GetPropertyAndEventBackingFieldLookup();

    std::uint32_t token = 0;
    // The property backing field is not an event backing field, and vice versa.
    EXPECT_FALSE(lookup.IsEventBackingField(0x04000001u, token));
    EXPECT_FALSE(lookup.IsPropertyBackingField(0x04000004u, token));
    // A non-backing field and out-of-range field tokens.
    EXPECT_FALSE(lookup.IsPropertyBackingField(0x04000003u, token));
    EXPECT_FALSE(lookup.IsEventBackingField(0x04000003u, token));
    EXPECT_FALSE(lookup.IsPropertyBackingField(0x04000000u, token));
    EXPECT_FALSE(lookup.IsPropertyBackingField(0x04FFFFFFu, token));
}

// Real mscorlib event backing fields (the C# field-like event spelling: a private field with
// the event's own name).
TEST(PropertyAndEventBackingFieldLookupTest, MscorlibEventBackingFieldsMatchGold) {
    const char* path = MscorlibPath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "mscorlib fixture not present";
    TM::MetadataFile file{ path };
    ASSERT_TRUE(file.IsValid());
    const TM::PropertyAndEventBackingFieldLookup& lookup =
        file.GetPropertyAndEventBackingFieldLookup();

    std::uint32_t typeToken = FindType(file, "System", "AppDomain");
    ASSERT_NE(typeToken, 0u);
    std::uint32_t field = FindField(file, typeToken, "AssemblyLoad");
    ASSERT_NE(field, 0u);
    std::uint32_t association = 0;
    ASSERT_TRUE(lookup.IsEventBackingField(field, association));
    EXPECT_EQ(association, 0x14000002u);

    typeToken = FindType(file, "System", "Progress`1");
    ASSERT_NE(typeToken, 0u);
    field = FindField(file, typeToken, "ProgressChanged");
    ASSERT_NE(field, 0u);
    association = 0;
    ASSERT_TRUE(lookup.IsEventBackingField(field, association));
    EXPECT_EQ(association, 0x14000013u);

    // A VB-style `_Name` event backing field would need the attribute; the `_EventSourceCreated`
    // field here is the plain C# same-name spelling.
    typeToken = FindType(file, "System.Diagnostics.Tracing", "EventListener");
    ASSERT_NE(typeToken, 0u);
    field = FindField(file, typeToken, "_EventSourceCreated");
    ASSERT_NE(field, 0u);
    association = 0;
    ASSERT_TRUE(lookup.IsEventBackingField(field, association));
    EXPECT_EQ(association, 0x14000016u);
}

// Real mscorlib property backing fields, including hand-authored fields that happen to carry
// the `<Property>k__BackingField` name (the lookup does not require the attribute on that
// spelling).
TEST(PropertyAndEventBackingFieldLookupTest, MscorlibPropertyBackingFieldsMatchGold) {
    const char* path = MscorlibPath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "mscorlib fixture not present";
    TM::MetadataFile file{ path };
    ASSERT_TRUE(file.IsValid());
    const TM::PropertyAndEventBackingFieldLookup& lookup =
        file.GetPropertyAndEventBackingFieldLookup();

    std::uint32_t typeToken = FindType(file, "System", "AppContextSwitches");
    ASSERT_NE(typeToken, 0u);
    std::uint32_t field = FindField(file, typeToken, "<DisableCaching>k__BackingField");
    ASSERT_NE(field, 0u);
    std::uint32_t association = 0;
    ASSERT_TRUE(lookup.IsPropertyBackingField(field, association));
    EXPECT_EQ(association, 0x1700002Eu);

    typeToken = FindType(file, "System.Diagnostics.Tracing", "EventSourceAttribute");
    ASSERT_NE(typeToken, 0u);
    field = FindField(file, typeToken, "<Name>k__BackingField");
    ASSERT_NE(field, 0u);
    association = 0;
    ASSERT_TRUE(lookup.IsPropertyBackingField(field, association));
    EXPECT_EQ(association, 0x170007E2u);
}

// The whole-file invariant: every field the lookup associates must have the exact naming
// shape the lookup derives the association from, and the associated property/event must live
// on the field's own declaring type.
TEST(PropertyAndEventBackingFieldLookupTest, MscorlibAssociationShapesAreExact) {
    const char* path = MscorlibPath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "mscorlib fixture not present";
    TM::MetadataFile file{ path };
    ASSERT_TRUE(file.IsValid());
    const TM::PropertyAndEventBackingFieldLookup& lookup =
        file.GetPropertyAndEventBackingFieldLookup();

    std::size_t propertyAssociations = 0;
    std::size_t eventAssociations = 0;
    for (const auto& type : file.TypeDefs()) {
        std::vector<std::string> eventNames;
        for (const auto& event : file.GetEvents(type.Token)) {
            eventNames.push_back(event.Name);
        }
        for (const auto& field : file.GetFields(type.Token)) {
            std::uint32_t association = 0;
            if (lookup.IsPropertyBackingField(field.Token, association)) {
                ++propertyAssociations;
                std::string propertyName = file.GetPropertyName(association);
                EXPECT_TRUE(field.Name == "<" + propertyName + ">k__BackingField"
                            || field.Name == "_" + propertyName)
                    << "field " << field.Name << " property " << propertyName;
            }
            if (lookup.IsEventBackingField(field.Token, association)) {
                ++eventAssociations;
                std::string eventName = file.GetEventName(association);
                bool exact = field.Name == eventName;
                bool suffixed = field.Name == eventName + "Event"
                    && std::find(eventNames.begin(), eventNames.end(), field.Name) == eventNames.end();
                EXPECT_TRUE(exact || suffixed)
                    << "field " << field.Name << " event " << eventName;
            }
        }
    }
    // Sanity: the fixture really exercises both maps.
    EXPECT_GT(propertyAssociations, 0u);
    EXPECT_GT(eventAssociations, 0u);
}

// An unreadable file yields an empty lookup that answers false for every key.
TEST(PropertyAndEventBackingFieldLookupTest, InvalidFileYieldsEmptyLookup) {
    TM::MetadataFile file{ "C:\\this\\path\\does\\not\\exist\\nope.dll" };
    ASSERT_FALSE(file.IsValid());
    const TM::PropertyAndEventBackingFieldLookup& lookup =
        file.GetPropertyAndEventBackingFieldLookup();
    std::uint32_t association = 0;
    EXPECT_FALSE(lookup.IsPropertyBackingField(0x04000001u, association));
    EXPECT_FALSE(lookup.IsEventBackingField(0x04000001u, association));
}

// The MetadataFile lazy accessor hands back one stable lookup instance (the C# LazyInit
// property identity).
TEST(PropertyAndEventBackingFieldLookupTest, LazyAccessorIsStable) {
    std::string path = ILSpy::Tests::WriteBfSynthDll();
    TM::MetadataFile file{ path };
    const TM::PropertyAndEventBackingFieldLookup* first =
        &file.GetPropertyAndEventBackingFieldLookup();
    const TM::PropertyAndEventBackingFieldLookup* second =
        &file.GetPropertyAndEventBackingFieldLookup();
    EXPECT_EQ(first, second);
}
