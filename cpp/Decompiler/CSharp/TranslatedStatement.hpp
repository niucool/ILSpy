// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/TranslatedStatement.cs -- the wrapper struct
// the statement translation returns (a decompiled C# statement that carries the
// IL-instruction annotations of the IL instructions it was translated from). The C#
// `readonly Statement Statement` field ports to a non-owning pointer (the caller that
// constructed the statement owns it -- the ExpressionWithILInstruction convention).
// The statement's IL-instruction annotations ride the same non-owning holder channel
// the expression wrappers use (see TranslatedExpression.hpp for the convention).

#pragma once

#include "Decompiler/CSharp/Syntax/AbstractAnnotatable.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/IL/ILInstruction.hpp"

#include <cassert>
#include <vector>

namespace ILSpy::Decompiler::CSharp {

// The C# `struct TranslatedStatement` (the compile guard "did we remember the
// IL-instruction annotation"). The C# `IEnumerable<ILInstruction> ILInstructions`
// property enumerates the IL-instruction annotations on the statement.
class TranslatedStatement {
public:
    TranslatedStatement() = default;

    // The C# `internal TranslatedStatement(Statement statement)` ctor (the C#
    // `Debug.Assert(statement != null)` -- the D401 assert convention).
    explicit TranslatedStatement(Syntax::Statement* statement)
        : statement_(statement) {
        assert(statement_ != nullptr &&
               "TranslatedStatement: statement must not be null");
    }

    // The C# `readonly Statement Statement` field.
    Syntax::Statement* Statement() const { return statement_; }

    // The C# `IEnumerable<ILInstruction> ILInstructions` property -- the
    // IL-instruction annotations on the statement, in insertion order.
    std::vector<IL::ILInstruction*> ILInstructions() const {
        return GetILInstructions(*statement_);
    }

private:
    Syntax::Statement* statement_ = nullptr;
};

}  // namespace ILSpy::Decompiler::CSharp
