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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the System.Reflection.Metadata TypeName port (cpp/Decompiler/Metadata/
// TypeName.{hpp,cpp}), pinned against the real .NET 10 classes: every parse
// expectation, render, node count and exception message was dumped from the
// .NET 10.0.8 runtime driven over the identical case matrix (the
// C:\temp-probe\TypeNameProbe public-API probe; the port maps the C# exceptions
// as InvalidOperationException -> std::runtime_error, ArgumentException ->
// std::invalid_argument, ArgumentOutOfRangeException -> std::out_of_range).

#include "Decompiler/Metadata/TypeName.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

using ILSpy::Decompiler::Metadata::AssemblyNameInfo;
using ILSpy::Decompiler::Metadata::TypeName;
using ILSpy::Decompiler::Metadata::TypeNameParseOptions;

namespace {

// TryParse + fail-loud: a false result must never hand back a type name.
std::shared_ptr<TypeName> MustParse(const char* input)
{
    std::shared_ptr<TypeName> result;
    if (!TypeName::TryParse(input, result)) {
        ADD_FAILURE() << "TypeName::TryParse failed for [" << input << "]";
        return nullptr;
    }
    return result;
}

// Runs `action` and returns the exception message (or "<no-throw>").
template <typename F>
std::string WhatOf(F&& action)
{
    try {
        action();
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "<non-standard>";
    }
    return "<no-throw>";
}

TEST(TypeNameParse, SimpleAndNamespaceShapes)
{
    auto t = MustParse("System.Int32");
    ASSERT_NE(t, nullptr);
    EXPECT_TRUE(t->IsSimple());
    EXPECT_EQ(t->Name(), "Int32");
    EXPECT_EQ(t->Namespace(), "System");
    EXPECT_EQ(t->FullName(), "System.Int32");
    EXPECT_EQ(t->AssemblyQualifiedName(), "System.Int32");
    EXPECT_EQ(t->AssemblyName(), nullptr);
    EXPECT_EQ(t->GetNodeCount(), 1);

    auto bare = MustParse("Int32");
    EXPECT_EQ(bare->Namespace(), "");
    EXPECT_EQ(bare->Name(), "Int32");

    // Leading whitespace is trimmed; trailing whitespace is CONTENT -- the
    // name runs to the first delimiter, so it keeps its trailing spaces
    // (gold: Name = "Int32  ").
    auto lead = MustParse("  System.Int32");
    EXPECT_EQ(lead->FullName(), "System.Int32");
    auto trail = MustParse("System.Int32  ");
    EXPECT_EQ(trail->FullName(), "System.Int32  ");
    EXPECT_EQ(trail->Name(), "Int32  ");

    // Unicode namespaces decode through the UTF-16 unit view.
    auto unicode = MustParse("\xe7\xb3\xbb\xe7\xbb\x9f.Int32");
    EXPECT_EQ(unicode->Namespace(), "\xe7\xb3\xbb\xe7\xbb\x9f");
    EXPECT_EQ(unicode->Name(), "Int32");
}

TEST(TypeNameParse, AssemblyQualifiedSimpleNames)
{
    auto t = MustParse("System.Int32, mscorlib");
    EXPECT_EQ(t->AssemblyQualifiedName(), "System.Int32, mscorlib");
    ASSERT_NE(t->AssemblyName(), nullptr);
    EXPECT_EQ(t->AssemblyName()->FullName(), "mscorlib");
    EXPECT_EQ(t->AssemblyName()->Name(), "mscorlib");

    auto full = MustParse(
        "System.String, mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(full->FullName(), "System.String");
    EXPECT_EQ(
        full->AssemblyQualifiedName(),
        "System.String, mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089");
    ASSERT_NE(full->AssemblyName(), nullptr);
    EXPECT_EQ(
        full->AssemblyName()->FullName(),
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");

    // The type name before the first ',' keeps its trailing spaces; the
    // assembly part parses the "Version = x" spaced forms.
    auto spaced = MustParse("System.Int32 , mscorlib , Version = 4.0.0.0");
    EXPECT_EQ(spaced->Name(), "Int32 ");
    EXPECT_EQ(spaced->FullName(), "System.Int32 ");
    EXPECT_EQ(spaced->AssemblyQualifiedName(), "System.Int32 , mscorlib, Version=4.0.0.0");

    // The canonical re-render of a fully spaced attribute list.
    auto canonical = MustParse(
        " System.Int32, mscorlib, Version = 2.0.0.0 , Culture = neutral , "
        "PublicKeyToken = null ");
    EXPECT_EQ(
        canonical->AssemblyQualifiedName(),
        "System.Int32, mscorlib, Version=2.0.0.0, Culture=neutral, PublicKeyToken=null");
}

TEST(TypeNameParse, ArrayPointerByRefDecorators)
{
    auto sz = MustParse("System.Int32[]");
    EXPECT_TRUE(sz->IsArray());
    EXPECT_TRUE(sz->IsSZArray());
    EXPECT_FALSE(sz->IsVariableBoundArrayType());
    EXPECT_EQ(sz->Name(), "Int32[]");
    EXPECT_EQ(sz->FullName(), "System.Int32[]");
    EXPECT_EQ(sz->GetArrayRank(), 1);
    EXPECT_EQ(sz->GetElementType()->FullName(), "System.Int32");
    EXPECT_EQ(sz->Namespace(), "System");
    EXPECT_EQ(sz->GetNodeCount(), 2);

    auto star = MustParse("System.Int32[*]");
    EXPECT_TRUE(star->IsArray());
    EXPECT_FALSE(star->IsSZArray());
    EXPECT_TRUE(star->IsVariableBoundArrayType());
    EXPECT_EQ(star->GetArrayRank(), 1);
    EXPECT_EQ(star->Name(), "Int32[*]");

    auto rank2 = MustParse("System.Int32[,]");
    EXPECT_EQ(rank2->GetArrayRank(), 2);
    EXPECT_EQ(rank2->Name(), "Int32[,]");

    auto rank4 = MustParse("System.Int32[,,,]");
    EXPECT_EQ(rank4->GetArrayRank(), 4);
    EXPECT_EQ(rank4->Name(), "Int32[,,,]");

    auto ptr = MustParse("System.Int32*");
    EXPECT_TRUE(ptr->IsPointer());
    EXPECT_EQ(ptr->Name(), "Int32*");
    EXPECT_EQ(ptr->GetElementType()->FullName(), "System.Int32");

    auto byref = MustParse("System.Int32&");
    EXPECT_TRUE(byref->IsByRef());
    EXPECT_EQ(byref->Name(), "Int32&");

    // The decorator chain wraps outermost-last: "*&" is a byref of a pointer,
    // "[]&" a byref of an array, "&[]" an array of a byref.
    auto ptrByRef = MustParse("System.Int32*&");
    EXPECT_TRUE(ptrByRef->IsByRef());
    EXPECT_EQ(ptrByRef->Name(), "Int32*&");
    EXPECT_TRUE(ptrByRef->GetElementType()->IsPointer());
    EXPECT_EQ(ptrByRef->GetNodeCount(), 3);

    auto arrByRef = MustParse("System.Int32[]&");
    EXPECT_TRUE(arrByRef->IsByRef());
    EXPECT_TRUE(arrByRef->GetElementType()->IsArray());
    EXPECT_EQ(arrByRef->Name(), "Int32[]&");

    auto byRefArr = MustParse("System.Int32&[]");
    EXPECT_TRUE(byRefArr->IsArray());
    EXPECT_TRUE(byRefArr->GetElementType()->IsByRef());
    EXPECT_EQ(byRefArr->Name(), "Int32&[]");

    // Stacked decorators: "[][*]" is a variable-bound array of an SZ array.
    auto stacked = MustParse("System.Int32[][*]");
    EXPECT_EQ(stacked->Name(), "Int32[][*]");
    EXPECT_EQ(stacked->GetElementType()->FullName(), "System.Int32[]");
    EXPECT_EQ(stacked->GetNodeCount(), 3);
}

TEST(TypeNameParse, NestedTypes)
{
    auto t = MustParse("Ns.Outer+Inner+Innermost");
    EXPECT_TRUE(t->IsNested());
    EXPECT_EQ(t->Name(), "Innermost");
    EXPECT_EQ(t->FullName(), "Ns.Outer+Inner+Innermost");
    EXPECT_EQ(t->GetNodeCount(), 3);
    auto declaring = t->DeclaringType();
    EXPECT_EQ(declaring->FullName(), "Ns.Outer+Inner");
    EXPECT_EQ(declaring->Name(), "Inner");
    EXPECT_TRUE(declaring->IsNested());
    EXPECT_EQ(declaring->DeclaringType()->FullName(), "Ns.Outer");
    EXPECT_EQ(declaring->DeclaringType()->Name(), "Outer");
    EXPECT_EQ(declaring->DeclaringType()->Namespace(), "Ns");

    auto two = MustParse("Outer+Inner");
    EXPECT_EQ(two->Name(), "Inner");
    EXPECT_EQ(two->DeclaringType()->FullName(), "Outer");
    EXPECT_EQ(two->DeclaringType()->Name(), "Outer");
    EXPECT_EQ(two->DeclaringType()->Namespace(), "");

    // The nested+generic combination: the arguments decorate the innermost
    // segment, the declaring chain is the generic definition's.
    auto nestedGeneric = MustParse("System.Collections.Generic.List`1+Enumerator[[System.Int32, mscorlib]]");
    EXPECT_TRUE(nestedGeneric->IsConstructedGenericType());
    EXPECT_TRUE(nestedGeneric->IsNested());
    EXPECT_EQ(nestedGeneric->Name(), "Enumerator");
    EXPECT_EQ(nestedGeneric->FullName(),
              "System.Collections.Generic.List`1+Enumerator[[System.Int32, mscorlib]]");
    EXPECT_EQ(nestedGeneric->GetGenericTypeDefinition()->FullName(),
              "System.Collections.Generic.List`1+Enumerator");
    EXPECT_EQ(nestedGeneric->DeclaringType()->FullName(),
              "System.Collections.Generic.List`1");
    EXPECT_EQ(nestedGeneric->DeclaringType()->Namespace(),
              "System.Collections.Generic");
    EXPECT_EQ(nestedGeneric->GetNodeCount(), 4);
}

TEST(TypeNameParse, ConstructedGenericShapes)
{
    auto t = MustParse(
        "System.Collections.Generic.List`1[[System.Int32, mscorlib, Version=4.0.0.0, "
        "Culture=neutral, PublicKeyToken=b77a5c561934e089]], mscorlib");
    EXPECT_TRUE(t->IsConstructedGenericType());
    EXPECT_EQ(t->Name(), "List`1");
    EXPECT_EQ(t->Namespace(), "System.Collections.Generic");
    EXPECT_EQ(
        t->FullName(),
        "System.Collections.Generic.List`1[[System.Int32, mscorlib, Version=4.0.0.0, "
        "Culture=neutral, PublicKeyToken=b77a5c561934e089]]");
    EXPECT_EQ(
        t->AssemblyQualifiedName(),
        "System.Collections.Generic.List`1[[System.Int32, mscorlib, Version=4.0.0.0, "
        "Culture=neutral, PublicKeyToken=b77a5c561934e089]], mscorlib");
    EXPECT_EQ(t->GetGenericTypeDefinition()->AssemblyQualifiedName(),
              "System.Collections.Generic.List`1, mscorlib");
    ASSERT_EQ(t->GetGenericArguments().size(), 1u);
    EXPECT_EQ(
        t->GetGenericArguments()[0]->AssemblyQualifiedName(),
        "System.Int32, mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(t->GetNodeCount(), 3);

    // The single-bracket argument form: no per-argument assembly names.
    auto single = MustParse("System.Collections.Generic.List`1[[System.Int32]]");
    EXPECT_EQ(single->FullName(), "System.Collections.Generic.List`1[[System.Int32]]");
    EXPECT_EQ(single->GetGenericArguments()[0]->FullName(), "System.Int32");
    EXPECT_EQ(single->AssemblyName(), nullptr);

    auto dictionary = MustParse(
        "System.Collections.Generic.Dictionary`2[[System.String, mscorlib],"
        "[System.Int32, mscorlib]]");
    ASSERT_EQ(dictionary->GetGenericArguments().size(), 2u);
    EXPECT_EQ(dictionary->GetGenericArguments()[0]->AssemblyQualifiedName(),
              "System.String, mscorlib");
    EXPECT_EQ(dictionary->GetGenericArguments()[1]->AssemblyQualifiedName(),
              "System.Int32, mscorlib");
    EXPECT_EQ(dictionary->GetNodeCount(), 4);

    // The short single-bracket form without inner brackets.
    auto shortForm = MustParse("List`1[Int32, Int32]");
    EXPECT_EQ(shortForm->FullName(), "List`1[[Int32],[Int32]]");
    ASSERT_EQ(shortForm->GetGenericArguments().size(), 2u);
    EXPECT_EQ(shortForm->GetGenericArguments()[0]->FullName(), "Int32");

    // Whitespace after argument separators is allowed in the double form.
    auto spaced = MustParse("List`1[[System.Int32, mscorlib], [System.Int32, mscorlib]]");
    ASSERT_EQ(spaced->GetGenericArguments().size(), 2u);
    EXPECT_EQ(spaced->GetGenericArguments()[1]->FullName(), "System.Int32");

    // A nested constructed argument carries its own assembly name.
    auto tuple = MustParse(
        "System.Tuple`2[[System.Int32, mscorlib],"
        "[System.Tuple`1[[System.Int32, mscorlib]], mscorlib]]");
    EXPECT_EQ(
        tuple->FullName(),
        "System.Tuple`2[[System.Int32, mscorlib],"
        "[System.Tuple`1[[System.Int32, mscorlib]], mscorlib]]");
    EXPECT_EQ(tuple->GetNodeCount(), 6);

    // An array-typed argument through the single-bracket form, and an array
    // OF a generic definition.
    auto arrayOfArg = MustParse("System.Collections.Generic.List`1[System.Int32[]]");
    EXPECT_EQ(arrayOfArg->FullName(),
              "System.Collections.Generic.List`1[[System.Int32[]]]");
    EXPECT_EQ(arrayOfArg->GetGenericArguments()[0]->Name(), "Int32[]");
    EXPECT_EQ(arrayOfArg->GetNodeCount(), 4);

    auto arrayOfGeneric = MustParse("System.Collections.Generic.List`1[]");
    EXPECT_TRUE(arrayOfGeneric->IsArray());
    EXPECT_EQ(arrayOfGeneric->GetElementType()->FullName(),
              "System.Collections.Generic.List`1");
}

TEST(TypeNameParse, EscapedDelimitersKeepTheirBackslash)
{
    // The escape keeps the delimiter inside the name -- and the stored name
    // keeps the backslash too (gold: Name "A\\+B", Namespace "").
    auto escaped = MustParse("A\\+B");
    EXPECT_EQ(escaped->Name(), "A\\+B");
    EXPECT_EQ(escaped->Namespace(), "");
    EXPECT_EQ(escaped->FullName(), "A\\+B");
    EXPECT_FALSE(escaped->IsNested());

    // An escaped '+' is transparent to the nested scan: the split happens at
    // the SECOND (unescaped) '+'.
    auto half = MustParse("A\\+B+C");
    EXPECT_TRUE(half->IsNested());
    EXPECT_EQ(half->Name(), "C");
    EXPECT_EQ(half->DeclaringType()->FullName(), "A\\+B");
    EXPECT_EQ(half->DeclaringType()->Name(), "A\\+B");
    EXPECT_EQ(half->FullName(), "A\\+B+C");

    auto escapedComma = MustParse("System.Int32\\,x");
    EXPECT_EQ(escapedComma->Name(), "Int32\\,x");
    EXPECT_EQ(escapedComma->Namespace(), "System");
    EXPECT_EQ(escapedComma->FullName(), "System.Int32\\,x");

    // A backslash before a non-escapable character is malformed.
    std::shared_ptr<TypeName> bad;
    EXPECT_FALSE(TypeName::TryParse("System\\.Int32", bad));
    EXPECT_FALSE(TypeName::TryParse("System.Int32\\", bad));
}

TEST(TypeNameParse, TypeParameterLikeNamesParseAsSimpleNames)
{
    // The ILSpy reflection-name forms for open types: the parser is generic
    // over names and accepts them verbatim (the resolution of ``0/`0 to type
    // parameters happens in ReflectionHelper.ResolveTypeName, not here).
    EXPECT_EQ(MustParse("`0")->Name(), "`0");
    EXPECT_EQ(MustParse("``0")->Name(), "``0");
    EXPECT_EQ(MustParse("``abc")->Name(), "``abc");
}

TEST(TypeNameParse, AssemblyQuotingAndEscapes)
{
    auto quoted = MustParse("System.Int32, \"My,Name\", Version=1.0.0.0");
    ASSERT_NE(quoted->AssemblyName(), nullptr);
    EXPECT_EQ(quoted->AssemblyName()->Name(), "My,Name");
    // The FullName re-escapes the name (AppendQuoted escapes every ','), so
    // the round-trip through AssemblyQualifiedName is lossy in form only.
    EXPECT_EQ(quoted->AssemblyQualifiedName(),
              "System.Int32, My\\,Name, Version=1.0.0.0");

    auto single = MustParse("System.Int32, 'My,Name', Version=1.0.0.0");
    EXPECT_EQ(single->AssemblyName()->Name(), "My,Name");

    auto escapedName = MustParse("System.Int32, My\\,Name");
    ASSERT_NE(escapedName->AssemblyName(), nullptr);
    EXPECT_EQ(escapedName->AssemblyName()->Name(), "My,Name");
    EXPECT_EQ(escapedName->AssemblyName()->FullName(), "My\\,Name");

    auto doubleBackslash = MustParse("System.Int32, My\\\\Name");
    EXPECT_EQ(doubleBackslash->AssemblyName()->Name(), "My\\Name");
    EXPECT_EQ(doubleBackslash->AssemblyName()->FullName(), "My\\\\Name");

    auto tabEscape = MustParse("System.Int32, My\\tName");
    EXPECT_EQ(tabEscape->AssemblyName()->Name(), "My\tName");
    EXPECT_EQ(tabEscape->AssemblyName()->FullName(), "My\\tName");
}

TEST(TypeNameParse, AssemblyAttributeForms)
{
    // Every attribute-form arm through the TypeName surface (the AssemblyNameInfo
    // suite covers the direct matrix in depth).
    struct Form {
        const char* input;
        const char* asmFullName;
    };
    Form forms[] = {
        {"System.Int32, mscorlib, Culture=neutral", "mscorlib, Culture=neutral"},
        {"System.Int32, mscorlib, Culture=de-DE", "mscorlib, Culture=de-DE"},
        {"System.Int32, mscorlib, PublicKeyToken=null", "mscorlib, PublicKeyToken=null"},
        {"System.Int32, mscorlib, PublicKeyToken=b77a5c561934e089",
         "mscorlib, PublicKeyToken=b77a5c561934e089"},
        {"System.Int32, mscorlib, PublicKeyToken=B77A5C561934E089",
         "mscorlib, PublicKeyToken=b77a5c561934e089"},
        {"System.Int32, mscorlib, PublicKey=b77a5c561934e089",
         "mscorlib, PublicKey=b77a5c561934e089"},
        {"System.Int32, mscorlib, PublicKey=0011", "mscorlib, PublicKey=0011"},
        {"System.Int32, mscorlib, Retargetable=Yes", "mscorlib, Retargetable=Yes"},
        {"System.Int32, mscorlib, Retargetable=no", "mscorlib"},
        {"System.Int32, mscorlib, ContentType=WindowsRuntime",
         "mscorlib, ContentType=WindowsRuntime"},
        {"System.Int32, mscorlib, ContentType=windowsruntime",
         "mscorlib, ContentType=WindowsRuntime"},
        {"System.Int32, mscorlib, Version=1.2.65535", "mscorlib, Version=1.2"},
        {"System.Int32, mscorlib, Unknown=1", "mscorlib"},
        {"My, Name, Version=1.0.0.0", "Name, Version=1.0.0.0"},
        {"System.Int32, mscorlib, Culture=neutral, PublicKeyToken=b77a5c561934e089, "
         "Retargetable=Yes",
         "mscorlib, Culture=neutral, PublicKeyToken=b77a5c561934e089, Retargetable=Yes"},
    };
    for (const Form& form : forms) {
        std::shared_ptr<TypeName> t;
        ASSERT_TRUE(TypeName::TryParse(form.input, t)) << form.input;
        ASSERT_NE(t->AssemblyName(), nullptr) << form.input;
        EXPECT_EQ(t->AssemblyName()->FullName(), form.asmFullName) << form.input;
    }

    // The failure matrix: every form the real parser rejects.
    const char* failures[] = {
        "System.Int32, mscorlib, extra",
        "System.Int32,",
        "System.Int32, ",
        "",
        "   ",
        "[",
        "]",
        ",",
        "System.Int32[[",
        "List`1[Int32,,]",
        "System.Int32, mscorlib, Version=65535.1",
        "System.Int32, mscorlib, Version=1.2.3.4.5",
        "System.Int32, mscorlib, Version=1",
        "System.Int32, mscorlib, Version=+1.0",
        "System.Int32, mscorlib, PublicKeyToken=xyz",
        "System.Int32, mscorlib, PublicKeyToken=b77a5c561934e08",
        "System.Int32, mscorlib, PublicKey=",
        "System.Int32, mscorlib, Retargetable=maybe",
        "System.Int32, mscorlib, ContentType=Default",
        "System.Int32, mscorlib, ProcessorArchitecture=sparc",
        "System.Int32, My\\qName",
        "System.Int32, Version=1.0.0.0, Version=2.0.0.0",
        "System.Int32, mscorlib, =",
    };
    for (const char* input : failures) {
        std::shared_ptr<TypeName> t;
        EXPECT_FALSE(TypeName::TryParse(input, t)) << "[" << input << "]";
    }
}

TEST(TypeNameParse, VersionSentinelFormsRenderShorter)
{
    // The ushort sentinel collision: a component equal to 65535 reads as
    // absent, so the canonical render is shorter than the input.
    struct Form {
        const char* input;
        const char* aqn;
        bool ok;
    };
    Form forms[] = {
        {"T, A, Version=1.2.65535.65535", "T, A, Version=1.2", true},
        {"T, A, Version=1.2.3.65535", "T, A, Version=1.2.3", true},
        {"T, A, Version=1.2.65535.1", "T, A, Version=1.2", true},
        {"T, A, Version=65535.65535", "", false},
    };
    for (const Form& form : forms) {
        std::shared_ptr<TypeName> t;
        ASSERT_EQ(TypeName::TryParse(form.input, t), form.ok) << form.input;
        if (form.ok)
            EXPECT_EQ(t->AssemblyQualifiedName(), form.aqn) << form.input;
    }
}

TEST(TypeNameParseThrowArms, ParseThrowsWithTheNetMessages)
{
    // ArgumentException carries the consumed-prefix index in the parameter
    // name (the port's documented divergence note: the text keeps the
    // formatted suffix).
    std::string message = WhatOf([] {
        TypeName::Parse("System.Int32[");
    });
    EXPECT_EQ(message, "The name of the type is invalid. (Parameter 'typeName@12')");

    message = WhatOf([] { TypeName::Parse(""); });
    EXPECT_EQ(message, "The name of the type is invalid. (Parameter 'typeName@0')");

    message = WhatOf([] { TypeName::Parse("  "); });
    EXPECT_EQ(message, "The name of the type is invalid. (Parameter 'typeName@0')");

    message = WhatOf([] { TypeName::Parse("System.Int32[]junk"); });
    EXPECT_EQ(message, "The name of the type is invalid. (Parameter 'typeName@14')");

    message = WhatOf([] { TypeName::Parse("[x]"); });
    EXPECT_EQ(message, "The name of the type is invalid. (Parameter 'typeName@0')");
}

TEST(TypeNameMaxNodes, TheDiveCounterBoundsTheNodeCount)
{
    // depth = 2 arrays: 3 nodes.
    auto two = MustParse("System.Int32[][]");
    EXPECT_EQ(two->GetNodeCount(), 3);

    auto five = MustParse("System.Int32[][][][][]");
    EXPECT_EQ(five->GetNodeCount(), 6);

    // 20 decorators -> 21 nodes: rejected at the default MaxNodes 20.
    std::string twenty = "System.Int32";
    for (int i = 0; i < 20; i++)
        twenty += "[]";
    std::shared_ptr<TypeName> t;
    EXPECT_FALSE(TypeName::TryParse(twenty, t));

    // 19 decorators -> 20 nodes: accepted.
    std::string nineteen = "System.Int32";
    for (int i = 0; i < 19; i++)
        nineteen += "[]";
    EXPECT_TRUE(TypeName::TryParse(nineteen, t));
    EXPECT_EQ(t->GetNodeCount(), 20);

    // Raising the budget admits the 21-node name.
    TypeNameParseOptions options;
    options.SetMaxNodes(21);
    EXPECT_TRUE(TypeName::TryParse(twenty, t, &options));

    // Nested segments and generic arguments consume the same budget.
    std::string nested21 = "A+B+C+D+E+F+G+H+I+J+K+L+M+N+O+P+Q+R+S+T+U";
    EXPECT_FALSE(TypeName::TryParse(nested21, t));

    std::string deepGeneric =
        "G`1[G`1[G`1[G`1[G`1[G`1[G`1[G`1[G`1[G`1[G`1[G`1[System.Int32]]]]]]]]]]]]";
    EXPECT_FALSE(TypeName::TryParse(deepGeneric, t));
    EXPECT_FALSE(TypeName::TryParse(deepGeneric, t, &options));
    options.SetMaxNodes(25);
    EXPECT_TRUE(TypeName::TryParse(deepGeneric, t, &options));

    // The throwing Parse arm reports the node budget on the overflow.
    std::string message = WhatOf([&nested21] { TypeName::Parse(nested21); });
    EXPECT_EQ(message, "Maximum node count of 20 exceeded.");
}

TEST(TypeNameParseOptions, MaxNodesMustBePositive)
{
    TypeNameParseOptions options;
    EXPECT_EQ(options.MaxNodes(), 20);
    std::string message = WhatOf([&options] { options.SetMaxNodes(0); });
    EXPECT_EQ(message,
              "value ('0') must be greater than '0'. (Parameter 'value')\nActual value was 0.");
    message = WhatOf([&options] { options.SetMaxNodes(-3); });
    EXPECT_EQ(message,
              "value ('-3') must be greater than '0'. (Parameter 'value')\nActual value was "
              "-3.");
    options.SetMaxNodes(100);
    EXPECT_EQ(options.MaxNodes(), 100);
}

TEST(TypeNameAccessorViolations, ThrowTheNetMessages)
{
    auto simple = MustParse("System.Int32");
    auto array = MustParse("System.Int32[]");
    auto nested = MustParse("Ns.Outer+Inner");
    auto generic = MustParse("List`1[[System.Int32, mscorlib]]");

    std::string message = WhatOf([&simple] { simple->GetElementType(); });
    EXPECT_EQ(message,
              "This operation is only valid on arrays, pointers and references.");

    message = WhatOf([&simple] { simple->GetGenericTypeDefinition(); });
    EXPECT_EQ(message, "This operation is only valid on generic types.");

    message = WhatOf([&simple] { simple->DeclaringType(); });
    EXPECT_EQ(message, "This operation is only valid on nested types.");

    message = WhatOf([&nested] { nested->Namespace(); });
    EXPECT_EQ(message, "Cannot retrieve the namespace of a nested type.");

    // The namespace of an array of a nested type walks to the nested element
    // and throws the same way.
    message = WhatOf([] {
        std::shared_ptr<TypeName> t;
        TypeName::TryParse("A+B[]", t);
        return t->Namespace();
    });
    EXPECT_EQ(message, "Cannot retrieve the namespace of a nested type.");

    message = WhatOf([&simple] { simple->GetArrayRank(); });
    EXPECT_EQ(message, "Must be an array type.");

    message = WhatOf([&generic] { generic->GetArrayRank(); });
    EXPECT_EQ(message, "Must be an array type.");

    message = WhatOf([&generic] { generic->WithAssemblyName(nullptr); });
    EXPECT_EQ(message, "'List`1[[System.Int32, mscorlib]]' is not a simple TypeName.");

    message = WhatOf([&array] {
        array->MakeGenericTypeName({MustParse("System.Int32")});
    });
    EXPECT_EQ(message, "'System.Int32[]' is not a simple TypeName.");

    message = WhatOf([&simple] { simple->MakeArrayTypeName(0); });
    EXPECT_EQ(message,
              "Specified argument was out of the range of valid values. (Parameter 'rank')");
    message = WhatOf([&simple] { simple->MakeArrayTypeName(-1); });
    EXPECT_EQ(message,
              "Specified argument was out of the range of valid values. (Parameter 'rank')");
}

TEST(TypeNameMake, DecoratorAndGenericFactories)
{
    auto simple = MustParse("System.Int32");

    auto sz = simple->MakeSZArrayTypeName();
    EXPECT_EQ(sz->FullName(), "System.Int32[]");
    EXPECT_TRUE(sz->IsSZArray());

    auto rank2 = simple->MakeArrayTypeName(2);
    EXPECT_EQ(rank2->Name(), "Int32[,]");
    EXPECT_TRUE(rank2->IsVariableBoundArrayType());
    EXPECT_EQ(rank2->GetArrayRank(), 2);

    // No rank-32 cap exists in the implementation.
    auto rank33 = simple->MakeArrayTypeName(33);
    EXPECT_EQ(rank33->GetArrayRank(), 33);

    auto ptr = simple->MakePointerTypeName();
    EXPECT_EQ(ptr->Name(), "Int32*");

    auto byref = simple->MakeByRefTypeName();
    EXPECT_EQ(byref->Name(), "Int32&");

    auto generic = simple->MakeGenericTypeName(
        {MustParse("System.String, mscorlib"), MustParse("System.Int32")});
    EXPECT_TRUE(generic->IsConstructedGenericType());
    EXPECT_EQ(generic->FullName(), "System.Int32[[System.String, mscorlib],[System.Int32]]");
    ASSERT_EQ(generic->GetGenericArguments().size(), 2u);
    EXPECT_EQ(generic->GetGenericArguments()[0]->AssemblyQualifiedName(),
              "System.String, mscorlib");
    EXPECT_EQ(generic->GetNodeCount(), 4);
}

TEST(TypeNameMake, WithAssemblyNameCopiesTheSimpleShape)
{
    auto nested = MustParse("Ns.Outer+Inner, mscorlib, Version=4.0.0.0");
    auto cleared = nested->WithAssemblyName(nullptr);
    EXPECT_EQ(cleared->FullName(), "Ns.Outer+Inner");
    EXPECT_EQ(cleared->AssemblyQualifiedName(), "Ns.Outer+Inner");
    EXPECT_EQ(cleared->AssemblyName(), nullptr);
    EXPECT_TRUE(cleared->IsNested());

    auto simple = MustParse("System.Int32");
    auto with = simple->WithAssemblyName(
        AssemblyNameInfo::Parse("mscorlib, Version=4.0.0.0"));
    EXPECT_EQ(with->AssemblyQualifiedName(), "System.Int32, mscorlib, Version=4.0.0.0");
}

TEST(TypeNameUnescape, CollapsesEscapesExactly)
{
    EXPECT_EQ(TypeName::Unescape("a\\+b"), "a+b");
    EXPECT_EQ(TypeName::Unescape("a\\\\+b"), "a\\+b");
    EXPECT_EQ(TypeName::Unescape("a\\"), "a\\");
    EXPECT_EQ(TypeName::Unescape("\\"), "\\");
    EXPECT_EQ(TypeName::Unescape("ab"), "ab");
    EXPECT_EQ(TypeName::Unescape("a\\\\b"), "a\\b");
    EXPECT_EQ(TypeName::Unescape("\\\\x"), "\\x");
    // (The null-input ArgumentNullException arm has no UTF-8 stand-in at the
    // port's string boundary; every ported caller passes real strings.)
}

} // namespace
