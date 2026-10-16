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

// A persistent (immutable) stack. The C# decompiler uses ImmutableStack<T> in
// the IL reader to snapshot the evaluation stack across block boundaries; the
// snapshots are branched on and must not mutate. std::stack is not a substitute:
// it mutates in place and copies wholesale. This is a shared_ptr-linked cons
// list, so push/pop return a new stack sharing most of its spine with the old
// one in O(1), matching System.Collections.Immutable.ImmutableStack<T>.

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace ILSpy::Decompiler::Util {

template <typename T>
class ImmutableStack {
    struct Node {
        T value;
        std::shared_ptr<const Node> next;
    };
    std::shared_ptr<const Node> head_;
    std::size_t size_ = 0;

    ImmutableStack(std::shared_ptr<const Node> h, std::size_t s)
        : head_(std::move(h)), size_(s) {}

public:
    ImmutableStack() = default;

    bool empty() const noexcept { return !head_; }
    std::size_t size() const noexcept { return size_; }

    // Caller must ensure !empty() (matches ImmutableStack<T>.Peek semantics).
    const T& top() const { return head_->value; }

    ImmutableStack push(T v) const {
        auto n = std::make_shared<Node>(Node{std::move(v), head_});
        return ImmutableStack(std::move(n), size_ + 1);
    }

    // Caller must ensure !empty().
    ImmutableStack pop() const { return ImmutableStack(head_->next, size_ - 1); }
};

} // namespace ILSpy::Decompiler::Util
