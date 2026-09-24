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

// Tests for the CollectionExtensions port
// (ICSharpCode.ILSpyX/Extensions/CollectionExtensions.cs): the binary
// search contracts (found index, complement-of-insertion-point, the range
// validation), InsertSorted's placement, PeekOrDefault's empty-stack
// default, and the distinct AddRange.

#include "ILSpyX/Extensions/CollectionExtensions.hpp"

#include <gtest/gtest.h>

#include <stack>
#include <string>
#include <vector>

namespace {

namespace Ext = ILSpy::ILSpyX::Extensions;

int CompareInts(int a, int b)
{
    return a < b ? -1 : (b < a ? 1 : 0);
}

}  // namespace

TEST(CollectionExtensionsTest, PeekOrDefaultReturnsTopOrDefault)
{
    std::stack<int> empty;
    EXPECT_EQ(Ext::PeekOrDefault(empty), 0);  // default(int)

    std::stack<std::string> strings;
    EXPECT_EQ(Ext::PeekOrDefault(strings), std::string());  // default(string)

    std::stack<int> values;
    values.push(7);
    values.push(42);
    EXPECT_EQ(Ext::PeekOrDefault(values), 42);
    EXPECT_EQ(values.size(), 2u);  // peek does not pop
}

TEST(CollectionExtensionsTest, BinarySearchFindsPresentElements)
{
    const std::vector<int> list{1, 3, 5, 7, 9, 11};
    EXPECT_EQ(Ext::BinarySearch(list, 1, 0, 6, &CompareInts), 0);
    EXPECT_EQ(Ext::BinarySearch(list, 7, 0, 6, &CompareInts), 3);
    EXPECT_EQ(Ext::BinarySearch(list, 11, 0, 6, &CompareInts), 5);
}

TEST(CollectionExtensionsTest, BinarySearchReturnsComplementOfInsertionPoint)
{
    const std::vector<int> list{1, 3, 5, 7, 9, 11};
    // 4 fits at index 2 (after 3, before 5).
    EXPECT_EQ(Ext::BinarySearch(list, 4, 0, 6, &CompareInts), ~2);
    // 0 fits before everything.
    EXPECT_EQ(Ext::BinarySearch(list, 0, 0, 6, &CompareInts), ~0);
    // 100 fits past the end.
    EXPECT_EQ(Ext::BinarySearch(list, 100, 0, 6, &CompareInts), ~6);
}

TEST(CollectionExtensionsTest, BinarySearchHonorsStartAndCountWindow)
{
    const std::vector<int> list{10, 20, 30, 40, 50, 60};
    // Search the [1, 4) window (20, 30, 40).
    EXPECT_EQ(Ext::BinarySearch(list, 30, 1, 3, &CompareInts), 2);
    // 40 is outside the window: its insertion point inside the window is
    // past it (start+count = 4), so the complement of 4.
    EXPECT_EQ(Ext::BinarySearch(list, 40, 0, 3, &CompareInts), ~3);
    // 20 at window start.
    EXPECT_EQ(Ext::BinarySearch(list, 20, 1, 2, &CompareInts), 1);
}

TEST(CollectionExtensionsTest, BinarySearchValidatesTheWindow)
{
    const std::vector<int> list{1, 2, 3};
    EXPECT_THROW({ Ext::BinarySearch(list, 1, -1, 2, &CompareInts); },
        std::out_of_range);
    EXPECT_THROW({ Ext::BinarySearch(list, 1, 3, 1, &CompareInts); },
        std::out_of_range);  // start == size is out of range
    EXPECT_THROW({ Ext::BinarySearch(list, 1, 0, -1, &CompareInts); },
        std::out_of_range);
    EXPECT_THROW({ Ext::BinarySearch(list, 1, 1, 3, &CompareInts); },
        std::out_of_range);  // count overflows the tail

    // The exact C# messages.
    try {
        Ext::BinarySearch(list, 1, 5, 1, &CompareInts);
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(),
            "Value must be between 0 and 2. (Parameter 'start')");
    }
}

TEST(CollectionExtensionsTest, BinarySearchByKeyFindsAndComplements)
{
    const std::vector<std::string> list{"alpha", "beta", "gamma"};
    const auto keyOf = [](const std::string& s) { return s; };
    EXPECT_EQ(Ext::BinarySearchByKey(list, std::string("beta"), keyOf), 1);
    EXPECT_EQ(Ext::BinarySearchByKey(list, std::string("aaa"), keyOf), ~0);
    // "delta" sorts between "beta" and "gamma" (index 2).
    EXPECT_EQ(Ext::BinarySearchByKey(list, std::string("delta"), keyOf), ~2);

    // A key selector over a struct field.
    struct Entry {
        int id;
        std::string name;
    };
    const std::vector<Entry> entries{{1, "one"}, {3, "three"}, {5, "five"}};
    const auto idOf = [](const Entry& e) { return e.id; };
    EXPECT_EQ(Ext::BinarySearchByKey(entries, 3, idOf), 1);
    EXPECT_EQ(Ext::BinarySearchByKey(entries, 4, idOf), ~2);
}

TEST(CollectionExtensionsTest, InsertSortedPlacesElementsInOrder)
{
    std::vector<int> list;
    Ext::InsertSorted(list, 5, &CompareInts);
    Ext::InsertSorted(list, 1, &CompareInts);
    Ext::InsertSorted(list, 9, &CompareInts);
    Ext::InsertSorted(list, 3, &CompareInts);
    EXPECT_EQ(list, (std::vector<int>{1, 3, 5, 9}));

    // Duplicates land at the found position (before the equal element).
    Ext::InsertSorted(list, 5, &CompareInts);
    EXPECT_EQ(list, (std::vector<int>{1, 3, 5, 5, 9}));
}

TEST(CollectionExtensionsTest, AddRangeDistinctSkipsDuplicates)
{
    std::vector<int> list{1, 2};
    const std::vector<int> more{2, 3, 1, 4};
    Ext::AddRangeDistinct(list, more);
    EXPECT_EQ(list, (std::vector<int>{1, 2, 3, 4}));
}
