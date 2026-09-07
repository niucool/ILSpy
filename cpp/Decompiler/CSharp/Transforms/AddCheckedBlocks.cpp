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

// AddCheckedBlocks annotation half -- see the header.

#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {
// The singletons (the C# static readonly fields; one shared object each).
const CheckedUncheckedAnnotation kCheckedAnnotation{true, false};
const CheckedUncheckedAnnotation kUncheckedAnnotation{false, false};
const CheckedUncheckedAnnotation kExplicitUncheckedAnnotation{false, true};
} // namespace

const CheckedUncheckedAnnotation& CheckedAnnotation()
{
    return kCheckedAnnotation;
}

const CheckedUncheckedAnnotation& UncheckedAnnotation()
{
    return kUncheckedAnnotation;
}

const CheckedUncheckedAnnotation& ExplicitUncheckedAnnotation()
{
    return kExplicitUncheckedAnnotation;
}

std::shared_ptr<CheckedUncheckedAnnotation> CheckedAnnotationHandle()
{
    return std::shared_ptr<CheckedUncheckedAnnotation>(
        const_cast<CheckedUncheckedAnnotation*>(&kCheckedAnnotation), [](CheckedUncheckedAnnotation*) {});
}

std::shared_ptr<CheckedUncheckedAnnotation> UncheckedAnnotationHandle()
{
    return std::shared_ptr<CheckedUncheckedAnnotation>(
        const_cast<CheckedUncheckedAnnotation*>(&kUncheckedAnnotation),
        [](CheckedUncheckedAnnotation*) {});
}

std::shared_ptr<CheckedUncheckedAnnotation> ExplicitUncheckedAnnotationHandle()
{
    return std::shared_ptr<CheckedUncheckedAnnotation>(
        const_cast<CheckedUncheckedAnnotation*>(&kExplicitUncheckedAnnotation),
        [](CheckedUncheckedAnnotation*) {});
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
