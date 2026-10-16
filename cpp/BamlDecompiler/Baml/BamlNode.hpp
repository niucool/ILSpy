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

// Port of ICSharpCode.BamlDecompiler/Baml/BamlNode.cs (Ki, 2015, MIT): the
// decompose step between the flat record list and the XAML handlers -- the
// header/footer predicates, the header-to-footer match table, and the
// Parse stack machine that folds the flat BamlDocument into the block tree
// (BamlBlockNode holding nested children, BamlRecordNode the leaf records).
//
// C#-to-C++ porting decisions:
//  * Ownership: the C# tree is GC-owned; the port's parents own their
//    children (the vector of unique_ptr) and Parse returns the owning root.
//    The nodes' records are NON-OWNING pointers into the caller's
//    BamlDocument (the C# object references): the document must outlive the
//    tree, which the future XamlContext upholds by holding both.
//  * The C# Parse returns `current`, which for a well-formed document is
//    the root block (the final DocumentEnd leaves it current; the stack is
//    empty). For a TRUNCATED document (a header with no matching footer
//    before the end of the record list) the C# returns the innermost open
//    block instead -- a quirk the port cannot reproduce: a unique_ptr tree
//    has one owner, so the port always returns the root (which owns the
//    innermost block as a descendant; the Parent chain stays intact).
//    Only malformed input can observe the difference.
//  * An empty document: the C# Debug.Assert (document.Count > 0 && the
//    first record is DocumentStart) is compiled out of the release
//    assembly the CLI ships, so the C# returns null; the port returns a
//    null unique_ptr (the same release behavior).
//  * `throw new Exception("Unexpected footer.")` maps to std::runtime_error
//    (the plain-Exception family mapping), and the leaf-record-before-any-
//    header arm (the C# `current.Children.Add` NullReferenceException)
//    maps to std::runtime_error carrying the .NET message ("Object
//    reference not set to an instance of an object.").
//  * The unmatched-footer hardening: the C# footer arm pops the parent
//    stack in a `while (!IsMatch(...))` loop that spins FOREVER when the
//    stack empties without a match (a malformed document -- e.g.
//    [DocumentStart, PropertyListEnd]). The port throws instead (the
//    crafted-input hardening convention of BamlDeferReader: a crafted
//    resource fails with a catchable exception instead of hanging).
//  * The C# `object Annotation` (the handler-set per-node payload, e.g.
//    the XamlElement a handler attaches) ports to std::any.
//  * The CancellationToken is dropped (the established deferral; every
//    ported caller passes CancellationToken.None).

#pragma once

#include "BamlDecompiler/Baml/BamlRecords.hpp"

#include <any>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ILSpy::BamlDecompiler::Baml {

class BamlBlockNode;

// The C# `BamlNode` abstract base.
class BamlNode {
public:
    virtual ~BamlNode() = default;

    // The C# `BamlBlockNode Parent` -- the non-owning back-pointer (null
    // for the root; set for every Parse-created child).
    BamlBlockNode* Parent = nullptr;

    virtual BamlRecordType Type() const = 0;

    // The C# `object Annotation`.
    std::any Annotation;

    // The C# `BamlRecord Record` -- the node's record (a block node's
    // header; a record node's own record). A non-owning pointer into the
    // caller's document (the C# null for a hand-constructed headerless
    // block node).
    virtual BamlRecord* Record() const = 0;

    // The C# `IsHeader`: the ten block-opening record types.
    static bool IsHeader(const BamlRecord& record);

    // The C# `IsFooter`: the ten block-closing record types.
    static bool IsFooter(const BamlRecord& record);

    // The C# `IsMatch`: the header-to-footer pairing table (both
    // ElementStart and NamedElementStart match ElementEnd; the other
    // headers match their own *End record).
    static bool IsMatch(const BamlRecord& header, const BamlRecord& footer);

    // The C# `Parse(BamlDocument, CancellationToken)`: folds the flat
    // record list into the block tree. Throws std::runtime_error
    // ("Unexpected footer.") for a footer before any header,
    // std::runtime_error ("Object reference not set to an instance of an
    // object.") for a leaf record before any header (the C#
    // NullReferenceException), and std::out_of_range for an end record
    // that matches no open block once the parent stack is empty (the C#
    // infinite loop; the hardening divergence above).
    static std::unique_ptr<BamlBlockNode> Parse(BamlDocument& document);
};

// The C# `BamlRecordNode`: a leaf record (no block structure).
class BamlRecordNode final : public BamlNode {
public:
    explicit BamlRecordNode(BamlRecord* record)
        : record_(record) {}

    BamlRecordType Type() const override { return record_->Type(); }
    BamlRecord* Record() const override { return record_; }

private:
    BamlRecord* record_; // non-owning (the caller's document)
};

// The C# `BamlBlockNode`: a header record, the nested children, and the
// matching footer (null while an end record stays omitted -- the C#
// "End record can be omited (sometimes)" path).
class BamlBlockNode final : public BamlNode {
public:
    // The block's header (non-owning; assigned by Parse before the node is
    // used, so a Parse-created block never lacks one).
    BamlRecord* Header = nullptr;

    // The C# `IList<BamlNode> Children` -- the owning child list.
    std::vector<std::unique_ptr<BamlNode>> Children;

    // The block's footer (non-owning, null when omitted or still open).
    BamlRecord* Footer = nullptr;

    BamlRecordType Type() const override {
        // The C# `Header.Type` NREs for a headerless block; only a
        // hand-constructed node can lack a header.
        if (Header == nullptr)
            throw std::runtime_error(
                "Object reference not set to an instance of an object.");
        return Header->Type();
    }

    BamlRecord* Record() const override { return Header; }
};

} // namespace ILSpy::BamlDecompiler::Baml
