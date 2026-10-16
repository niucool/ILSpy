// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/Util/UnionFind.cs -- the union-find
// (disjoint-set) structure with path-compressed Find and rank-ordered Merge.
// The SplitVariables GroupStores pass uses it to merge store instructions
// that must stay together (cannot be split into separate live ranges).

#pragma once

#include <cassert>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ILSpy::Decompiler::Util {

template <typename T>
class UnionFind {
public:
    // The representative element of the set containing `element` (the C#
    // `T Find(T element)`). First sight creates a singleton set.
    const T& Find(const T& element) {
        Node* node = GetNode(element);
        Node* root = FindRoot(node);
        return root->value;
    }

    // Join the sets containing `a` and `b` (the C# `void Merge(T a, T b)`),
    // union-by-rank.
    void Merge(const T& a, const T& b) {
        Node* rootA = FindRoot(GetNode(a));
        Node* rootB = FindRoot(GetNode(b));
        if (rootA == rootB) return;
        if (rootA->rank < rootB->rank) {
            rootA->parent = rootB;
        } else if (rootA->rank > rootB->rank) {
            rootB->parent = rootA;
        } else {
            rootB->parent = rootA;
            rootA->rank++;
        }
    }

private:
    struct Node {
        int rank = 0;
        Node* parent = nullptr;
        T value;

        explicit Node(T v) : value(std::move(v)) { parent = this; }
    };

    Node* GetNode(const T& element) {
        auto it = mapping_.find(element);
        if (it != mapping_.end()) return it->second;
        auto node = std::make_unique<Node>(element);
        Node* raw = node.get();
        nodes_.push_back(std::move(node));
        mapping_.emplace(element, raw);
        return raw;
    }

    static Node* FindRoot(Node* node) {
        assert(node != nullptr);
        while (node->parent != node) {
            // Path compression: the C# recurses; the iterative two-pass walk
            // keeps the same amortized bound without the stack depth.
            node->parent = node->parent->parent;
            node = node->parent;
        }
        return node;
    }

    std::unordered_map<T, Node*> mapping_;
    std::vector<std::unique_ptr<Node>> nodes_;
};

} // namespace ILSpy::Decompiler::Util
