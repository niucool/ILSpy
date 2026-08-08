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

// A decoded CLI method body: the C++ port's stand-in for
// System.Reflection.Metadata.MethodBodyBlock. Phase 1 fills in the gap the
// vendored microsoft/winmd reader leaves (winmd targets metadata-only WinMD
// files, so it does not read method bodies). Decoded per ECMA-335 II.25.4:
//   * tiny header  (1 byte):  (codeSize << 2) | 0x02
//   * fat header   (12 bytes): Flags|Size, MaxStack, CodeSize, LocalVarSigTok
//   * EH sections  (II.25.4.3): small/fat section header + 24-byte clauses
//
// The IL bytes and handler clauses are views into the owning PE image; the
// MethodBody holds a shared pointer to that image so the views stay valid
// after the MetadataFile is destroyed.

#pragma once

#include "Decompiler/Util/Span.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

enum class ExceptionHandlerKind : std::uint32_t {
    Catch  = 0,
    Filter = 1,
    Finally = 2,
    Fault  = 4,
};

struct ExceptionHandlerClause {
    ExceptionHandlerKind Kind;
    std::uint32_t TryOffset;
    std::uint32_t TryLength;
    std::uint32_t HandlerOffset;
    std::uint32_t HandlerLength;
    // Catch: the TypeDefOrRef token of the caught type.
    // Filter: the IL offset of the filter block.
    // Finally/Fault: unused (0).
    std::uint32_t ClassTokenOrFilterOffset;
};

class MethodBody {
public:
    MethodBody() = default;

    bool IsValid() const noexcept { return held_ != nullptr; }
    bool IsFat() const noexcept { return isFat_; }

    std::uint32_t MaxStack() const noexcept { return maxStack_; }
    std::uint32_t CodeSize() const noexcept { return codeSize_; }
    // StandAloneSig token of the local-variable signature, or 0 if none.
    std::uint32_t LocalVarSigToken() const noexcept { return localVarSigTok_; }

    // The IL instruction bytes (length == CodeSize()).
    Util::Span<const std::uint8_t> IL() const noexcept { return il_; }
    // The exception-handler clauses (empty unless the fat header set MoreSects).
    Util::Span<const ExceptionHandlerClause> Handlers() const noexcept { return handlers_; }

private:
    // Backing storage for the views: a shared reference to the PE image. Kept
    // alive for as long as any MethodBody referencing it exists.
    std::shared_ptr<const std::vector<std::uint8_t>> held_;
    // Owned copies of decoded clauses (the section bytes are not kept; clauses
    // are materialised into a vector so Handlers() is always a stable view).
    std::shared_ptr<std::vector<ExceptionHandlerClause>> handlerStorage_;
    Util::Span<const std::uint8_t> il_;
    Util::Span<const ExceptionHandlerClause> handlers_;
    std::uint32_t maxStack_ = 0;
    std::uint32_t codeSize_ = 0;
    std::uint32_t localVarSigTok_ = 0;
    bool isFat_ = false;

    friend class MethodBodyReader;
    // Set by the reader after decoding into this instance.
    void Adopt(std::shared_ptr<const std::vector<std::uint8_t>> image,
               std::shared_ptr<std::vector<ExceptionHandlerClause>> handlers,
               Util::Span<const std::uint8_t> il,
               std::uint32_t maxStack, std::uint32_t codeSize,
               std::uint32_t localVarSigTok, bool isFat);
};

} // namespace ILSpy::Decompiler::Metadata
