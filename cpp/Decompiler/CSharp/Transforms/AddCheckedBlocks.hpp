// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the AddCheckedBlocks ANNOTATION half
// (ICSharpCode.Decompiler/CSharp/Transforms/AddCheckedBlocks.cs): the
// CheckedUncheckedAnnotation object and the three `static readonly object`
// annotations the translation layer attaches to cast/assignment expressions to
// drive the `checked`/`unchecked` block insertion. The transform itself (the
// block-rewriting IAstTransform) is deferred with the rest of the AST-transform
// layer; the annotation objects are what every expression-builder site reads
// and writes.

#pragma once

#include "Decompiler/CSharp/Syntax/AbstractAnnotatable.hpp"

#include <memory>

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `sealed class CheckedUncheckedAnnotation` (the AddCheckedBlocks private
// nested class): `IsChecked` (true=checked, false=unchecked) plus the
// `IsExplicit` flag the C# adds in the first `static` annotation. The port
// derives from AnnotationBase (the annotation channel's base).
class CheckedUncheckedAnnotation : public Syntax::AnnotationBase {
public:
    // The C# object-initializer form (`new CheckedUncheckedAnnotation { IsChecked =
    // true }`); a ctor instead of aggregate initialization because the AnnotationBase
    // base's virtual destructor makes the class a non-aggregate.
    CheckedUncheckedAnnotation(bool isChecked, bool isExplicit)
        : IsChecked(isChecked), IsExplicit(isExplicit)
    {
    }

    bool IsChecked = false;
    bool IsExplicit = false;
};

// The C# `public static readonly object CheckedAnnotation = new
// CheckedUncheckedAnnotation { IsChecked = true };` -- the Meyers-singleton
// form preserving the C# reference identity (the C# compares annotations by
// reference when checking for the blocks).
const CheckedUncheckedAnnotation& CheckedAnnotation();

// The C# `public static readonly object UncheckedAnnotation = new
// CheckedUncheckedAnnotation { IsChecked = false };`.
const CheckedUncheckedAnnotation& UncheckedAnnotation();

// The C# `public static readonly object ExplicitUncheckedAnnotation = new
// CheckedUncheckedAnnotation { IsChecked = false, IsExplicit = true };` --
// "an explicit unchecked(...) context is required" (the compile-time-constant
// cast that would otherwise overflow at compile time).
const CheckedUncheckedAnnotation& ExplicitUncheckedAnnotation();

// The shared_ptr handles the annotation channel takes (each call site adds a
// fresh handle sharing the singleton -- the C# stores the boxed object itself).
std::shared_ptr<CheckedUncheckedAnnotation> CheckedAnnotationHandle();
std::shared_ptr<CheckedUncheckedAnnotation> UncheckedAnnotationHandle();
std::shared_ptr<CheckedUncheckedAnnotation> ExplicitUncheckedAnnotationHandle();

} // namespace ILSpy::Decompiler::CSharp::Transforms
