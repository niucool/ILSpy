// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The IntroduceRefReadOnlyModifierOnLocals tests: a by-ref local is marked
// ref readonly when one of its stores initializes it from a reference C#
// requires to be readonly (the IsReadonlyReference shapes) or from a
// `readonly.` ldelema. Hand-built trees (the 0-firing-on-corpus precedent:
// neither the net48 mscorlib nor the modern shared-framework assemblies
// carry a `readonly. ldelema` body -- Roslyn elides the prefix in the
// shapes this fixture family triggers).

#include "Decompiler/IL/Transforms/IntroduceRefReadOnlyModifierOnLocals.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace ILSpy::Decompiler::IL;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

// A by-reference type stand-in (the C# ByReferenceType; the name-only
// SimpleType with Kind ByReference).
TS::ITypePtr ByRefType() {
    return std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName("System", "Int32&"), TS::TypeKind::ByReference);
}

ILVariablePtr MakeByRefLocal() {
    auto v = std::make_shared<ILVariable>();
    v->Name = "V_0";
    v->Kind = VariableKind::Local;
    v->Type = ByRefType();
    return v;
}

// An ILFunction with a single block storing `storeValue` into `variable`.
std::unique_ptr<ILFunction> MakeStoringFunction(ILVariablePtr variable,
                                  std::unique_ptr<ILInstruction> storeValue) {
    auto fn = std::make_unique<ILFunction>();
    fn->Variables.push_back(variable);
    auto container = std::make_unique<BlockContainer>();
    auto block = std::make_unique<Block>();
    block->Add(std::make_unique<StLoc>(variable, std::move(storeValue)));
    container->AddBlock(std::move(block));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    // The transform reads the variable's StoreInstructions, which the
    // variable-usage pass populates (the pipeline always runs it first).
    ComputeVariableUsage(*fn);
    return fn;
}

} // namespace

// A by-ref local stored from a `readonly.` ldelema is marked.
TEST(IntroduceRefReadOnlyModifierOnLocalsTest,
     MarksLocalStoredFromReadonlyLdelema) {
    ILVariablePtr variable = MakeByRefLocal();
    auto array = std::make_unique<LdLoc>(MakeByRefLocal());
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<LdcI4>(0));
    auto ldelema = std::make_unique<LdElema>(nullptr, std::move(array),
                                             std::move(indices));
    ldelema->IsReadOnly = true;
    std::unique_ptr<ILFunction> fn = MakeStoringFunction(variable, std::move(ldelema));

    ILTransformContext ctx;
    IntroduceRefReadOnlyModifierOnLocals().Run(*fn, ctx);
    EXPECT_TRUE(variable->IsRefReadOnly);
}

// A plain (non-readonly) ldelema does not mark the local.
TEST(IntroduceRefReadOnlyModifierOnLocalsTest,
     PlainLdelemaDoesNotMarkTheLocal) {
    ILVariablePtr variable = MakeByRefLocal();
    auto array = std::make_unique<LdLoc>(MakeByRefLocal());
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<LdcI4>(0));
    auto ldelema = std::make_unique<LdElema>(nullptr, std::move(array),
                                             std::move(indices));
    ldelema->IsReadOnly = false;
    std::unique_ptr<ILFunction> fn = MakeStoringFunction(variable, std::move(ldelema));

    ILTransformContext ctx;
    IntroduceRefReadOnlyModifierOnLocals().Run(*fn, ctx);
    EXPECT_FALSE(variable->IsRefReadOnly);
}

// A by-ref local stored from a readonly field's address (the
// IsReadonlyReference ldflda shape) is marked.
TEST(IntroduceRefReadOnlyModifierOnLocalsTest,
     MarksLocalStoredFromReadonlyFieldAddress) {
    ILVariablePtr variable = MakeByRefLocal();
    auto target0 = std::make_unique<LdLoc>(MakeByRefLocal());
    auto ldflda = std::make_unique<LdFlda>(std::move(target0),
                                           "System.Int32 C::_f");
    ldflda->FieldIsReadOnly = true;
    std::unique_ptr<ILFunction> fn = MakeStoringFunction(variable, std::move(ldflda));

    ILTransformContext ctx;
    IntroduceRefReadOnlyModifierOnLocals().Run(*fn, ctx);
    EXPECT_TRUE(variable->IsRefReadOnly);
}

// A mutable field's address does not mark the local.
TEST(IntroduceRefReadOnlyModifierOnLocalsTest,
     MutableFieldAddressDoesNotMarkTheLocal) {
    ILVariablePtr variable = MakeByRefLocal();
    auto target1 = std::make_unique<LdLoc>(MakeByRefLocal());
    auto ldflda = std::make_unique<LdFlda>(std::move(target1),
                                           "System.Int32 C::_f");
    ldflda->FieldIsReadOnly = false;
    std::unique_ptr<ILFunction> fn = MakeStoringFunction(variable, std::move(ldflda));

    ILTransformContext ctx;
    IntroduceRefReadOnlyModifierOnLocals().Run(*fn, ctx);
    EXPECT_FALSE(variable->IsRefReadOnly);
}

// A by-ref parameter is never marked (the C# early-out); a local of a
// non-by-ref type never reaches the checks.
TEST(IntroduceRefReadOnlyModifierOnLocalsTest,
     ParametersAndNonByRefLocalsAreSkipped) {
    ILVariablePtr parameter = MakeByRefLocal();
    parameter->Kind = VariableKind::Parameter;
    auto array0 = std::make_unique<LdLoc>(MakeByRefLocal());
    std::vector<std::unique_ptr<ILInstruction>> indices0;
    indices0.push_back(std::make_unique<LdcI4>(0));
    auto ldelema0 = std::make_unique<LdElema>(nullptr, std::move(array0),
                                              std::move(indices0));
    ldelema0->IsReadOnly = true;
    std::unique_ptr<ILFunction> fn0 = MakeStoringFunction(parameter, std::move(ldelema0));
    ILTransformContext ctx;
    IntroduceRefReadOnlyModifierOnLocals().Run(*fn0, ctx);
    EXPECT_FALSE(parameter->IsRefReadOnly);

    ILVariablePtr intLocal = std::make_shared<ILVariable>();
    intLocal->Name = "V_1";
    intLocal->Kind = VariableKind::Local;
    intLocal->Type = std::make_shared<TS::KnownType>(
        TS::KnownTypeCode::Int32);
    auto array1 = std::make_unique<LdLoc>(MakeByRefLocal());
    std::vector<std::unique_ptr<ILInstruction>> indices1;
    indices1.push_back(std::make_unique<LdcI4>(0));
    auto ldelema1 = std::make_unique<LdElema>(nullptr, std::move(array1),
                                              std::move(indices1));
    ldelema1->IsReadOnly = true;
    std::unique_ptr<ILFunction> fn1 = MakeStoringFunction(intLocal, std::move(ldelema1));
    IntroduceRefReadOnlyModifierOnLocals().Run(*fn1, ctx);
    EXPECT_FALSE(intLocal->IsRefReadOnly);
}
