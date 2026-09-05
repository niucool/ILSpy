// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.BamlDecompiler/BamlConnectionId.cs (Ki, 2015, MIT): the
// three annotation payloads the ConnectionIdRewritePass step trades in --
//  * `BamlConnectionId`: the element annotation the ConnectionIdHandler
//    attaches (the raw connection id the rewrite pass later pairs with the
//    code-behind members the decompiler generates).
//  * `FieldAssignment`: a generated field assignment (a XAML code-behind
//    class field the rewrite pass resolves from the id's event/field
//    registration).
//  * `EventRegistration`: a generated event handler registration.
//
// C#-to-C++ porting decisions:
//  * The C# annotations hold plain GC references; the port's annotations are
//    `std::any` values, so the handlers store OWNING
//    `std::shared_ptr<BamlConnectionId>` instances (the XamlResourceKey
//    annotate-targets convention -- the C# GC roots the payload through the
//    annotation once the context is gone).
//  * `FieldAssignment.Field` is a mutable field holding an `IField` reference
//    -- a non-owning pointer in the port (the compilation owns the member).

#pragma once

#include "Decompiler/TypeSystem/IField.hpp"

#include <cstdint>
#include <string>

namespace ILSpy::BamlDecompiler {

// The C# `internal class BamlConnectionId` (the get-only `Id` property ports
// to a plain field -- the class has no encapsulated invariants).
class BamlConnectionId {
public:
    explicit BamlConnectionId(std::uint32_t id)
        : Id(id)
    {
    }

    std::uint32_t Id;
};

// The C# `internal sealed class FieldAssignment`.
class FieldAssignment {
public:
    const ILSpy::Decompiler::TypeSystem::IField* Field = nullptr;
};

// The C# `internal sealed class EventRegistration`.
class EventRegistration {
public:
    std::string EventName;
    std::string MethodName;
};

} // namespace ILSpy::BamlDecompiler
