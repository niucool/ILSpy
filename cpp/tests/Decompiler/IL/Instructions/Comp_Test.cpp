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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the Comp node's faithful nullable-lifting model (the foundation
// the deferred NullableLiftingStatementTransform / ExpressionTransforms
// nullable-lifting visits need): the ComparisonLiftingKind enum, the LiftingKind
// + InputType fields, the IsLifted() / UnderlyingResultType() helpers, the
// ResultType() flip to O for the SQL-style ThreeValuedLogic lift, and the
// `.lifted[C#]` / `.lifted[3VL]` dump annotation. The model follows Comp.cs:
// an ordinary comparison has LiftingKind == None and InputType == the left
// operand's ResultType; a lifted comparison carries the underlying input type
// explicitly and the lifting kind the nullable-lifting machinery produced.
// The MatchCompOrDecimal helper (Comp subset) that consults IsLifted() is
// covered in NullableLiftingTransform_Test.

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ILVariablePtr MakeLocal(std::string name, KnownTypeCode typeCode = KnownTypeCode::Int32) {
    auto v = std::make_shared<ILVariable>();
    v->Name = std::move(name);
    v->Kind = VariableKind::Local;
    v->Type = std::make_shared<KnownType>(typeCode);
    return v;
}

} // namespace

// An ordinary comparison (the default constructor): LiftingKind is None,
// IsLifted() is false, InputType derives from the left operand's ResultType,
// and ResultType() is I4 (the CLI comparison result). The dump renders the
// kind and no lifting suffix.
TEST(Comp, OrdinaryComparisonDefaults) {
    auto v = MakeLocal("v");
    Comp c(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
           ComparisonKind::Equality);
    EXPECT_EQ(c.LiftingKind, ComparisonLiftingKind::None);
    EXPECT_FALSE(c.IsLifted());
    EXPECT_EQ(c.InputType, StackType::I4);  // LdLoc of a Local -> I4 (default)
    EXPECT_EQ(c.ResultType(), StackType::I4);
    EXPECT_EQ(c.UnderlyingResultType(), StackType::I4);
    c.CheckInvariant(ILPhase::Normal);
    std::string dump;
    c.WriteTo(dump);
    EXPECT_NE(dump.find("comp(eq"), std::string::npos);
    EXPECT_EQ(dump.find("lifted"), std::string::npos);  // no suffix for None
}

// An unsigned comparison carries the Unsigned flag and the dump renders `.un`;
// LiftingKind stays None and ResultType stays I4.
TEST(Comp, UnsignedComparison) {
    auto v = MakeLocal("v");
    Comp c(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(5),
           ComparisonKind::GreaterThan, true);
    EXPECT_TRUE(c.Unsigned);
    EXPECT_FALSE(c.IsLifted());
    EXPECT_EQ(c.ResultType(), StackType::I4);
    std::string dump;
    c.WriteTo(dump);
    EXPECT_NE(dump.find("comp(gt.un"), std::string::npos);
    EXPECT_EQ(dump.find("lifted"), std::string::npos);
}

// A C#-style lifted comparison (Comp(kind, CSharp, inputType, left, right)):
// IsLifted() is true, InputType is the passed underlying type, and ResultType
// stays I4 (the C# lift keeps the I4 result). The dump annotates `.lifted[C#]`.
TEST(Comp, CSharpLiftedComparison) {
    auto v = MakeLocal("v");
    Comp c(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
           ComparisonKind::Equality, ComparisonLiftingKind::CSharp, StackType::I4, false);
    EXPECT_EQ(c.LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_TRUE(c.IsLifted());
    EXPECT_EQ(c.InputType, StackType::I4);
    EXPECT_EQ(c.ResultType(), StackType::I4);  // CSharp lift keeps I4
    EXPECT_EQ(c.UnderlyingResultType(), StackType::I4);
    std::string dump;
    c.WriteTo(dump);
    EXPECT_NE(dump.find("comp(eq"), std::string::npos);
    EXPECT_NE(dump.find(".lifted[C#]"), std::string::npos);
}

// A SQL-style lifted comparison (ThreeValuedLogic): the result is itself a
// nullable value, so ResultType() flips to O (faithful to Comp.cs); IsLifted()
// is true and the dump annotates `.lifted[3VL]`.
TEST(Comp, ThreeValuedLogicLiftedComparison) {
    auto v = MakeLocal("v");
    Comp c(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
           ComparisonKind::Equality, ComparisonLiftingKind::ThreeValuedLogic,
           StackType::I4, false);
    EXPECT_EQ(c.LiftingKind, ComparisonLiftingKind::ThreeValuedLogic);
    EXPECT_TRUE(c.IsLifted());
    EXPECT_EQ(c.ResultType(), StackType::O);  // 3VL lift -> O (null result)
    EXPECT_EQ(c.UnderlyingResultType(), StackType::I4);
    std::string dump;
    c.WriteTo(dump);
    EXPECT_NE(dump.find("comp(eq"), std::string::npos);
    EXPECT_NE(dump.find(".lifted[3VL]"), std::string::npos);
}

// The lifted constructor accepts an explicit underlying input type that
// differs from the operands' ResultType (the operands are Nullable<T> of O
// stack type, the underlying type is the inner T). InputType is the passed
// value, not Left->ResultType().
TEST(Comp, LiftedComparisonCarriesExplicitInputType) {
    auto v = MakeLocal("v");
    // The operands are O-typed (a Nullable<T> on the eval stack is O); the
    // underlying input type is I8 (a Nullable<long>). The lift records I8.
    Comp c(std::make_unique<LdLoc>(v), std::make_unique<LdcI4>(0),
           ComparisonKind::LessThan, ComparisonLiftingKind::CSharp, StackType::I8,
           true);
    EXPECT_EQ(c.InputType, StackType::I8);
    EXPECT_TRUE(c.Unsigned);
    EXPECT_TRUE(c.IsLifted());
    std::string dump;
    c.WriteTo(dump);
    EXPECT_NE(dump.find("comp(lt.un.lifted[C#]"), std::string::npos);
}

// NegateComparison mirrors ComparisonKind.Negate (ECMA-335 II.3.2):
// == <=> !=, < => >=, <= => >, > => <=, >= => <.
TEST(Comp, NegateComparisonIsFaithful) {
    EXPECT_EQ(NegateComparison(ComparisonKind::Equality), ComparisonKind::Inequality);
    EXPECT_EQ(NegateComparison(ComparisonKind::Inequality), ComparisonKind::Equality);
    EXPECT_EQ(NegateComparison(ComparisonKind::LessThan), ComparisonKind::GreaterThanOrEqual);
    EXPECT_EQ(NegateComparison(ComparisonKind::LessThanOrEqual), ComparisonKind::GreaterThan);
    EXPECT_EQ(NegateComparison(ComparisonKind::GreaterThan), ComparisonKind::LessThanOrEqual);
    EXPECT_EQ(NegateComparison(ComparisonKind::GreaterThanOrEqual), ComparisonKind::LessThan);
}
