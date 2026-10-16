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

// Port of ICSharpCode.Decompiler/DebugInfo/KnownGuids.cs: the well-known
// GUIDs of the portable-PDB format -- the language and hash-algorithm GUIDs
// of the Document table and the Roslyn CustomDebugInformation kinds
// (https://github.com/dotnet/roslyn/blob/main/src/Dependencies/CodeAnalysis.Debugging/PortableCustomDebugInfoKinds.cs).
//
// C#-to-C++ porting decision: each Guid ports as the 16 bytes of the
// canonical little-endian binary form -- exactly what the metadata #GUID
// heap stores and what MetadataReader.GetGuid hands back -- so a kind
// comparison is a plain byte-array comparison, the equivalent of the C#
// `Guid ==` (the .NET Guid stores these same canonical bytes; comparing
// them is value equality). The canonical dashed string form is kept in the
// comment on each constant.

#pragma once

#include <array>
#include <cstdint>

namespace ILSpy::Decompiler::DebugInfo {

namespace KnownGuids {

// "3f5162f8-07c6-11d3-9053-00c04fa302a1" -- the C# language GUID.
inline constexpr std::array<std::uint8_t, 16> CSharpLanguageGuid = {
    0xF8, 0x62, 0x51, 0x3F, 0xC6, 0x07, 0xD3, 0x11,
    0x90, 0x53, 0x00, 0xC0, 0x4F, 0xA3, 0x02, 0xA1,
};
// "3a12d0b8-c26c-11d0-b442-00a0244a1dd2" -- the VB language GUID.
inline constexpr std::array<std::uint8_t, 16> VBLanguageGuid = {
    0xB8, 0xD0, 0x12, 0x3A, 0x6C, 0xC2, 0xD0, 0x11,
    0xB4, 0x42, 0x00, 0xA0, 0x24, 0x4A, 0x1D, 0xD2,
};
// "ab4f38c9-b6e6-43ba-be3b-58080b2ccce3" -- the F# language GUID.
inline constexpr std::array<std::uint8_t, 16> FSharpLanguageGuid = {
    0xC9, 0x38, 0x4F, 0xAB, 0xE6, 0xB6, 0xBA, 0x43,
    0xBE, 0x3B, 0x58, 0x08, 0x0B, 0x2C, 0xCC, 0xE3,
};

// The Roslyn CustomDebugInformation kinds.
// "6da9a61e-f8c7-4874-be62-68bc5630df71"
inline constexpr std::array<std::uint8_t, 16> StateMachineHoistedLocalScopes = {
    0x1E, 0xA6, 0xA9, 0x6D, 0xC7, 0xF8, 0x74, 0x48,
    0xBE, 0x62, 0x68, 0xBC, 0x56, 0x30, 0xDF, 0x71,
};
// "83c563c4-b4f3-47d5-b824-ba5441477ea8"
inline constexpr std::array<std::uint8_t, 16> DynamicLocalVariables = {
    0xC4, 0x63, 0xC5, 0x83, 0xF3, 0xB4, 0xD5, 0x47,
    0xB8, 0x24, 0xBA, 0x54, 0x41, 0x47, 0x7E, 0xA8,
};
// "58b2eab6-209f-4e4e-a22c-b2d0f910c782"
inline constexpr std::array<std::uint8_t, 16> DefaultNamespaces = {
    0xB6, 0xEA, 0xB2, 0x58, 0x9F, 0x20, 0x4E, 0x4E,
    0xA2, 0x2C, 0xB2, 0xD0, 0xF9, 0x10, 0xC7, 0x82,
};
// "755f52a8-91c5-45be-b4b8-209571e552bd"
inline constexpr std::array<std::uint8_t, 16> EditAndContinueLocalSlotMap = {
    0xA8, 0x52, 0x5F, 0x75, 0xC5, 0x91, 0xBE, 0x45,
    0xB4, 0xB8, 0x20, 0x95, 0x71, 0xE5, 0x52, 0xBD,
};
// "a643004c-0240-496f-a783-30d64f4979de"
inline constexpr std::array<std::uint8_t, 16> EditAndContinueLambdaAndClosureMap = {
    0x4C, 0x00, 0x43, 0xA6, 0x40, 0x02, 0x6F, 0x49,
    0xA7, 0x83, 0x30, 0xD6, 0x4F, 0x49, 0x79, 0xDE,
};
// "8b78cd68-2ede-420b-980b-e15884b8aaa3"
inline constexpr std::array<std::uint8_t, 16> EncStateMachineStateMap = {
    0x68, 0xCD, 0x78, 0x8B, 0xDE, 0x2E, 0x0B, 0x42,
    0x98, 0x0B, 0xE1, 0x58, 0x84, 0xB8, 0xAA, 0xA3,
};
// "0e8a571b-6926-466e-b4ad-8ab04611f5fe"
inline constexpr std::array<std::uint8_t, 16> EmbeddedSource = {
    0x1B, 0x57, 0x8A, 0x0E, 0x26, 0x69, 0x6E, 0x46,
    0xB4, 0xAD, 0x8A, 0xB0, 0x46, 0x11, 0xF5, 0xFE,
};
// "cc110556-a091-4d38-9fec-25ab9a351a6a"
inline constexpr std::array<std::uint8_t, 16> SourceLink = {
    0x56, 0x05, 0x11, 0xCC, 0x91, 0xA0, 0x38, 0x4D,
    0x9F, 0xEC, 0x25, 0xAB, 0x9A, 0x35, 0x1A, 0x6A,
};
// "54fd2ac5-e925-401a-9c2a-f94f171072f8"
inline constexpr std::array<std::uint8_t, 16> MethodSteppingInformation = {
    0xC5, 0x2A, 0xFD, 0x54, 0x25, 0xE9, 0x1A, 0x40,
    0x9C, 0x2A, 0xF9, 0x4F, 0x17, 0x10, 0x72, 0xF8,
};
// "b5feec05-8cd0-4a83-96da-466284bb4bd8"
inline constexpr std::array<std::uint8_t, 16> CompilationOptions = {
    0x05, 0xEC, 0xFE, 0xB5, 0xD0, 0x8C, 0x83, 0x4A,
    0x96, 0xDA, 0x46, 0x62, 0x84, 0xBB, 0x4B, 0xD8,
};
// "7e4d4708-096e-4c5c-aeda-cb10ba6a740d"
inline constexpr std::array<std::uint8_t, 16> CompilationMetadataReferences = {
    0x08, 0x47, 0x4D, 0x7E, 0x6E, 0x09, 0x5C, 0x4C,
    0xAE, 0xDA, 0xCB, 0x10, 0xBA, 0x6A, 0x74, 0x0D,
};
// "ed9fdf71-8879-4747-8ed3-fe5ede3ce710"
inline constexpr std::array<std::uint8_t, 16> TupleElementNames = {
    0x71, 0xDF, 0x9F, 0xED, 0x79, 0x88, 0x47, 0x47,
    0x8E, 0xD3, 0xFE, 0x5E, 0xDE, 0x3C, 0xE7, 0x10,
};
// "932e74bc-dba9-4478-8d46-0f32a7bab3d3"
inline constexpr std::array<std::uint8_t, 16> TypeDefinitionDocuments = {
    0xBC, 0x74, 0x2E, 0x93, 0xA9, 0xDB, 0x78, 0x47,
    0x8D, 0x46, 0x0F, 0x32, 0xA7, 0xBA, 0xB3, 0xD3,
};

// The Document-table hash algorithms.
// "ff1816ec-aa5e-4d10-87f7-6f4963833460" -- SHA-1.
inline constexpr std::array<std::uint8_t, 16> HashAlgorithmSHA1 = {
    0xEC, 0x16, 0x18, 0xFF, 0x5E, 0xAA, 0x10, 0x4D,
    0x87, 0xF7, 0x6F, 0x49, 0x63, 0x83, 0x34, 0x60,
};
// "8829d00f-11b8-4213-878b-770e8597ac16" -- SHA-256.
inline constexpr std::array<std::uint8_t, 16> HashAlgorithmSHA256 = {
    0x0F, 0xD0, 0x29, 0x88, 0xB8, 0x11, 0x13, 0x42,
    0x87, 0x8B, 0x77, 0x0E, 0x85, 0x97, 0xAC, 0x16,
};

}  // namespace KnownGuids

}  // namespace ILSpy::Decompiler::DebugInfo
