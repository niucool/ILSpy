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

// Port of ICSharpCode.ILSpyX/Extensions/CollectionExtensions.cs: the
// collection helpers the ILSpyX layers share (PeekOrDefault, the two
// BinarySearch forms, InsertSorted, the distinct AddRange).
//
// C#-to-C++ porting decisions:
//  * The extension methods port as function templates in this header (the
//    C# static class is a C#-ism; every member is generic).
//  * The C# `IList<T>` ports as any indexable container (operator[], and a
//    size() the traits below read); `ICollection<T>.Contains` maps to a
//    linear std::find.
//  * The C# `IComparer<T>`/key-selector delegates port as callables
//    (lambdas or function pointers).
//  * The C# `ArgumentNullException` arms port as std::invalid_argument
//    (the repo's ArgumentNullException convention) and the
//    ArgumentOutOfRangeException arms as std::out_of_range (the
//    ResourcesFile convention: exact C# message text).
//  * PeekOrDefault returns default(T): the value-type reading (T{}) is the
//    only meaningful one for a C++ value; the reference-type null case has
//    no analogue (documented divergence).
//  * NOT PORTED: the `Deconstruct` pair and the `EmptyIfNull` family --
//    C# nullable-enumerable idiom sugar with no C++ counterpart and no
//    consumer in the ported ILSpyX subset.

#pragma once

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <stack>
#include <string>
#include <utility>

namespace ILSpy::ILSpyX::Extensions {

// The C# `internal static void AddRange<T>(this ICollection<T> list,
// IEnumerable<T> items)`: add every item the collection does not already
// contain.
template <typename Container, typename Items>
void AddRangeDistinct(Container& list, const Items& items)
{
    for (const auto& item : items) {
        if (std::find(list.begin(), list.end(), item) == list.end())
            list.push_back(item);
    }
}

// The C# `public static T? PeekOrDefault<T>(this Stack<T> stack)`: the top
// of the stack, or default(T) (T{} -- see the header note) when empty.
template <typename T>
T PeekOrDefault(const std::stack<T>& stack)
{
    if (stack.empty())
        return T{};
    return stack.top();
}

// The C# `public static int BinarySearch<T>(this IList<T> list, T item,
// int start, int count, IComparer<T> comparer)`: the index of an equal
// element in [start, start+count), or the complement of the insertion
// point.
template <typename Container, typename T, typename Comparer>
int BinarySearch(const Container& list, const T& item, int start, int count,
    Comparer comparer)
{
    if (start < 0 || start >= static_cast<int>(list.size()))
        throw std::out_of_range("Value must be between 0 and "
            + std::to_string(list.size() - 1) + ". (Parameter 'start')");
    if (count < 0 || count > static_cast<int>(list.size()) - start)
        throw std::out_of_range("Value must be between 0 and "
            + std::to_string(static_cast<int>(list.size()) - start)
            + ". (Parameter 'count')");
    int end = start + count - 1;
    while (start <= end) {
        int pivot = (start + end) / 2;
        int result = comparer(item, list[pivot]);
        if (result == 0)
            return pivot;
        if (result < 0)
            end = pivot - 1;
        else
            start = pivot + 1;
    }
    return ~start;
}

// The C# `public static int BinarySearch<T, TKey>(this IList<T> instance,
// TKey itemKey, Func<T, TKey> keySelector)`: the index of the element whose
// selected key equals itemKey, or the complement of the insertion point.
template <typename Container, typename TKey, typename Selector>
int BinarySearchByKey(const Container& instance, const TKey& itemKey,
    Selector keySelector)
{
    int start = 0;
    int end = static_cast<int>(instance.size()) - 1;

    while (start <= end) {
        int m = (start + end) / 2;
        TKey key = keySelector(instance[m]);
        int result = key < itemKey ? -1 : (itemKey < key ? 1 : 0);
        if (result == 0)
            return m;
        if (result < 0)
            start = m + 1;
        else
            end = m - 1;
    }
    return ~start;
}

// The C# `public static void InsertSorted<T>(this IList<T> list, T item,
// IComparer<T> comparer)`: insert at the binary-searched position.
template <typename Container, typename T, typename Comparer>
void InsertSorted(Container& list, const T& item, Comparer comparer)
{
    if (list.empty()) {
        list.push_back(item);
        return;
    }
    int index = BinarySearch(list, item, 0, static_cast<int>(list.size()),
        comparer);
    list.insert(list.begin() + (index < 0 ? ~index : index), item);
}

}  // namespace ILSpy::ILSpyX::Extensions
