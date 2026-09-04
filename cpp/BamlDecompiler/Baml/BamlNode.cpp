// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
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

// Port of ICSharpCode.BamlDecompiler/Baml/BamlNode.cs (Ki, 2015, MIT).
// The class declarations and the porting decisions live in BamlNode.hpp;
// this file carries the header/footer predicates, the match table, and
// the Parse stack machine.

#include "BamlDecompiler/Baml/BamlNode.hpp"

#include <stdexcept>

namespace ILSpy::BamlDecompiler::Baml {

bool BamlNode::IsHeader(const BamlRecord& record) {
    switch (record.Type()) {
        case BamlRecordType::ConstructorParametersStart:
        case BamlRecordType::DocumentStart:
        case BamlRecordType::ElementStart:
        case BamlRecordType::KeyElementStart:
        case BamlRecordType::NamedElementStart:
        case BamlRecordType::PropertyArrayStart:
        case BamlRecordType::PropertyComplexStart:
        case BamlRecordType::PropertyDictionaryStart:
        case BamlRecordType::PropertyListStart:
        case BamlRecordType::StaticResourceStart:
            return true;
        default:
            return false;
    }
}

bool BamlNode::IsFooter(const BamlRecord& record) {
    switch (record.Type()) {
        case BamlRecordType::ConstructorParametersEnd:
        case BamlRecordType::DocumentEnd:
        case BamlRecordType::ElementEnd:
        case BamlRecordType::KeyElementEnd:
        case BamlRecordType::PropertyArrayEnd:
        case BamlRecordType::PropertyComplexEnd:
        case BamlRecordType::PropertyDictionaryEnd:
        case BamlRecordType::PropertyListEnd:
        case BamlRecordType::StaticResourceEnd:
            return true;
        default:
            return false;
    }
}

bool BamlNode::IsMatch(const BamlRecord& header, const BamlRecord& footer) {
    switch (header.Type()) {
        case BamlRecordType::ConstructorParametersStart:
            return footer.Type() == BamlRecordType::ConstructorParametersEnd;

        case BamlRecordType::DocumentStart:
            return footer.Type() == BamlRecordType::DocumentEnd;

        case BamlRecordType::KeyElementStart:
            return footer.Type() == BamlRecordType::KeyElementEnd;

        case BamlRecordType::PropertyArrayStart:
            return footer.Type() == BamlRecordType::PropertyArrayEnd;

        case BamlRecordType::PropertyComplexStart:
            return footer.Type() == BamlRecordType::PropertyComplexEnd;

        case BamlRecordType::PropertyDictionaryStart:
            return footer.Type() == BamlRecordType::PropertyDictionaryEnd;

        case BamlRecordType::PropertyListStart:
            return footer.Type() == BamlRecordType::PropertyListEnd;

        case BamlRecordType::StaticResourceStart:
            return footer.Type() == BamlRecordType::StaticResourceEnd;

        case BamlRecordType::ElementStart:
        case BamlRecordType::NamedElementStart:
            return footer.Type() == BamlRecordType::ElementEnd;

        default:
            return false;
    }
}

std::unique_ptr<BamlBlockNode> BamlNode::Parse(BamlDocument& document) {
    // The C# walks with a `current` block reference plus an explicit stack
    // of parents: every nested header pushes its parent, every footer pops
    // one level (or more, when end records are omitted). The port keeps
    // the root's ownership in rootHolder -- the first block ever created
    // (a leaf-first document throws before any block exists, and after the
    // first header `current` is never null again, so a second top-level
    // header re-parents onto the root instead of replacing it).
    std::unique_ptr<BamlBlockNode> rootHolder;
    BamlBlockNode* current = nullptr;
    std::vector<BamlBlockNode*> stack;

    for (std::size_t i = 0; i < document.Count(); i++) {
        BamlRecord& record = document[i];

        if (IsHeader(record)) {
            BamlBlockNode* prev = current;

            auto node = std::make_unique<BamlBlockNode>();
            node->Header = &record;
            current = node.get();

            if (prev != nullptr) {
                node->Parent = prev;
                prev->Children.push_back(std::move(node));
                stack.push_back(prev);
            } else {
                rootHolder = std::move(node);
            }
        } else if (IsFooter(record)) {
            if (current == nullptr)
                throw std::runtime_error("Unexpected footer.");

            while (!IsMatch(*current->Header, record)) {
                // End record can be omited (sometimes): the C# pops past
                // the still-open blocks WITHOUT assigning their footers,
                // so the intermediate blocks keep a null Footer.
                if (stack.empty())
                    throw std::out_of_range(
                        "Malformed BAML block structure: the end record "
                        "matches no open block.");
                current = stack.back();
                stack.pop_back();
            }
            current->Footer = &record;
            if (!stack.empty()) {
                current = stack.back();
                stack.pop_back();
            }
        } else {
            if (current == nullptr)
                // The C# `current.Children.Add` NullReferenceException.
                throw std::runtime_error(
                    "Object reference not set to an instance of an object.");

            auto node = std::make_unique<BamlRecordNode>(&record);
            node->Parent = current;
            current->Children.push_back(std::move(node));
        }
    }

    // The C# returns `current` here (the root for a well-formed document;
    // the innermost open block for a truncated one -- see BamlNode.hpp for
    // the port's root-return divergence) and asserts an empty stack and a
    // non-empty DocumentStart-headed document in debug builds only.
    return rootHolder;
}

} // namespace ILSpy::BamlDecompiler::Baml
