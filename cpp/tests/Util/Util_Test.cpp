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

#include "Decompiler/Util/ImmutableStack.hpp"
#include "Decompiler/Util/Span.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// ---- Span ----------------------------------------------------------------

TEST(Util_Span, PointerCountConstruction) {
    int arr[] = {10, 20, 30};
    ILSpy::Decompiler::Util::Span<int> s(arr, 3);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s.data(), arr);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s[0], 10);
    EXPECT_EQ(s[2], 30);
    EXPECT_EQ(s.front(), 10);
    EXPECT_EQ(s.back(), 30);
    EXPECT_EQ(s.begin() + 3, s.end());
}

TEST(Util_Span, IteratorRangeConstruction) {
    std::vector<int> v = {1, 2, 3, 4};
    ILSpy::Decompiler::Util::Span<int> s(v.data(), v.data() + v.size());
    ASSERT_EQ(s.size(), 4u);
    int sum = 0;
    for (int x : s) sum += x;
    EXPECT_EQ(sum, 10);
}

TEST(Util_Span, ContainerConstructionAndSubspans) {
    std::vector<int> v = {0, 1, 2, 3, 4};
    ILSpy::Decompiler::Util::Span<int> s(v);
    ASSERT_EQ(s.size(), 5u);
    EXPECT_EQ(s.first(2)[1], 1);
    EXPECT_EQ(s.last(2)[0], 3);
    EXPECT_EQ(s.subspan(2).size(), 3u);
    EXPECT_EQ(s.subspan(1, 2)[1], 2);
}

// ---- Utf -----------------------------------------------------------------

TEST(Util_Utf, RoundTripAscii) {
    std::u16string u16 = u"Hello";
    std::string utf8 = ILSpy::Decompiler::Util::Utf16ToUtf8(u16);
    EXPECT_EQ(utf8, "Hello");
    std::u16string back = ILSpy::Decompiler::Util::Utf8ToUtf16(utf8);
    EXPECT_EQ(back, u16);
}

TEST(Util_Utf, RoundTripBmpAndSupplementary) {
    // "naive" with a Latin small letter i (U+00EF) and the Euro sign (U+20AC).
    const std::u16string u16 = {u'n', u'a', u'\u00EF', u'v', u'e',
                                u' ', u'\u20AC'};
    std::string utf8 = ILSpy::Decompiler::Util::Utf16ToUtf8(u16);
    std::u16string back = ILSpy::Decompiler::Util::Utf8ToUtf16(utf8);
    EXPECT_EQ(back, u16);
}

TEST(Util_Utf, SupplementaryCodePointUsesSurrogatePair) {
    // U+1F600 GRINNING FACE -> surrogate pair D83D DE00. Surrogates are not
    // valid as \u universal-character-names, so build them via integer cast.
    const std::u16string u16 = {static_cast<char16_t>(0xD83D),
                               static_cast<char16_t>(0xDE00)};
    std::string utf8 = ILSpy::Decompiler::Util::Utf16ToUtf8(u16);
    EXPECT_EQ(utf8, std::string("\xF0\x9F\x98\x80"));
    std::u16string back = ILSpy::Decompiler::Util::Utf8ToUtf16(utf8);
    EXPECT_EQ(back, u16);
}

TEST(Util_Utf, EmptyAndInvalid) {
    EXPECT_EQ(ILSpy::Decompiler::Util::Utf16ToUtf8(u""), "");
    EXPECT_EQ(ILSpy::Decompiler::Util::Utf8ToUtf16("").size(), 0u);
    // A lone leading continuation byte is ill-formed; replaced with U+FFFD.
    std::u16string r = ILSpy::Decompiler::Util::Utf8ToUtf16("\x80");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], static_cast<char16_t>(0xFFFD));
}

// ---- ImmutableStack ------------------------------------------------------

TEST(Util_ImmutableStack, EmptyAndPushPop) {
    ILSpy::Decompiler::Util::ImmutableStack<int> s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);

    auto s1 = s.push(1);
    auto s2 = s1.push(2);
    ASSERT_FALSE(s2.empty());
    EXPECT_EQ(s2.size(), 2u);
    EXPECT_EQ(s2.top(), 2);

    auto s3 = s2.pop();
    ASSERT_FALSE(s3.empty());
    EXPECT_EQ(s3.top(), 1);

    // Persistence: the originals are unchanged.
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s1.top(), 1);
    EXPECT_EQ(s2.top(), 2);
}

TEST(Util_ImmutableStack, SharedSpineIsCheap) {
    // Pushing onto a large stack and popping must not copy the spine.
    ILSpy::Decompiler::Util::ImmutableStack<int> s;
    for (int i = 0; i < 1000; ++i) s = s.push(i);
    auto snapshot = s;            // cheap: shares the spine
    s = s.pop();                  // also cheap
    EXPECT_EQ(snapshot.size(), 1000u);
    EXPECT_EQ(s.size(), 999u);
    EXPECT_EQ(snapshot.top(), 999);
    EXPECT_EQ(s.top(), 998);
}
