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

#include "Decompiler/Metadata/MethodBody.hpp"

#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// Defined out-of-line because MethodBodyReader (the sole caller, in
// MethodBodyReader.hpp) is the friend declared in MethodBody.hpp. Keeping the
// state-setter here lets the public header stay free of winmd/<windows.h>.
void MethodBody::Adopt(std::shared_ptr<const std::vector<std::uint8_t>> image,
                       std::shared_ptr<std::vector<ExceptionHandlerClause>> handlers,
                       Util::Span<const std::uint8_t> il,
                       std::uint32_t maxStack, std::uint32_t codeSize,
                       std::uint32_t localVarSigTok, bool isFat) {
    held_ = std::move(image);
    handlerStorage_ = std::move(handlers);
    il_ = il;
    if (handlerStorage_) {
        handlers_ = Util::Span<const ExceptionHandlerClause>(
            handlerStorage_->data(), handlerStorage_->size());
    }
    maxStack_ = maxStack;
    codeSize_ = codeSize;
    localVarSigTok_ = localVarSigTok;
    isFat_ = isFat;
}

} // namespace ILSpy::Decompiler::Metadata
