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

// Tests for the settings -> TypeSystemOptions mapping (the port of the
// C# DecompilerTypeSystem.GetOptions mapping, living on the ILSpyX
// DecompilerSettings subclass) and the deferred
// LoadedAssemblyExtensions.GetTypeSystemWithDecompilerSettingsOrNull.

#include "ILSpyX/AssemblyList.hpp"
#include "ILSpyX/LoadedAssemblyExtensions.hpp"
#include "ILSpyX/Settings/DecompilerSettings.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;
using ILSpy::ILSpyX::LoadedAssembly;
using ILSpy::ILSpyX::AssemblyList;
using EngineSettings = ILSpy::Decompiler::DecompilerSettings;
using ILSpyXSettings = ILSpy::ILSpyX::Settings::DecompilerSettings;

// The C# mapping table: the settings flag -> the TypeSystemOptions bit.
struct FlagMapping {
    const char* name;
    void (EngineSettings::*setter)(bool);
    TypeSystemOptions bit;
};

const FlagMapping kMappings[] = {
    {"Dynamic", &EngineSettings::SetDynamic, TypeSystemOptions::Dynamic},
    {"TupleTypes", &EngineSettings::SetTupleTypes, TypeSystemOptions::Tuple},
    {"ExtensionMethods", &EngineSettings::SetExtensionMethods,
        TypeSystemOptions::ExtensionMethods},
    {"DecimalConstants", &EngineSettings::SetDecimalConstants,
        TypeSystemOptions::DecimalConstants},
    {"IntroduceRefModifiersOnStructs",
        &EngineSettings::SetIntroduceRefModifiersOnStructs,
        TypeSystemOptions::RefStructs},
    {"IntroduceReadonlyAndInModifiers",
        &EngineSettings::SetIntroduceReadonlyAndInModifiers,
        TypeSystemOptions::ReadOnlyStructsAndParameters},
    {"IntroduceUnmanagedConstraint",
        &EngineSettings::SetIntroduceUnmanagedConstraint,
        TypeSystemOptions::UnmanagedConstraints},
    {"NullableReferenceTypes", &EngineSettings::SetNullableReferenceTypes,
        TypeSystemOptions::NullabilityAnnotations},
    {"ReadOnlyMethods", &EngineSettings::SetReadOnlyMethods,
        TypeSystemOptions::ReadOnlyMethods},
    {"NativeIntegers", &EngineSettings::SetNativeIntegers,
        TypeSystemOptions::NativeIntegers},
    {"FunctionPointers", &EngineSettings::SetFunctionPointers,
        TypeSystemOptions::FunctionPointers},
    {"ScopedRef", &EngineSettings::SetScopedRef, TypeSystemOptions::ScopedRef},
    {"NumericIntPtr", &EngineSettings::SetNumericIntPtr,
        TypeSystemOptions::NativeIntegersWithoutAttribute},
    {"RefReadOnlyParameters", &EngineSettings::SetRefReadOnlyParameters,
        TypeSystemOptions::RefReadOnlyParameters},
    {"ParamsCollections", &EngineSettings::SetParamsCollections,
        TypeSystemOptions::ParamsCollections},
    {"FirstClassSpanTypes", &EngineSettings::SetFirstClassSpanTypes,
        TypeSystemOptions::FirstClassSpanTypes},
    {"ExtensionMembers", &EngineSettings::SetExtensionMembers,
        TypeSystemOptions::ExtensionMembers},
    {"AsyncAwait", &EngineSettings::SetAsyncAwait,
        TypeSystemOptions::RuntimeAsync},
};

// Turns every mapped flag off (the C# defaults carry most of them ON --
// the mapping reflects the CURRENT state, whatever the defaults are).
EngineSettings AllFlagsOff()
{
    EngineSettings settings;
    for (const FlagMapping& mapping : kMappings) {
        (settings.*(mapping.setter))(false);
    }
    return settings;
}

TEST(DecompilerSettingsOptionsTest, GetOptionsIsNoneWhenEveryFlagIsOff) {
    // The C# mapping starts from TypeSystemOptions.None and ORs the
    // current values -- all flags off maps to None.
    EngineSettings settings = AllFlagsOff();
    EXPECT_EQ(ILSpyXSettings::GetOptions(settings),
        TypeSystemOptions::None);
}

TEST(DecompilerSettingsOptionsTest, EachFlagMapsToItsBit) {
    for (const FlagMapping& mapping : kMappings) {
        EngineSettings settings;
        (settings.*(mapping.setter))(true);
        EXPECT_EQ(ILSpyXSettings::GetOptions(settings) & mapping.bit,
            mapping.bit)
            << mapping.name;
        // And back off again.
        (settings.*(mapping.setter))(false);
        EXPECT_EQ(ILSpyXSettings::GetOptions(settings) & mapping.bit,
            TypeSystemOptions::None)
            << mapping.name;
    }
}

TEST(DecompilerSettingsOptionsTest, CombinedFlagsOr) {
    EngineSettings settings = AllFlagsOff();
    settings.SetDynamic(true);
    settings.SetFunctionPointers(true);
    settings.SetAsyncAwait(true);
    const TypeSystemOptions expected = TypeSystemOptions::Dynamic |
        TypeSystemOptions::FunctionPointers | TypeSystemOptions::RuntimeAsync;
    EXPECT_EQ(ILSpyXSettings::GetOptions(settings), expected);
}

TEST(DecompilerSettingsOptionsTest,
    GetTypeSystemWithDecompilerSettingsReflectsTheOptions) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    const auto& module = asm_.GetMetadataFile();

    // The mapping feeds the options-keyed type system: with the default
    // settings (most flags on) the options include Dynamic.
    EngineSettings settings;
    settings.SetDynamic(true);
    const auto first =
        ILSpy::ILSpyX::GetTypeSystemWithDecompilerSettingsOrNull(module,
            settings);
    ASSERT_NE(first, nullptr);
    // The same settings hit the cache (the same compilation).
    const auto again =
        ILSpy::ILSpyX::GetTypeSystemWithDecompilerSettingsOrNull(module,
            settings);
    EXPECT_EQ(first, again);
    // Different settings rebuild (the C# options-keyed cache).
    EngineSettings off = AllFlagsOff();
    const auto withoutDynamic =
        ILSpy::ILSpyX::GetTypeSystemWithDecompilerSettingsOrNull(module, off);
    ASSERT_NE(withoutDynamic, nullptr);
    EXPECT_NE(first, withoutDynamic);
}

}  // namespace
