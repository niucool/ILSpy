// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
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

// Tests for the TransformDisplayClassUsage scaffolding (the port of the C#
// nested DisplayClass / VariableToDeclare classes): the GetOrDeclare local
// registration (the field's type and name, the UsesInitialValue flag), the
// Propagate/CanPropagate pair, and the AddVariable map wiring.

#include "Decompiler/IL/Transforms/TransformDisplayClassUsage.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

// The shared fixture: the compilation the LookupField stubs reference.
TS::TestSupport::LookupCompilation compilation;

TEST(TransformDisplayClassUsageTest, GetOrDeclareRegistersTheLocal)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto fieldType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    // The container: the display-class variable (registered on the same
    // function; the C# `container.Variable.Function` back-pointer shape).
    ILVariablePtr container = fn->RegisterVariable(
        IL::VariableKind::Local, nullptr, std::string("closure"));
    auto field = std::make_shared<TS::TestSupport::LookupField>("value", fieldType, compilation);
    IL::TransformDisplayClassUsage::VariableToDeclare vtd(
        container.get(), fn.get(), nullptr, field.get());
    vtd.UsesInitialValue = true;

    IL::ILVariable* declared = vtd.GetOrDeclare();
    ASSERT_NE(declared, nullptr);
    EXPECT_EQ(declared->Name, "value");
    EXPECT_EQ(declared->Kind, IL::VariableKind::Local);
    EXPECT_EQ(declared->Type.get(), fieldType.get());
    EXPECT_TRUE(declared->UsesInitialValue);
    // The second call returns the same (the C# GetOrDeclare caches).
    EXPECT_EQ(vtd.GetOrDeclare(), declared);
    // The registered variable belongs to the function.
    bool found = false;
    for (const auto& v : fn->Variables) {
        if (v.get() == declared) found = true;
    }
    EXPECT_TRUE(found) << "the declared local is registered on the function";
}

TEST(TransformDisplayClassUsageTest, PropagateTogglesCanPropagate)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto fieldType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    ILVariablePtr container = fn->RegisterVariable(
        IL::VariableKind::Local, nullptr, std::string("closure"));
    ILVariablePtr replacement = fn->RegisterVariable(
        IL::VariableKind::Local, fieldType, std::string("replacement"));
    auto field = std::make_shared<TS::TestSupport::LookupField>("value", fieldType, compilation);
    IL::TransformDisplayClassUsage::VariableToDeclare vtd(
        container.get(), fn.get(), nullptr, field.get());

    EXPECT_FALSE(vtd.CanPropagate);
    vtd.Propagate(replacement.get());
    EXPECT_TRUE(vtd.CanPropagate);
    EXPECT_EQ(vtd.DeclaredVariable(), replacement.get());
    // Revoking the propagation nulls the variable (the C# Propagate(null)).
    vtd.Propagate(nullptr);
    EXPECT_FALSE(vtd.CanPropagate);
    EXPECT_EQ(vtd.DeclaredVariable(), nullptr);
    // The revocation clears the cached declared variable (the next
    // GetOrDeclare re-registers from the field).
    EXPECT_EQ(vtd.GetOrDeclare()->Name, "value");
}

TEST(TransformDisplayClassUsageTest, VariableToDeclareNameIsTheFieldName)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto fieldType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    ILVariablePtr container = fn->RegisterVariable(
        IL::VariableKind::Local, nullptr, std::string("closure"));
    auto field = std::make_shared<TS::TestSupport::LookupField>("cached", fieldType, compilation);
    IL::TransformDisplayClassUsage::VariableToDeclare vtd(
        container.get(), fn.get(), nullptr, field.get());
    EXPECT_EQ(vtd.Name(), "cached");
}

} // namespace