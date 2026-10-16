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

// Minimal std::span polyfill for C++17 (std::span is C++20). The decompiler port
// uses this for read-only views over memory buffers and decoded tables. It is
// intentionally a small subset; grow it as real call sites demand.

#pragma once

#include <cstddef>
#include <iterator>

namespace ILSpy::Decompiler::Util {

template <typename T>
class Span {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = pointer;
    using const_iterator = const_pointer;

    constexpr Span() noexcept = default;
    constexpr Span(pointer ptr, size_type count) noexcept : data_(ptr), size_(count) {}
    constexpr Span(pointer first, pointer last) noexcept
        : data_(first), size_(static_cast<size_type>(last - first)) {}

    // Construct from any contiguous container exposing data()/size() (std::vector,
    // std::array, std::string). Excluded for Span itself via the implicit copy ctor.
    template <typename Cont>
    constexpr Span(Cont& c) noexcept : data_(c.data()), size_(c.size()) {}

    constexpr pointer data() const noexcept { return data_; }
    constexpr size_type size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr reference operator[](size_type i) const noexcept { return data_[i]; }
    constexpr reference front() const noexcept { return data_[0]; }
    constexpr reference back() const noexcept { return data_[size_ - 1]; }

    constexpr iterator begin() const noexcept { return data_; }
    constexpr iterator end() const noexcept { return data_ + size_; }

    constexpr Span first(size_type n) const noexcept { return Span(data_, n); }
    constexpr Span last(size_type n) const noexcept { return Span(data_ + size_ - n, n); }
    constexpr Span subspan(size_type off, size_type n = static_cast<size_type>(-1)) const noexcept {
        return Span(data_ + off, n == static_cast<size_type>(-1) ? size_ - off : n);
    }

private:
    pointer data_ = nullptr;
    size_type size_ = 0;
};

} // namespace ILSpy::Decompiler::Util
