// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `KnownAttribute` (cpp/Decompiler/TypeSystem/KnownAttribute.hpp, the D378 port of
// ICSharpCode.Decompiler/TypeSystem/Implementation/KnownAttributes.cs). `KnownAttribute`
// names every attribute the decompiler recognizes by its metadata type name; it is a leaf
// dependency of `IEntity` (whose `HasAttribute`/`GetAttribute` take a `KnownAttribute`) and
// toward `IModule`/`IAttribute`/`TypeSystemAstBuilder`/`CSharpAmbience`. The tests pin the
// enum values, the `Count`/table extent, the `GetTypeName` metadata-name mapping, the
// `None` default-constructed table entry, and the pure `IsCustomAttribute` classifier.

#include "Decompiler/TypeSystem/KnownAttribute.hpp"

#include <gtest/gtest.h>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::KnownAttribute;
using TS::KnownAttributeCount;
using TS::KnownAttributeTypeNames;
using TS::GetTypeName;
using TS::IsCustomAttribute;

// ---------------------------------------------------------------------------
// KnownAttribute -- representative enum values match their C# declaration index.
// ---------------------------------------------------------------------------
TEST(KnownAttributeTest, EnumValuesMatchCSharpDeclarationOrder)
{
    EXPECT_EQ(static_cast<int>(KnownAttribute::None), 0);
    EXPECT_EQ(static_cast<int>(KnownAttribute::CompilerGenerated), 1);
    EXPECT_EQ(static_cast<int>(KnownAttribute::CompilerFeatureRequired), 2);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Extension), 3);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Dynamic), 4);
    EXPECT_EQ(static_cast<int>(KnownAttribute::TupleElementNames), 5);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Nullable), 6);
    EXPECT_EQ(static_cast<int>(KnownAttribute::NullableContext), 7);
    EXPECT_EQ(static_cast<int>(KnownAttribute::NullablePublicOnly), 8);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Conditional), 9);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Obsolete), 10);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Embedded), 11);
    EXPECT_EQ(static_cast<int>(KnownAttribute::IsReadOnly), 12);
    EXPECT_EQ(static_cast<int>(KnownAttribute::SpecialName), 13);
    EXPECT_EQ(static_cast<int>(KnownAttribute::DebuggerHidden), 14);
    EXPECT_EQ(static_cast<int>(KnownAttribute::DebuggerStepThrough), 15);
    EXPECT_EQ(static_cast<int>(KnownAttribute::DebuggerBrowsable), 16);
    EXPECT_EQ(static_cast<int>(KnownAttribute::AssemblyVersion), 17);
    EXPECT_EQ(static_cast<int>(KnownAttribute::InternalsVisibleTo), 18);
    EXPECT_EQ(static_cast<int>(KnownAttribute::TypeForwardedTo), 19);
    EXPECT_EQ(static_cast<int>(KnownAttribute::ReferenceAssembly), 20);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Serializable), 21);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Flags), 22);
    EXPECT_EQ(static_cast<int>(KnownAttribute::ComImport), 23);
    EXPECT_EQ(static_cast<int>(KnownAttribute::CoClass), 24);
    EXPECT_EQ(static_cast<int>(KnownAttribute::StructLayout), 25);
    EXPECT_EQ(static_cast<int>(KnownAttribute::DefaultMember), 26);
    EXPECT_EQ(static_cast<int>(KnownAttribute::IsByRefLike), 27);
    EXPECT_EQ(static_cast<int>(KnownAttribute::IteratorStateMachine), 28);
    EXPECT_EQ(static_cast<int>(KnownAttribute::AsyncStateMachine), 29);
    EXPECT_EQ(static_cast<int>(KnownAttribute::AsyncMethodBuilder), 30);
    EXPECT_EQ(static_cast<int>(KnownAttribute::AsyncIteratorStateMachine), 31);
    EXPECT_EQ(static_cast<int>(KnownAttribute::FieldOffset), 32);
    EXPECT_EQ(static_cast<int>(KnownAttribute::NonSerialized), 33);
    EXPECT_EQ(static_cast<int>(KnownAttribute::DecimalConstant), 34);
    EXPECT_EQ(static_cast<int>(KnownAttribute::FixedBuffer), 35);
    EXPECT_EQ(static_cast<int>(KnownAttribute::DllImport), 36);
    EXPECT_EQ(static_cast<int>(KnownAttribute::PreserveSig), 37);
    EXPECT_EQ(static_cast<int>(KnownAttribute::MethodImpl), 38);
    EXPECT_EQ(static_cast<int>(KnownAttribute::IndexerName), 39);
    EXPECT_EQ(static_cast<int>(KnownAttribute::ParamArray), 40);
    EXPECT_EQ(static_cast<int>(KnownAttribute::ParamCollection), 41);
    EXPECT_EQ(static_cast<int>(KnownAttribute::In), 42);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Out), 43);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Optional), 44);
    EXPECT_EQ(static_cast<int>(KnownAttribute::DefaultParameterValue), 45);
    EXPECT_EQ(static_cast<int>(KnownAttribute::CallerMemberName), 46);
    EXPECT_EQ(static_cast<int>(KnownAttribute::CallerFilePath), 47);
    EXPECT_EQ(static_cast<int>(KnownAttribute::CallerLineNumber), 48);
    EXPECT_EQ(static_cast<int>(KnownAttribute::ScopedRef), 49);
    EXPECT_EQ(static_cast<int>(KnownAttribute::RequiresLocation), 50);
    EXPECT_EQ(static_cast<int>(KnownAttribute::IsUnmanaged), 51);
    EXPECT_EQ(static_cast<int>(KnownAttribute::MarshalAs), 52);
    EXPECT_EQ(static_cast<int>(KnownAttribute::PermissionSet), 53);
    EXPECT_EQ(static_cast<int>(KnownAttribute::NativeInteger), 54);
    EXPECT_EQ(static_cast<int>(KnownAttribute::PreserveBaseOverrides), 55);
    EXPECT_EQ(static_cast<int>(KnownAttribute::UnmanagedCallersOnly), 56);
    EXPECT_EQ(static_cast<int>(KnownAttribute::Required), 57);
    EXPECT_EQ(static_cast<int>(KnownAttribute::InlineArray), 58);
    EXPECT_EQ(static_cast<int>(KnownAttribute::ExtensionMarker), 59);
}

// ---------------------------------------------------------------------------
// KnownAttribute -- `KnownAttributeCount` is the enum extent (None..ExtensionMarker = 60).
// ---------------------------------------------------------------------------
TEST(KnownAttributeTest, KnownAttributeCountMatchesEnumExtent)
{
    EXPECT_EQ(KnownAttributeCount, 60);
    EXPECT_EQ(static_cast<int>(KnownAttribute::ExtensionMarker) + 1, KnownAttributeCount);
    // The type-name table has exactly one entry per enum value.
    EXPECT_EQ(static_cast<int>(KnownAttributeTypeNames().size()), KnownAttributeCount);
}

// ---------------------------------------------------------------------------
// KnownAttribute -- `GetTypeName` returns the metadata type name for a known attribute.
// ---------------------------------------------------------------------------
TEST(KnownAttributeTest, GetTypeNameReturnsConfiguredTypeNames)
{
    {
        const auto& tn = GetTypeName(KnownAttribute::CompilerGenerated);
        EXPECT_EQ(tn.Namespace(), "System.Runtime.CompilerServices");
        EXPECT_EQ(tn.Name(), "CompilerGeneratedAttribute");
        EXPECT_EQ(tn.TypeParameterCount(), 0);
    }
    {
        const auto& tn = GetTypeName(KnownAttribute::Extension);
        EXPECT_EQ(tn.Namespace(), "System.Runtime.CompilerServices");
        EXPECT_EQ(tn.Name(), "ExtensionAttribute");
    }
    {
        const auto& tn = GetTypeName(KnownAttribute::Obsolete);
        EXPECT_EQ(tn.Namespace(), "System");
        EXPECT_EQ(tn.Name(), "ObsoleteAttribute");
    }
    {
        const auto& tn = GetTypeName(KnownAttribute::DllImport);
        EXPECT_EQ(tn.Namespace(), "System.Runtime.InteropServices");
        EXPECT_EQ(tn.Name(), "DllImportAttribute");
    }
    {
        const auto& tn = GetTypeName(KnownAttribute::ParamArray);
        EXPECT_EQ(tn.Namespace(), "System");
        EXPECT_EQ(tn.Name(), "ParamArrayAttribute");
    }
    {
        const auto& tn = GetTypeName(KnownAttribute::ExtensionMarker);
        EXPECT_EQ(tn.Namespace(), "System.Runtime.CompilerServices");
        EXPECT_EQ(tn.Name(), "ExtensionMarkerAttribute");
    }
    // The reflection name (namespace + "." + name) round-trips for a sampled attribute.
    EXPECT_EQ(GetTypeName(KnownAttribute::Conditional).ReflectionName(),
              "System.Diagnostics.ConditionalAttribute");
}

// ---------------------------------------------------------------------------
// KnownAttribute -- the `None` (index 0) table entry is default-constructed, mirroring the
// C# `default` entry (the C# `GetTypeName` asserts `attr != None`, so it is read directly
// from the table, never via `GetTypeName`).
// ---------------------------------------------------------------------------
TEST(KnownAttributeTest, NoneEntryIsDefaultConstructed)
{
    const auto& none = KnownAttributeTypeNames()[0];
    EXPECT_EQ(none.Namespace(), "");
    EXPECT_EQ(none.Name(), "");
    EXPECT_EQ(none.TypeParameterCount(), 0);
}

// ---------------------------------------------------------------------------
// KnownAttribute -- every non-`None` table entry carries a real metadata type name, so the
// table covers the full enum extent.
// ---------------------------------------------------------------------------
TEST(KnownAttributeTest, TypeNamesTableCoversAllNonNoneAttributes)
{
    const auto& typeNames = KnownAttributeTypeNames();
    EXPECT_EQ(static_cast<int>(typeNames.size()), KnownAttributeCount);
    for (std::size_t i = 1; i < typeNames.size(); ++i) {
        EXPECT_FALSE(typeNames[i].Name().empty())
            << "table entry " << i << " should name a known attribute";
        EXPECT_FALSE(typeNames[i].Namespace().empty())
            << "table entry " << i << " should name a known attribute namespace";
    }
}

// ---------------------------------------------------------------------------
// KnownAttribute -- `GetTypeName` indexes the same table `KnownAttributeTypeNames` exposes,
// so each enum value's name comes from its table slot (pointer identity).
// ---------------------------------------------------------------------------
TEST(KnownAttributeTest, GetTypeNameMatchesAllTableEntries)
{
    const auto& typeNames = KnownAttributeTypeNames();
    for (int i = 1; i < KnownAttributeCount; ++i) {
        const auto attr = static_cast<KnownAttribute>(i);
        EXPECT_EQ(&GetTypeName(attr), &typeNames[static_cast<std::size_t>(i)])
            << "GetTypeName should return the table entry for index " << i;
    }
}

// ---------------------------------------------------------------------------
// KnownAttribute -- `IsCustomAttribute` is false for the pseudo-attributes the C# compiler
// folds into other metadata tables (NOT emitted as a real `CustomAttribute`).
// ---------------------------------------------------------------------------
TEST(KnownAttributeTest, IsCustomAttributeReturnsFalseForPseudoAttributes)
{
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::Serializable));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::ComImport));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::StructLayout));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::DllImport));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::PreserveSig));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::MethodImpl));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::FieldOffset));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::NonSerialized));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::MarshalAs));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::PermissionSet));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::Optional));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::DefaultParameterValue));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::In));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::Out));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::IndexerName));
    EXPECT_FALSE(IsCustomAttribute(KnownAttribute::SpecialName));
}

// ---------------------------------------------------------------------------
// KnownAttribute -- `IsCustomAttribute` is true for real custom attributes (and for `None`,
// which falls through the C# `default` arm).
// ---------------------------------------------------------------------------
TEST(KnownAttributeTest, IsCustomAttributeReturnsTrueForRealCustomAttributes)
{
    EXPECT_TRUE(IsCustomAttribute(KnownAttribute::None));
    EXPECT_TRUE(IsCustomAttribute(KnownAttribute::CompilerGenerated));
    EXPECT_TRUE(IsCustomAttribute(KnownAttribute::Extension));
    EXPECT_TRUE(IsCustomAttribute(KnownAttribute::Obsolete));
    EXPECT_TRUE(IsCustomAttribute(KnownAttribute::ParamArray));
    EXPECT_TRUE(IsCustomAttribute(KnownAttribute::Required));
    EXPECT_TRUE(IsCustomAttribute(KnownAttribute::InlineArray));
    EXPECT_TRUE(IsCustomAttribute(KnownAttribute::ExtensionMarker));
}
