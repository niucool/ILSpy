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

// Tests for the LdcDecimal ILAst node -- the System.Decimal constant node the
// next in-order ExpressionTransforms.VisitLdObj piece
// (TransformDecimalFieldToConstant: ldsfld Decimal.One/Zero/MinusOne -> the
// LdcDecimal constant) and the deferred EarlyExpressionTransforms
// TransformDecimalCtorToConstant build from. This is a tested foundation: the
// node + the faithful DecimalValue model (System.Decimal's internal layout --
// a 96-bit mantissa, a sign, a 0..28 scale). The tests cover the node
// invariant/flags/ResultType/dump, the DecimalValue factory helpers
// (FromInt32/UInt32/Int64/UInt64/FromBits + One/Zero/MinusOne), the ToString
// numeric formatting (integer + scaled + sign + zero), equality, the
// ILAstToCSharp seed rendering of the `m`-suffixed decimal literal, and a
// mscorlib sweep constructing LdcDecimals over real Decimal static-field loads.

#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

// A one-block function assigning an LdcDecimal to a local, so the seed's
// statement walker renders the decimal literal in `--csharp` output.
std::unique_ptr<ILFunction> MakeFnWithLdcDecimal(ITypePtr resultType, DecimalValue value) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto v = MakeLocal("d", resultType);
    fn->Variables.push_back(v);
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(v, std::make_unique<LdcDecimal>(value)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    return fn;
}

} // namespace

// The node is a SimpleInstruction leaf: no children, DirectFlags None, ResultType
// O (faithful to the C# `override StackType ResultType => StackType.O`).
TEST(LdcDecimal, NodeInvariantAndResultType) {
    LdcDecimal node(DecimalValue::One());
    EXPECT_EQ(node.Op, OpCode::LdcDecimal);
    EXPECT_EQ(node.ChildCount(), 0);
    EXPECT_EQ(node.DirectFlags(), InstructionFlags::None);
    EXPECT_EQ(node.ResultType(), StackType::O);
    node.CheckInvariant(ILPhase::Normal);
}

// The dump renders `ldc.decimal(<numeric-string>)` so the value is visible in
// the ILAst text dump (matching the C# `ldc.decimal <value>` form).
TEST(LdcDecimal, DumpShowsValue) {
    LdcDecimal one(DecimalValue::One());
    EXPECT_EQ(one.ToString(), "ldc.decimal(1)");
    LdcDecimal zero(DecimalValue::Zero());
    EXPECT_EQ(zero.ToString(), "ldc.decimal(0)");
    LdcDecimal minusOne(DecimalValue::MinusOne());
    EXPECT_EQ(minusOne.ToString(), "ldc.decimal(-1)");
}

// DecimalValue::One/Zero/MinusOne produce the System.Decimal named-constant
// values: 1, 0, and -1 (mantissa 1 + isNegative).
TEST(LdcDecimal, NamedConstantsAreCorrect) {
    EXPECT_EQ(DecimalValue::One().ToString(), "1");
    EXPECT_EQ(DecimalValue::Zero().ToString(), "0");
    EXPECT_EQ(DecimalValue::MinusOne().ToString(), "-1");
    EXPECT_TRUE(DecimalValue::One() == DecimalValue(DecimalValue::One()));
    EXPECT_FALSE(DecimalValue::One() == DecimalValue::Zero());
    EXPECT_TRUE(DecimalValue::Zero().IsZero());
    EXPECT_FALSE(DecimalValue::One().IsZero());
}

// FromInt32 covers positive, negative, and the INT32_MIN edge (whose magnitude
// is 2^31, fitting in the 32-bit mantissa via the int64 negation).
TEST(LdcDecimal, FromInt32CoversSignAndMinEdge) {
    EXPECT_EQ(DecimalValue::FromInt32(1).ToString(), "1");
    EXPECT_EQ(DecimalValue::FromInt32(-1).ToString(), "-1");
    EXPECT_EQ(DecimalValue::FromInt32(2147483647).ToString(), "2147483647");
    EXPECT_EQ(DecimalValue::FromInt32(-2147483648).ToString(), "-2147483648");
    EXPECT_FALSE(DecimalValue::FromInt32(-2147483648).IsZero());
}

// FromUInt32/FromUInt64 carry the unsigned magnitude with no sign.
TEST(LdcDecimal, FromUnsignedHasNoSign) {
    EXPECT_EQ(DecimalValue::FromUInt32(0xFFFFFFFFu).ToString(), "4294967295");
    EXPECT_FALSE(DecimalValue::FromUInt32(0xFFFFFFFFu).isNegative);
    EXPECT_EQ(DecimalValue::FromUInt64(0xFFFFFFFFFFFFFFFFull).ToString(),
              "18446744073709551615");
    EXPECT_FALSE(DecimalValue::FromUInt64(0xFFFFFFFFFFFFFFFFull).isNegative);
}

// FromInt64 splits the 64-bit magnitude across lo/mid; INT64_MIN's magnitude is
// 2^63 (mid = 0x80000000, lo = 0).
TEST(LdcDecimal, FromInt64SplitsAcrossLoMid) {
    EXPECT_EQ(DecimalValue::FromInt64(1).ToString(), "1");
    EXPECT_EQ(DecimalValue::FromInt64(-1).ToString(), "-1");
    EXPECT_EQ(DecimalValue::FromInt64(9223372036854775807LL).ToString(),
              "9223372036854775807");
    EXPECT_EQ(DecimalValue::FromInt64(-9223372036854775807LL - 1).ToString(),
              "-9223372036854775808");
}

// FromBits (the 5-arg `new decimal(lo, mid, hi, isNegative, scale)` constructor)
// round-trips through ToString with the decimal point inserted `scale` digits
// from the right and the sign applied (zero is always unsigned).
TEST(LdcDecimal, FromBitsAppliesScaleAndSign) {
    // 1.5 = mantissa 15, scale 1.
    EXPECT_EQ(DecimalValue::FromBits(15, 0, 0, false, 1).ToString(), "1.5");
    // -0.25 = mantissa 25, scale 2, negative.
    EXPECT_EQ(DecimalValue::FromBits(25, 0, 0, true, 2).ToString(), "-0.25");
    // 0.00 = mantissa 0, scale 2 (zero is unsigned).
    EXPECT_EQ(DecimalValue::FromBits(0, 0, 0, true, 2).ToString(), "0.00");
    // A value whose mantissa has fewer digits than the scale is padded:
    // mantissa 5, scale 3 -> 0.005.
    EXPECT_EQ(DecimalValue::FromBits(5, 0, 0, false, 3).ToString(), "0.005");
}

// A 96-bit mantissa (spanning lo/mid/hi) formats correctly via the repeated
// divmod-by-1e9 -- the largest 96-bit decimal (2^96 - 1) round-trips.
TEST(LdcDecimal, FormatsFull96BitMantissa) {
    // 2^96 - 1 = 79228162514264337593543950335.
    DecimalValue max96;
    max96.lo = 0xFFFFFFFFu;
    max96.mid = 0xFFFFFFFFu;
    max96.hi = 0xFFFFFFFFu;
    EXPECT_EQ(max96.ToString(), "79228162514264337593543950335");
}

// The ILAstToCSharp seed renders the decimal as the C# literal form with the
// trailing `m` suffix (matching the real back end's VisitLdcDecimal ->
// ConvertConstantValue -> a PrimitiveExpression with the `m` suffix).
TEST(LdcDecimal, SeedRendersDecimalLiteralWithMSuffix) {
    auto decimalType = std::make_shared<KnownType>(KnownTypeCode::Decimal);
    {
        auto fn = MakeFnWithLdcDecimal(decimalType, DecimalValue::One());
        fn->CheckInvariant(ILPhase::Normal);
        std::string text = ILAstToCSharp(*fn, "void", "M", "decimal d");
        EXPECT_NE(text.find("d = 1m"), std::string::npos) << text;
    }
    {
        auto fn = MakeFnWithLdcDecimal(decimalType, DecimalValue::MinusOne());
        fn->CheckInvariant(ILPhase::Normal);
        std::string text = ILAstToCSharp(*fn, "void", "M", "decimal d");
        EXPECT_NE(text.find("d = -1m"), std::string::npos) << text;
    }
    {
        auto fn = MakeFnWithLdcDecimal(decimalType, DecimalValue::FromBits(15, 0, 0, false, 1));
        fn->CheckInvariant(ILPhase::Normal);
        std::string text = ILAstToCSharp(*fn, "void", "M", "decimal d");
        EXPECT_NE(text.find("1.5m"), std::string::npos) << text;
    }
}

// A mscorlib sweep: decode real methods and, for each method whose body loads a
// Decimal static field (ldsfld Decimal.One/Zero/MinusOne -> ldobj(ldsflda)),
// construct an LdcDecimal over the field's value and assert the node invariant
// holds and the dump renders. This exercises the node on the real corpus shape
// the TransformDecimalFieldToConstant fold will consume.
TEST(LdcDecimal, MscorlibConstructFromRealDecimalFieldLoads) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int decimalFields = 0;
    int constructed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (!inst || inst->Op != OpCode::LdObj) return;
            auto* ldobj = static_cast<LdObj*>(inst);
            auto* addr = ldobj->Target ? dynamic_cast<LdsFlda*>(ldobj->Target.get()) : nullptr;
            if (!addr) return;
            const std::string& name = addr->FieldName;
            DecimalValue value;
            if (name == "System.Decimal::One") { value = DecimalValue::One(); }
            else if (name == "System.Decimal::Zero") { value = DecimalValue::Zero(); }
            else if (name == "System.Decimal::MinusOne") { value = DecimalValue::MinusOne(); }
            else return;
            ++decimalFields;
            auto ldc = std::make_unique<LdcDecimal>(value);
            ldc->CheckInvariant(ILPhase::Normal);
            std::string dump = ldc->ToString();
            EXPECT_NE(dump.find("ldc.decimal"), std::string::npos) << dump;
            ++constructed;
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(decimalFields, 0) << "mscorlib must reference Decimal.One/Zero/MinusOne";
    EXPECT_GT(constructed, 0);
}
