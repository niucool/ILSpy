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

// Tests for the `AbstractAnnotatable` annotation channel (cpp/Decompiler/CSharp/Syntax/
// AbstractAnnotatable.hpp, mirroring ICSharpCode.Decompiler/CSharp/Syntax/IAnnotatable.cs):
// the `Annotation` polymorphic base, `AddAnnotation`, `Annotation<T>` (is-a), `RemoveAnnotations<T>`,
// `Annotations`, and `CloneAnnotationsFrom` (clonable deep-copy vs non-clonable share). The
// `StubAnnotatable` exposes the `protected` `CloneAnnotationsFrom` so the clone/share behavior
// can be exercised.

#include "Decompiler/CSharp/Syntax/AbstractAnnotatable.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;

namespace {

// A non-clonable annotation (the `AnnotationBase::Clone` default returns null -- shared, not
// deep-copied). Carries a `Value` so a found annotation can be identified.
class StubAnnotation : public AnnotationBase {
public:
    int Value = 0;
    explicit StubAnnotation(int v = 0) : Value(v) {}
};

// A clonable annotation (overrides `Clone` to return a deep copy). The clone carries the
// same `Value`, but is a distinct object (so the clone/share distinction can be observed).
class ClonableStub : public AnnotationBase {
public:
    int Value;
    explicit ClonableStub(int v) : Value(v) {}
    std::shared_ptr<AnnotationBase> Clone() const override {
        return std::make_shared<ClonableStub>(Value);
    }
};

// A subtype of `ClonableStub` so the `Annotation<T>()` is-a lookup (dynamic_cast accepts
// subtypes, faithful to the C# `as T`) can be exercised: a stored `DerivedClonable` is found
// by `Annotation<ClonableStub>()`.
class DerivedClonable : public ClonableStub {
public:
    explicit DerivedClonable(int v) : ClonableStub(v) {}
};

// `AbstractAnnotatable`'s `CloneAnnotationsFrom` is `protected`; this stub exposes it so the
// clone/share behavior is testable.
class StubAnnotatable : public AbstractAnnotatable {
public:
    using AbstractAnnotatable::CloneAnnotationsFrom;
};

} // namespace

// ---- AddAnnotation / Annotation<T> ----------------------------------------

TEST(CSharp_AbstractAnnotatable, AddThenAnnotationReturnsIt) {
    StubAnnotatable a;
    auto ann = std::make_shared<StubAnnotation>(7);
    a.AddAnnotation(ann);
    EXPECT_EQ(a.Annotation<StubAnnotation>(), ann.get());
    EXPECT_EQ(a.Annotation<StubAnnotation>()->Value, 7);
    // A type that was never stored is not found.
    EXPECT_EQ(a.Annotation<ClonableStub>(), nullptr);
}

TEST(CSharp_AbstractAnnotatable, NoAnnotationsReturnsNull) {
    StubAnnotatable a;
    EXPECT_EQ(a.Annotation<StubAnnotation>(), nullptr);
}

TEST(CSharp_AbstractAnnotatable, AddAnnotationNullThrows) {
    StubAnnotatable a;
    EXPECT_THROW(a.AddAnnotation(nullptr), std::invalid_argument);
}

TEST(CSharp_AbstractAnnotatable, MultipleAnnotationsReturnFirstOfType) {
    StubAnnotatable a;
    auto first = std::make_shared<StubAnnotation>(1);
    auto clonable = std::make_shared<ClonableStub>(2);
    auto second = std::make_shared<StubAnnotation>(3);
    a.AddAnnotation(first);
    a.AddAnnotation(clonable);
    a.AddAnnotation(second);
    // Annotation<T>() returns the FIRST annotation of type T (is-a) in insertion order.
    EXPECT_EQ(a.Annotation<StubAnnotation>(), first.get());
    EXPECT_EQ(a.Annotation<ClonableStub>(), clonable.get());
}

TEST(CSharp_AbstractAnnotatable, AnnotationIsAAcceptsSubtype) {
    // A stored `DerivedClonable` is found by `Annotation<ClonableStub>()` (the dynamic_cast
    // is-a lookup, faithful to the C# `as T`).
    StubAnnotatable a;
    auto derived = std::make_shared<DerivedClonable>(42);
    a.AddAnnotation(derived);
    EXPECT_EQ(a.Annotation<ClonableStub>(), derived.get());
    EXPECT_EQ(a.Annotation<ClonableStub>()->Value, 42);
}

// ---- RemoveAnnotations<T> ------------------------------------------------

TEST(CSharp_AbstractAnnotatable, RemoveAnnotationsRemovesAllOfType) {
    StubAnnotatable a;
    auto s1 = std::make_shared<StubAnnotation>(1);
    auto c1 = std::make_shared<ClonableStub>(2);
    auto s2 = std::make_shared<StubAnnotation>(3);
    a.AddAnnotation(s1);
    a.AddAnnotation(c1);
    a.AddAnnotation(s2);
    a.RemoveAnnotations<StubAnnotation>();
    // Both StubAnnotations removed; the ClonableStub remains.
    EXPECT_EQ(a.Annotation<StubAnnotation>(), nullptr);
    EXPECT_EQ(a.Annotation<ClonableStub>(), c1.get());
}

TEST(CSharp_AbstractAnnotatable, RemoveAnnotationsNoOpWhenNone) {
    StubAnnotatable a;
    EXPECT_NO_THROW(a.RemoveAnnotations<StubAnnotation>());
    EXPECT_EQ(a.Annotation<StubAnnotation>(), nullptr);
}

TEST(CSharp_AbstractAnnotatable, RemoveAnnotationsIsARemovesSubtypes) {
    // A stored `DerivedClonable` (subtype of `ClonableStub`) is removed by
    // `RemoveAnnotations<ClonableStub>()` (the is-a lookup).
    StubAnnotatable a;
    auto derived = std::make_shared<DerivedClonable>(5);
    a.AddAnnotation(derived);
    a.RemoveAnnotations<ClonableStub>();
    EXPECT_EQ(a.Annotation<ClonableStub>(), nullptr);
}

// ---- Annotations() --------------------------------------------------------

TEST(CSharp_AbstractAnnotatable, AnnotationsReturnsAllInInsertionOrder) {
    StubAnnotatable a;
    auto s1 = std::make_shared<StubAnnotation>(1);
    auto c1 = std::make_shared<ClonableStub>(2);
    auto s2 = std::make_shared<StubAnnotation>(3);
    a.AddAnnotation(s1);
    a.AddAnnotation(c1);
    a.AddAnnotation(s2);
    auto all = a.Annotations();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0], s1.get());
    EXPECT_EQ(all[1], c1.get());
    EXPECT_EQ(all[2], s2.get());
}

TEST(CSharp_AbstractAnnotatable, AnnotationsEmptyWhenNone) {
    StubAnnotatable a;
    EXPECT_TRUE(a.Annotations().empty());
}

// ---- CloneAnnotationsFrom (clonable deep-copy vs non-clonable share) -------

TEST(CSharp_AbstractAnnotatable, CloneAnnotationsFromDeepCopiesClonable) {
    StubAnnotatable src;
    auto c = std::make_shared<ClonableStub>(99);
    src.AddAnnotation(c);

    StubAnnotatable dst;
    dst.CloneAnnotationsFrom(src);
    // The clone has a ClonableStub with the same Value, but a DISTINCT object (deep copy).
    ClonableStub* dstAnn = dst.Annotation<ClonableStub>();
    ASSERT_NE(dstAnn, nullptr);
    EXPECT_EQ(dstAnn->Value, 99);
    EXPECT_NE(dstAnn, c.get());  // deep copy, not the same object
}

TEST(CSharp_AbstractAnnotatable, CloneAnnotationsFromSharesNonClonable) {
    StubAnnotatable src;
    auto s = std::make_shared<StubAnnotation>(7);  // non-clonable (default Clone returns null)
    src.AddAnnotation(s);

    StubAnnotatable dst;
    dst.CloneAnnotationsFrom(src);
    // A non-clonable annotation is SHARED (the clone holds the same shared_ptr), matching the
    // C# MemberwiseClone shallow-copy + CloneAnnotations (which only clones ICloneable).
    StubAnnotation* dstAnn = dst.Annotation<StubAnnotation>();
    ASSERT_NE(dstAnn, nullptr);
    EXPECT_EQ(dstAnn, s.get());  // shared, the same object
}

TEST(CSharp_AbstractAnnotatable, CloneAnnotationsFromMixedClonableAndShared) {
    StubAnnotatable src;
    auto c = std::make_shared<ClonableStub>(1);  // clonable -> deep-copied
    auto s = std::make_shared<StubAnnotation>(2);  // non-clonable -> shared
    src.AddAnnotation(c);
    src.AddAnnotation(s);

    StubAnnotatable dst;
    dst.CloneAnnotationsFrom(src);
    EXPECT_NE(dst.Annotation<ClonableStub>(), c.get());    // deep copy
    EXPECT_EQ(dst.Annotation<ClonableStub>()->Value, 1);
    EXPECT_EQ(dst.Annotation<StubAnnotation>(), s.get());  // shared
}

TEST(CSharp_AbstractAnnotatable, CloneAnnotationsFromNoOpWhenSourceEmpty) {
    StubAnnotatable src;
    StubAnnotatable dst;
    dst.CloneAnnotationsFrom(src);  // source has no annotations
    EXPECT_TRUE(dst.Annotations().empty());
}
