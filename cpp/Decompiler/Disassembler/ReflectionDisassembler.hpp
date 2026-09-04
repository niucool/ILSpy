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

// Port of ICSharpCode.Decompiler/Disassembler/ReflectionDisassembler.cs: the
// disassembler class that renders whole modules/types/members as IL text
// (the ilspycmd --il path). This slice carries the instance skeleton and
// the output-shaping helpers every member renderer consumes, plus the
// constant and parameter renderers the field/method headers embed
// (WriteConstant, WriteParameters); the member renderers themselves
// (DisassembleMethod/Field/Property/Event/Type, WriteAttributes, the
// module headers) land with their metadata reads in later slices.
//
// C#-to-C++ porting decisions:
//  * The C# field pair `output`/`cancellationToken`/`isInType`/
//    `methodBodyDisassembler` ports as the constructor-bound output
//    reference, a private isInType flag (driven by DisassembleType/
//    DisassembleNamespace), and the MethodBodyDisassembler pointer -- the
//    C# primary constructor takes the disassembler object; the chaining
//    constructor news one up over the same output, so the port's chaining
//    constructor owns a fresh one in the unique_ptr.
//  * The delegating properties (DetectControlStructure, ShowSequencePoints,
//    ShowMetadataTokens, ShowMetadataTokensInBase10, ShowRawRVAOffsetAndBytes)
//    port as get/set member pairs reading/writing the MethodBodyDisassembler
//    flags. `DebugInfo` (IDebugInfoProvider) defers with the provider type;
//    `AssemblyResolver` defers with WriteSecurityDeclarations;
//    `EntityProcessor` defers with the Process overloads (IEntityProcessor/
//    SortByNameProcessor, the next slice). The `CancellationToken` defers
//    with the type -- the C# ThrowIfCancellationRequested calls live in the
//    DisassembleType/DisassembleNamespace loops (not yet ported).
//  * `ExpandMemberDefinitions`/`DecodeCustomAttributeBlobs` are plain
//    auto-properties -- public bool fields.
//  * `WriteBlob(BlobReader)` ports over a byte span (the C# blob reader over
//    the marshalling/constant blobs). The BlobHandle overload (a heap-offset
//    blob read) defers with WriteAssemblyHeader, its first caller.
//  * `OpenBlock`/`CloseBlock` port faithfully, folding markers included (the
//    PlainTextOutput no-ops); `CloseBlock(string comment = null)` keeps the
//    null-vs-empty distinction through a `const char*` default of nullptr.
//  * `WriteMarshalInfo(BlobReader)` takes the blob (the C# caller passes a
//    fresh reader over the descriptor blob) and `ref`s it into
//    WriteNativeType; the port takes (base, size) and drives the
//    (base, size, pos) cursor, WriteNativeType taking the cursor by
//    reference (the array/fixed-array recursions share it) -- the
//    ILParser/BlobReader cursor convention.
//  * The C# BadImageFormatException paths of a truncated/empty native-type
//    blob port to std::out_of_range (the ILParser truncated-operand
//    convention).
//  * The private members are the port's public members so the tests can
//    drive them directly (the MethodBodyDisassembler convention).

#pragma once

#include "Decompiler/Disassembler/MethodBodyDisassembler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::Disassembler {

class ReflectionDisassembler {
public:
    // The C# `public ReflectionDisassembler(ITextOutput output,
    // MethodBodyDisassembler methodBodyDisassembler, CancellationToken
    // cancellationToken)` -- the primary constructor. The disassembler must
    // outlive this object (the C# reference field).
    ReflectionDisassembler(Output::ITextOutput& output,
        MethodBodyDisassembler& methodBodyDisassembler);

    // The C# `public ReflectionDisassembler(ITextOutput output,
    // CancellationToken cancellationToken)` -- the chaining constructor that
    // owns a fresh `new MethodBodyDisassembler(output, cancellationToken)`
    // over the same output.
    explicit ReflectionDisassembler(Output::ITextOutput& output);

    ReflectionDisassembler(const ReflectionDisassembler&) = delete;
    ReflectionDisassembler& operator=(const ReflectionDisassembler&) = delete;

    // The C# `public bool DetectControlStructure { get =>
    // methodBodyDisassembler.DetectControlStructure; set => ... }` (and the
    // four show-flag siblings): delegate to the method body disassembler.
    bool DetectControlStructure() const;
    void DetectControlStructure(bool value);
    bool ShowSequencePoints() const;
    void ShowSequencePoints(bool value);
    bool ShowMetadataTokens() const;
    void ShowMetadataTokens(bool value);
    bool ShowMetadataTokensInBase10() const;
    void ShowMetadataTokensInBase10(bool value);
    bool ShowRawRVAOffsetAndBytes() const;
    void ShowRawRVAOffsetAndBytes(bool value);

    // The C# `public bool ExpandMemberDefinitions { get; set; }`: show
    // method bodies expanded (not collapsed into foldings) by default.
    bool ExpandMemberDefinitions = false;

    // The C# `public bool DecodeCustomAttributeBlobs { get; set; }`
    // (roughly the ildasm /CAVERBAL switch): decode custom attribute blobs
    // instead of dumping them as raw bytes.
    bool DecodeCustomAttributeBlobs = false;

    // The C# `internal static void WriteMetadataToken(ITextOutput output,
    // MetadataFile module, Handle? handle, int metadataToken, bool spaceAfter,
    // bool spaceBefore, bool showMetadataTokens, bool base10)` (see the
    // .cpp).
    static void WriteMetadataToken(Output::ITextOutput& output,
        const Metadata::MetadataFile& module, std::uint32_t handleToken,
        std::uint32_t metadataToken, bool spaceAfter, bool spaceBefore,
        bool showMetadataTokens, bool base10);

    // The C# `void WriteBlob(BlobReader reader)`: the `(xx xx ...)` hex dump
    // -- "(" Indent, one lowercase two-digit hex byte per position with a
    // space before every byte except the line-start ones (a newline before
    // each 16th byte that is not the last), WriteLine, Unindent, ")".
    void WriteBlob(const std::uint8_t* base, std::size_t size);

    // The C# `void OpenBlock(bool defaultCollapsed)`: the folding start
    // (defaultCollapsed ANDed with !ExpandMemberDefinitions), a blank line,
    // "{" and Indent.
    void OpenBlock(bool defaultCollapsed);

    // The C# `void CloseBlock(string comment = null)`: Unindent, "}", the
    // optional " // comment" suffix, the folding end, and the line break.
    // A `const char*` nullptr is the C# null comment.
    void CloseBlock(const char* comment = nullptr);

    // The C# `void WriteMarshalInfo(BlobReader marshalInfo)`: "marshal(" +
    // the native type + ") " over the marshalling-descriptor blob.
    void WriteMarshalInfo(const std::uint8_t* base, std::size_t size);

    // The C# `void WriteNativeType(ref BlobReader blob)`: the II.23.4
    // native-type walk -- the simple spellings, the array
    // (nested type + the three size fields), fixed sysstring, safearray
    // (the element type table), fixed array (size + nested type),
    // the custom marshaler (the four SerStrings + the GUID), error, and
    // the decimal fallback. The cursor is advanced in place (the recursions
    // share it); reading past the end throws std::out_of_range.
    void WriteNativeType(const std::uint8_t* base, std::size_t size,
        std::size_t& pos);

    // The C# `void WriteConstant(MetadataReader metadata, Constant constant)`
    // (ReflectionDisassembler.cs lines 1220-1268): the II.23.2 constant value
    // render -- "nullref" for the NullReference code (the raw 0x12 slot --
    // the blob is not read), the double-quoted escaped literal for String
    // (no type wrapper), and `<il-type>(<value>)` for the numeric codes with
    // the NaN/infinity float/double IEEE bit patterns, the invalid-typecode
    // comment for everything else (and for a blob too short for its code).
    // The C# MetadataReader argument drops away: the port's ConstantInfo
    // carries the materialized value blob (the blob-reader-at-a-handle
    // convention).
    void WriteConstant(const Metadata::ConstantInfo& constant);

    // The C# `void WriteParameters(MetadataReader metadata,
    // IEnumerable<ParameterHandle> parameters,
    // MethodSignature<Action<ILNameSyntax>> signature)`
    // (ReflectionDisassembler.cs lines 1107-1160): the "( params )" list --
    // the Sequence-column walk that skips the return row, fills sequence gaps
    // with the unnamed "''" reference, renders the [in]/[out]/[opt] prefixes,
    // the type at ILNameSyntax.Signature, the marshalling descriptor, and the
    // escaped-name local reference ("param_N" with the instance `this` offset),
    // and appends the unnamed remaining-signature parameters. A signature
    // shorter than the Param rows throws std::out_of_range (the C# IndexOutOfRange
    // on ParameterTypes[i] -- loud rather than wrong). The parameter collection
    // ports as the GetParameters row vector (the vararg callers slice it).
    void WriteParameters(const std::vector<Metadata::ParameterInfo>& parameters,
        const Metadata::MethodSignatureT& signature);

private:
    Output::ITextOutput& output_;

    // The fresh MethodBodyDisassembler the chaining constructor owns (null
    // when the caller supplied one).
    std::unique_ptr<MethodBodyDisassembler> ownedMethodBodyDisassembler_;

    // The C# `MethodBodyDisassembler methodBodyDisassembler` field --
    // never null.
    MethodBodyDisassembler* methodBodyDisassembler_;

    // The C# `bool isInType` -- whether we are currently disassembling a
    // whole type (drives the defaultCollapsed folding of member blocks).
    // Private: only the DisassembleType/DisassembleNamespace walks (later
    // slices) flip it.
    bool isInType_ = false;
};

}  // namespace ILSpy::Decompiler::Disassembler
