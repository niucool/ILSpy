// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/CallBuilder.cs -- the call-expression
// builder the ExpressionBuilder VisitNewObj/VisitCall arms construct.
//
// This file is the FIRST SLICE: the `IsSpanBasedStringConcat(IMethod)` static
// the VisitUserDefinedCompoundAssign arm consults to detect the
// span-based `string.Concat(ReadOnlySpan<char>, ...)` compound-assign lowering
// (`s += "..."` on a const string). The Build/BuildStringConcat machinery and
// the `IsStringToReadOnlySpanCharImplicitConversion` helper are DEFERRED with
// the VisitCall/VisitNewObj slices they serve.

#pragma once

#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/Util/BitSet.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// The real type-system namespace alias (the ExpressionBuilder TS:: convention --
// the CSharp/TypeSystem sub-namespace shadows the plain `TypeSystem::` lookup).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace ILSpy::Decompiler::TypeSystem {
class IParameter;
}

namespace ILSpy::Decompiler::CSharp {

// The C# `public class CallBuilder` -- the port carries the static half first
// (the VisitUserDefinedCompoundAssign prerequisite); the instance Build family
// lands with the call arms.
class CallBuilder {
public:
    // The C# `struct ExpectedTargetDetails` (CallBuilder.cs lines 42-46): the call
    // opcode and whether the boxing conversion on the target must be preserved.
    struct ExpectedTargetDetails {
        // The C# `default(OpCode)` zero value is `InvalidBranch` (the port's enum
        // first value); it is always assigned before the Build arms read it.
        IL::OpCode CallOpCode = IL::OpCode::InvalidBranch;
        bool NeedsBoxingConversion = false;
    };

    // The C# `struct ArgumentList` (CallBuilder.cs lines 48-200): the translated
    // call arguments plus the expected parameters, names, and optional/named
    // bookkeeping the Build arms fill in. The C# `string[]` name arrays port to
    // `std::vector<std::string>` with the empty string standing in for null (the
    // D222 IsNullOrEmpty convention); a null-name element means "no name".
    struct ArgumentList {
        std::vector<TranslatedExpression> Arguments;
        std::vector<const TS::IParameter*> ExpectedParameters;
        std::vector<std::string> ParameterNames;
        std::optional<std::vector<std::string>> ArgumentNames;
        int FirstOptionalArgumentIndex = -1;
        Util::BitSet IsPrimitiveValue;
        std::optional<std::vector<int>> ArgumentToParameterMap;
        bool AddNamesToPrimitiveValues = false;
        bool UseImplicitlyTypedOut = false;
        bool IsExpandedForm = false;

        // The C# `int Length => Arguments.Length`.
        int Length() const { return static_cast<int>(Arguments.size()); }

        // The C# `string[]? GetArgumentNames(int skipCount = 0)`.
        std::optional<std::vector<std::string>> GetArgumentNames(int skipCount = 0) const;
        // The C# `IList<ResolveResult> GetArgumentResolveResults(int skipCount = 0)`.
        std::vector<std::shared_ptr<Sem::ResolveResult>> GetArgumentResolveResults(
            int skipCount = 0) const;
        // The C# `IList<ResolveResult> GetArgumentResolveResultsDirect(int skipCount = 0)`.
        std::vector<std::shared_ptr<Sem::ResolveResult>> GetArgumentResolveResultsDirect(
            int skipCount = 0) const;
        // The C# `IEnumerable<Expression> GetArgumentExpressions(int skipCount = 0)`.
        std::vector<Syntax::Expression*> GetArgumentExpressions(int skipCount = 0) const;
        // The C# `bool CanInferAnonymousTypePropertyNamesFromArguments()`.
        bool CanInferAnonymousTypePropertyNamesFromArguments() const;
        // The C# `[Conditional("DEBUG")] void CheckNoNamedOrOptionalArguments()`.
        void CheckNoNamedOrOptionalArguments() const;

    private:
        // The C# `private int GetActualArgumentCount()`.
        int GetActualArgumentCount() const;
    };

    virtual ~CallBuilder() = default;

    // The C# `internal static bool IsSpanBasedStringConcat(IMethod method)`
    // (CallBuilder.cs lines 300-318): whether the method is a static
    // `string.Concat` whose every parameter is `ReadOnlySpan<char>` -- the
    // span-based overload shape the C# compiler emits for `s += "literal"`.
    // Recognized by the method name, the static form, the
    // System.String declaring type (the C# `DeclaringType.IsKnownType
    // (KnownTypeCode.String)` extension), and the per-parameter
    // `ReadOnlySpan<char>` element check (`p.Type.TypeArguments[0]`).
    // Implemented out-of-line in the .cpp.
    static bool IsSpanBasedStringConcat(const TS::IMethod& method);
};

} // namespace ILSpy::Decompiler::CSharp
