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
// (the ilspycmd --il path). This slice carries the instance skeleton, the
// output-shaping helpers every member renderer consumes, the constant and
// parameter renderers the field/method headers embed (WriteConstant,
// WriteParameters), the attribute renderers every member header embeds
// (WriteAttributes with the generic-parameter/parameter attribute blocks),
// and the member renderers (DisassembleMethod with the header, the body
// block, the security declarations, and the generic-parameter list;
// DisassembleField with the data-block arm; DisassembleProperty and
// DisassembleEvent with the accessor lines; DisassembleType with the
// implements list, the layout lines, the .interfaceimpl blocks, and the
// member sections; DisassembleNamespace with the .namespace wrapper block;
// WriteAssemblyHeader and WriteAssemblyReferences with the manifest
// blocks; WriteModuleHeader and WriteModuleContents with the module-level
// chain -- the whole file is now ported apart from the documented
// deferrals (CancellationToken, the AssemblyResolver-driven
// permission-set decode inside WriteSecurityDeclarations, and the
// DecodeCustomAttributeBlobs/WriteDecodedCustomAttributeBlob path; the
// DebugInfo provider pointer delegates through to the ported
// MethodBodyDisassembler member).
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
//    ShowMetadataTokens, ShowMetadataTokensInBase10, ShowRawRVAOffsetAndBytes,
//    and the DebugInfo provider pointer) port as get/set member pairs
//    reading/writing the MethodBodyDisassembler members. `AssemblyResolver`
//    defers with WriteSecurityDeclarations;
//    `EntityProcessor` ports as the caller-owned IEntityProcessor pointer
//    with the Process passthrough (SortByNameProcessor is the ported
//    implementation). The `CancellationToken` defers
//    with the type -- the C# ThrowIfCancellationRequested calls live in the
//    DisassembleType/DisassembleNamespace loops (the member renders are
//    ported; only the cancellation itself defers).
//  * `ExpandMemberDefinitions`/`DecodeCustomAttributeBlobs` are plain
//    auto-properties -- public bool fields.
//  * `WriteBlob(BlobReader)` ports over a byte span (the C# blob reader over
//    the marshalling/constant blobs). The BlobHandle overload (a heap-offset
//    blob read) never needs a port shape of its own: every caller reaches a
//    blob through a read that already materialized its bytes (the custom
//    attribute's ValueBlob, the marshalling/constant blobs), so the span
//    call covers both C# overloads.
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
#include "Decompiler/Disassembler/IEntityProcessor.hpp"
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

    // The C# `public IDebugInfoProvider DebugInfo { get =>
    // methodBodyDisassembler.DebugInfo; set => ... }`: delegate the
    // caller-owned provider pointer to the method body disassembler (null
    // for none).
    const DebugInfo::IDebugInfoProvider* DebugInfo() const;
    void DebugInfo(const DebugInfo::IDebugInfoProvider* value);

    // The C# `public bool ExpandMemberDefinitions { get; set; }`: show
    // method bodies expanded (not collapsed into foldings) by default.
    bool ExpandMemberDefinitions = false;

    // The C# `public bool DecodeCustomAttributeBlobs { get; set; }`
    // (roughly the ildasm /CAVERBAL switch): decode custom attribute blobs
    // instead of dumping them as raw bytes.
    bool DecodeCustomAttributeBlobs = false;

    // The C# `public IEntityProcessor EntityProcessor { get; set; }`: the
    // reordering hook every member/attribute collection is routed through
    // (the test harness plugs in SortByNameProcessor). The processor is
    // caller-owned (the C# reference property over a heap object; the port's
    // raw pointer) and must outlive this object; null (the default) disables
    // reordering -- the Process passthrough below.
    IEntityProcessor* EntityProcessor() const;
    void EntityProcessor(IEntityProcessor* value);

    // The C# private `Process(MetadataFile module,
    // IReadOnlyCollection<THandle> items)` overloads -- seven in C#, typed by
    // the handle kind; the port's raw-token collections collapse them into
    // one tagged method: `EntityProcessor?.Process(module, items) ?? items`.
    // (The port keeps the private members public so the tests can drive them
    // directly.)
    std::vector<std::uint32_t> Process(const Metadata::MetadataFile& module,
        const std::vector<std::uint32_t>& items,
        ProcessedEntityKind kind) const;

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

    // The C# `void WriteAttributes(MetadataFile module,
    // CustomAttributeHandleCollection attributes)`
    // (ReflectionDisassembler.cs lines 1851-1872): one ".custom" line per
    // attribute -- the constructor rendered through EntityHandle.WriteTo at
    // the DEFAULT generic context (the C# passes `default`; an attribute
    // constructor never carries VAR/MVAR), the optional " = " + blob hex
    // dump for a non-nil value, and the metadata-token comment when
    // ShowMetadataTokens is on. The collection routes through Process (the
    // EntityProcessor hook) as CustomAttribute rows. The attribute row ports
    // as the GetCustomAttributeTokens vector; an out-of-range token throws
    // std::out_of_range (the C# metadata.GetCustomAttribute(handle) throws
    // for an invalid handle -- loud rather than wrong).
    // WriteDecodedCustomAttributeBlob (the DecodeCustomAttributeBlobs path,
    // the SecurityDeclarationDecoder custom-attribute-value decode) is not
    // yet ported: with the flag set this throws std::logic_error.
    void WriteAttributes(const Metadata::MetadataFile& module,
        const std::vector<std::uint32_t>& attributeTokens);

    // The C# `void WriteGenericParametersAndAttributes(MetadataFile module,
    // MetadataGenericContext context, GenericParameterHandle handle)`
    // (ReflectionDisassembler.cs lines 1175-1200): the ".param type <name>"
    // block over the generic parameter's own custom attributes and the
    // ".param constraint <name>, <type>" blocks over its constraint rows'
    // attributes -- each block Indents around WriteAttributes. An unknown
    // token throws std::out_of_range (the C# GetGenericParameter(handle)
    // throws -- loud rather than wrong). The handle ports as the raw
    // GenericParam token (a GetGenericParameters row).
    void WriteGenericParametersAndAttributes(
        const Metadata::MetadataFile& module,
        const Metadata::MetadataGenericContext& context,
        std::uint32_t genericParameterToken);

    // The C# `void WriteParameterAttributes(MetadataFile module,
    // ParameterHandle handle)` (ReflectionDisassembler.cs lines 1202-1218):
    // the ".param [N]" line with the optional " = <constant>" tail (the
    // Param row's Constant-table default) and the indented attribute block
    // -- or nothing at all when the row has neither. The Sequence column
    // renders as-is (the seq-0 return row included; the skip is the C# CALLER's
    // WriteParameters rule, not this member's). The handle ports as the
    // GetParameters row (the token drives the Constant/CustomAttribute reads).
    void WriteParameterAttributes(const Metadata::MetadataFile& module,
        const Metadata::ParameterInfo& parameter);

    // The C# `public void DisassembleMethod(MetadataFile module,
    // MethodDefinitionHandle handle)` (ReflectionDisassembler.cs lines
    // 153-161): the ".method" reference, the header, and the body block
    // (the ilspycmd --il member entry). The handle ports as the raw
    // MethodDef token. The module is non-const (the body disassembler's
    // Disassemble takes it that way).
    void DisassembleMethod(Metadata::MetadataFile& module,
        std::uint32_t methodToken);

    // The C# `public void DisassembleMethodHeader(MetadataFile module,
    // MethodDefinitionHandle handle)` (lines 163-170): the ".method"
    // reference and the header, no body block.
    void DisassembleMethodHeader(Metadata::MetadataFile& module,
        std::uint32_t methodToken);

    // The C# `void DisassembleMethodHeaderInternal(MetadataFile module,
    // MethodDefinitionHandle handle, MetadataGenericContext genericContext)`
    // (lines 172-318): the flags line (the visibility WriteEnum + the
    // methodAttributeFlags WriteFlags + the privatescope arm), the
    // pinvokeimpl("dll" as "name" nomangle charset lasterr conv) arm over
    // the ImplMap row, the indented signature line (the instance/explicit
    // prefix, the calling-convention WriteEnum, the return type at Signature
    // syntax, the seq-0 return marshalling descriptor, the escaped name --
    // with the $PST<token> suffix on a compiler-controlled method -- and the
    // generic-parameter list), the "( params )" block through
    // WriteParameters, and the "cil managed <implflags>" tail. A malformed
    // signature blob renders "<bad signature>" and skips the parameter
    // block (the C# BadImageFormatException catch over the decode).
    void DisassembleMethodHeaderInternal(Metadata::MetadataFile& module,
        std::uint32_t methodToken,
        const Metadata::MetadataGenericContext& genericContext);

    // The C# `void DisassembleMethodBlock(MetadataFile module,
    // MethodDefinitionHandle handle, MetadataGenericContext genericContext)`
    // (lines 354-389): the OpenBlock/CloseBlock pair around the attributes,
    // the ".override method" lines (the MethodImpl rows whose body is the
    // method), the generic-parameter and parameter attribute blocks, the
    // ".permissionset" lines, the method body through the
    // MethodBodyDisassembler, and the "end of method <Type>::<name>" close
    // comment.
    void DisassembleMethodBlock(Metadata::MetadataFile& module,
        std::uint32_t methodToken,
        const Metadata::MetadataGenericContext& genericContext);

    // The C# `void WriteSecurityDeclarations(MetadataFile module,
    // DeclarativeSecurityAttributeHandleCollection secDeclProvider)`
    // (lines 390-467): the ".permissionset <action> = " line per DeclSecurity
    // row with the raw blob hex dump. The AssemblyResolver paths (the
    // "bytearray" alternative and the SecurityDeclarationDecoder decode)
    // defer with the resolver type -- with no resolver set the raw dump is
    // the only path (and the ilspycmd --il output's shape: the CLI never
    // sets a resolver). The action spellings are the C# switch's fifteen
    // named values; anything else renders the decimal (the C# enum
    // ToString).
    void WriteSecurityDeclarations(Metadata::MetadataFile& module,
        const std::vector<Metadata::MetadataFile::DeclarativeSecurityInfo>&
            securityDeclarations);

    // The C# `void WriteTypeParameters(ITextOutput output, MetadataFile module,
    // MetadataGenericContext context, GenericParameterHandleCollection p)`
    // (lines 1755-1802): the "<...>" generic-parameter list -- the
    // class/valuetype/byreflike/.ctor flag prefixes over the raw Flags
    // column, the "(constraint, ...)" type list at TypeName syntax, the
    // '-'/'+' variance prefix, and the escaped name per parameter. The
    // collection ports as the GetGenericParameters row vector.
    void WriteTypeParameters(Output::ITextOutput& output,
        const Metadata::MetadataFile& module,
        const Metadata::MetadataGenericContext& context,
        const std::vector<Metadata::GenericParameterInfo>& parameters);

    // The C# `public void DisassembleField(MetadataFile module,
    // FieldDefinitionHandle handle)` (ReflectionDisassembler.cs lines
    // 1288-1343): the ".field" header, the blank-line-terminated attribute
    // block (a field carries no braces/body block -- the .custom lines land
    // at the header's own indentation), and the HasFieldRVA arm: the
    // "// RVA <rva> invalid (not in any section)" comment for an RVA in no
    // section, else the ".data [cil|tls] <prefix>_<rva> = bytearray (...)"
    // block over the field's initial value (GetFieldInitialValue), with the
    // failed-read "// .data ..." comment for the BadImageFormatException
    // shapes. The handle ports as the raw Field token (table 0x04).
    void DisassembleField(Metadata::MetadataFile& module, std::uint32_t fieldToken);

    // The C# `public void DisassembleFieldHeader(MetadataFile module,
    // FieldDefinitionHandle handle)` (lines 1346-1351): the header only,
    // no attribute lines or data block.
    void DisassembleFieldHeader(Metadata::MetadataFile& module,
        std::uint32_t fieldToken);

    // The C# `private char DisassembleFieldHeaderInternal(MetadataFile module,
    // FieldDefinitionHandle handle, MetadataReader metadata, FieldDefinition
    // fieldDefinition)` (lines 1353-1417): the ".field" reference + token
    // comment, the "[offset] " prefix over the FieldLayout row, the
    // visibility WriteEnum + the attribute WriteFlags (the HasDefault/
    // HasFieldMarshal/HasFieldRVA bits masked out -- rendered by the
    // constant/marshal/at-<rva> arms), the field type decoded through the
    // DisassemblerSignatureTypeProvider at the declaring type's context (the
    // marshal(...) descriptor rendered before the type), the escaped name,
    // the " at <prefix>_<rva>" suffix, and the " = <constant>" tail.
    // Returns the section prefix the data block's name carries (the C#
    // sectionPrefix; 'D' for fields without data). A malformed signature blob
    // throws std::logic_error (the field header has NO catch -- the C#
    // BadImageFormatException escapes to the caller, unlike the method
    // header's "<bad signature>").
    char DisassembleFieldHeaderInternal(Metadata::MetadataFile& module,
        std::uint32_t fieldToken);

    // The C# `private char GetRVASectionPrefix(MetadataFile module, int rva)`
    // (lines 1419-1435): the PE section the RVA lands in as the data-name
    // prefix -- 'T' for .tls, 'I' for .text, 'D' for anything else (and for
    // an RVA in no section).
    char GetRVASectionPrefix(const Metadata::MetadataFile& module,
        std::uint32_t rva);

    // The C# `public void DisassembleProperty(MetadataFile module,
    // PropertyDefinitionHandle property)` (ReflectionDisassembler.cs lines
    // 1424-1439): the ".property" header, then the attribute/`.get`/`.set`/
    // `.other` block (an uncollapsed OpenBlock(false) -- hard-coded false,
    // NOT isInType) with the accessor lines rendered through
    // EntityHandle.WriteTo. The handle ports as the raw Property token
    // (table 0x17).
    void DisassembleProperty(Metadata::MetadataFile& module,
        std::uint32_t propertyToken);

    // The C# `public void DisassemblePropertyHeader(MetadataFile module,
    // PropertyDefinitionHandle property)` (lines 1441-1446): the header
    // only, no block. The render ends mid-line after the ')'.
    void DisassemblePropertyHeader(Metadata::MetadataFile& module,
        std::uint32_t propertyToken);

    // The C# `private PropertyAccessors
    // DisassemblePropertyHeaderInternal(MetadataFile module,
    // PropertyDefinitionHandle handle, ...)` (lines 1448-1479): the
    // ".property" reference + token comment, the whole-value WriteFlags over
    // the PropertyAttributes column, the property type decoded as a METHOD
    // signature (the 0x08-header blob through DecodeMethodSignature -- the
    // SRM PropertyDefinition.DecodeSignature) at the declaring type's
    // generic context (the GetAny() accessor's declaring type -- the getter,
    // else the setter, per the ILSpy SRMExtensions), the escaped name, and
    // the indexer parameter block: the accessor's own Param rows sliced to
    // `count` (`count - 1` when there is no getter -- the setter's trailing
    // value parameter drops) through the Indent/Unindent-wrapped
    // WriteParameters. A malformed signature blob throws (the property
    // header has NO catch -- the C# BadImageFormatException escapes to the
    // caller, like the field header); a negative slice throws
    // std::out_of_range (the C# Enumerable.Take ArgumentOutOfRangeException).
    // Returns the accessor set the block renders.
    Metadata::MetadataFile::PropertyAccessorsInfo
    DisassemblePropertyHeaderInternal(Metadata::MetadataFile& module,
        std::uint32_t propertyToken);

    // The C# `void WriteNestedMethod(string keyword, MetadataFile module,
    // MethodDefinitionHandle method)` (lines 1479-1488): the `.get`/`.set`/
    // `.other`/`.addon`/`.removeon`/`.fire` accessor line -- the keyword, a
    // space, the method rendered through EntityHandle.WriteTo at the
    // DEFAULT generic context, and the line break. A nil method writes
    // nothing at all (the C# IsNil early-out).
    void WriteNestedMethod(const char* keyword, Metadata::MetadataFile& module,
        std::uint32_t methodToken);

    // The C# `public void DisassembleEvent(MetadataFile module,
    // EventDefinitionHandle handle)` (ReflectionDisassembler.cs lines
    // 1497-1512): the ".event" header, then the attribute/`.addon`/
    // `.removeon`/`.fire`/`.other` block. The handle ports as the raw Event
    // token (table 0x14).
    void DisassembleEvent(Metadata::MetadataFile& module,
        std::uint32_t eventToken);

    // The C# `public void DisassembleEventHeader(MetadataFile module,
    // EventDefinitionHandle handle)` (lines 1514-1519): the header only.
    void DisassembleEventHeader(Metadata::MetadataFile& module,
        std::uint32_t eventToken);

    // The C# `private void DisassembleEventHeaderInternal(MetadataFile
    // module, EventDefinitionHandle handle, EventDefinition eventDefinition,
    // EventAccessors accessors)` (lines 1521-1562): the ".event" reference +
    // token comment, the whole-value WriteFlags over the EventAttributes
    // column, then the event's delegate type rendered directly through the
    // signature provider (the TypeDef/TypeRef/TypeSpec arms of the
    // `eventDefinition.Type` switch -- the TypeSpec arm at the declaring
    // type's generic context; anything else is the C#
    // BadImageFormatException, ports to std::out_of_range) at TypeName
    // syntax, and the escaped name. The declaring type comes from the
    // adder, else the remover, else the raiser (the C# GetAny order -- it
    // only feeds the TypeSpec context).
    void DisassembleEventHeaderInternal(Metadata::MetadataFile& module,
        std::uint32_t eventToken,
        const Metadata::MetadataFile::EventAccessorsInfo& accessors);

    // The C# `public void DisassembleType(MetadataFile module,
    // TypeDefinitionHandle type)` (ReflectionDisassembler.cs lines
    // 1598-1717): the whole type render -- the header, the `implements`
    // list over the processed InterfaceImpl rows, the `{` block with the
    // type's own attributes, security declarations, generic-parameter
    // attribute blocks, the `.pack`/`.size` layout lines, the
    // `.interfaceimpl` blocks for attributed interface rows, then the
    // "// Nested Types"/"// Fields"/"// Methods"/"// Events"/"//
    // Properties" sections (each routed through the EntityProcessor)
    // and the "end of class <name>" close comment (the escaped short
    // name for a nested type, the reflection full name for a top-level
    // one). isInType flips true for the body (the member blocks fold); a
    // nested type recurses through here with the OUTER isInType still
    // set. The handle ports as the raw TypeDef token (table 0x02). The C#
    // `cancellationToken.ThrowIfCancellationRequested()` calls in the
    // member loops defer with the cancellation-token type (the CLI never
    // cancels a single-type render).
    void DisassembleType(Metadata::MetadataFile& module, std::uint32_t typeToken);

    // The C# `public void DisassembleTypeHeader(MetadataFile module,
    // TypeDefinitionHandle type)` (lines 1719-1724): the header only, no
    // body.
    void DisassembleTypeHeader(Metadata::MetadataFile& module,
        std::uint32_t typeToken);

    // The C# `private void DisassembleTypeHeaderInternal(MetadataFile
    // module, TypeDefinitionHandle handle, TypeDefinition typeDefinition,
    // MetadataGenericContext genericContext)` (lines 1726-1753): the
    // ".class" reference + token comment, the "interface " prefix over
    // the ClassSemanticsMask bit, the visibility/layout/string-format
    // WriteEnum splits, the remaining-flags WriteFlags, the type name
    // (the full IL name for a top-level type, the escaped short name for
    // a nested one) with the generic-parameter list, the fold start, and
    // the indented `extends <base>` line (nothing for a nil base). The
    // C# typeDefinition parameter (row data the caller already had)
    // drops away -- the port reads the row by token.
    void DisassembleTypeHeaderInternal(Metadata::MetadataFile& module,
        std::uint32_t typeToken,
        const Metadata::MetadataGenericContext& genericContext);

    // The C# `public void DisassembleNamespace(string nameSpace, MetadataFile
    // module, IEnumerable<TypeDefinitionHandle> types)`
    // (ReflectionDisassembler.cs lines 2034-2054): the ".namespace <name>"
    // wrapper block around the type renders (the ILSpy app's per-namespace
    // view -- the CLI's WriteModuleContents path never wraps). An empty/null
    // namespace string renders the types bare at top level. isInType flips
    // true for the walk and restores only in the non-empty-namespace arm (the
    // C# restores inside the same `if` -- an empty-namespace call LEAVES
    // isInType set; preserved faithfully). The type collection ports as the
    // raw TypeDef token vector (the caller's namespace grouping; the
    // cancellationToken call defers with the token type). Each type render is
    // followed by a blank line (the loop's WriteLine), including the last.
    void DisassembleNamespace(const std::string& nameSpace,
        Metadata::MetadataFile& module,
        const std::vector<std::uint32_t>& typeTokens);

    // The C# `public void WriteAssemblyHeader(MetadataFile module)` (lines
    // 2056-2091): the ".assembly <name>" manifest block -- the optional
    // "windowsruntime " prefix (the AssemblyAttributes WindowsRuntime bit),
    // the assembly's own custom attributes and security declarations, the
    // ".publickey = (...)" blob for a non-nil PublicKey column, the
    // ".hash algorithm 0x..." line with the SHA1 comment, and the
    // ".ver major:minor:build:revision" line. A no-op for a file that is not
    // an assembly (the C# `!metadata.IsAssembly` early return -- a netmodule).
    void WriteAssemblyHeader(Metadata::MetadataFile& module);

    // The C# `public void WriteAssemblyReferences(MetadataReader metadata)`
    // (lines 2093-2120): the ".module extern <name>" line per ModuleRef row,
    // then the ".assembly extern <name>" block per AssemblyRef row (the
    // optional "windowsruntime " prefix, the ".publickeytoken = (...)" blob
    // for a non-nil column, and the ".ver" line). The C# MetadataReader
    // parameter ports as the MetadataFile (the reader is the file's metadata).
    void WriteAssemblyReferences(const Metadata::MetadataFile& module);

    // The C# `public void WriteModuleHeader(MetadataFile module,
    // bool skipMVID = false)` (ReflectionDisassembler.cs lines 2119-2197):
    // the ".class extern" block per ExportedType row -- the optional
    // "forwarder " prefix (the SRM IsForwarder property fuses the
    // ForwarderType flag with an AssemblyReference implementation), the
    // escaped namespace.name header (the namespace omitted for a nil
    // column), and the implementation body: the ".file <name>" + optional
    // ".class 0x..." TypeDefId lines for a File implementation, the
    // ".class extern " + declaring-chain walk for a nested ExportedType
    // implementation (each declaring row's namespace.name written with NO
    // separator between chain links -- faithful to the C# while-loop), or
    // the ".assembly extern <name>" line for an AssemblyRef; anything else
    // throws (the C# BadImageFormatException). Then the ".module <name>"
    // line (NOT escaped), the "// MVID: {...}" line in the "B" brace form
    // uppercased (skipped when skipMVID), the five PE lines when the image
    // is a PE (.imagebase/.file alignment/.stackreserve/.subsystem/.
    // corflags with their .NET enum spellings), and the module's own custom
    // attributes over the ModuleDefinition token.
    void WriteModuleHeader(Metadata::MetadataFile& module,
        bool skipMVID = false);

    // The C# `public void WriteModuleContents(MetadataFile module)`
    // (lines 2199-2206): every top-level type rendered through
    // DisassembleType (routed through the EntityProcessor as TypeDef rows),
    // each followed by a blank line -- the CLI's whole-module -il path.
    void WriteModuleContents(Metadata::MetadataFile& module);

private:
    Output::ITextOutput& output_;

    // The C# `IEntityProcessor EntityProcessor` auto-property backing field
    // (null until set -- never dereferenced by this class).
    IEntityProcessor* entityProcessor_ = nullptr;

    // The fresh MethodBodyDisassembler the chaining constructor owns (null
    // when the caller supplied one).
    std::unique_ptr<MethodBodyDisassembler> ownedMethodBodyDisassembler_;

    // The C# `MethodBodyDisassembler methodBodyDisassembler` field --
    // never null.
    MethodBodyDisassembler* methodBodyDisassembler_;

    // The C# `bool isInType` -- whether we are currently disassembling a
    // whole type (drives the defaultCollapsed folding of member blocks).
    // Private: the DisassembleType and DisassembleNamespace walks flip it.
    bool isInType_ = false;
};

}  // namespace ILSpy::Decompiler::Disassembler
