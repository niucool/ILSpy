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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The flat render's port of the C# IntroduceUsingDeclarations.FindRequiredImports
// pass: the C# walks the finished syntax tree's SimpleType nodes (with their
// TypeResolveResult annotations) and collects the namespaces of the type references
// that actually render -- a using directive is emitted for a namespace only when
// some rendered reference needs it. The flat render has no surviving syntax tree, so
// the equivalent is a recording side channel: the render driver arms a sink before
// rendering and every short-name emission site records its target's namespace. The
// current-type namespace filter mirrors the C#'s IsParentOfCurrentNamespace -- a
// reference inside `namespace N { }` to a type in N (or a parent of N) resolves
// through the namespace declaration and needs no using.

#pragma once

#include <set>
#include <string>

namespace ILSpy::Decompiler::CSharp::RequiredImports {

// Arms the recording for one render scope. Nested scopes (the whole-module
// loop rendering type after type) accumulate into the shared sink; each
// type's scope carries its own current-namespace filter. Re-arming with a
// null sink suspends recording (the tests' direct renderer calls).
class RecordingScope {
public:
    RecordingScope(std::set<std::string>* sink,
                   const std::string& currentNamespace);
    ~RecordingScope();

    RecordingScope(const RecordingScope&) = delete;
    RecordingScope& operator=(const RecordingScope&) = delete;
};

// True when a recording scope is active.
bool IsRecording();

// Records one referenced type's namespace (already split). Applies the
// current-namespace filter and drops the empty namespace.
void RecordNamespace(const std::string& ns);

// Records the namespace of a reflection-style dotted type name
// ("System.Collections.Generic.List" or "Ns.Outer+Inner"): everything
// before the last dot. Names with no dot (the bare nested name or a
// global-namespace type) record nothing.
void RecordTypeName(const std::string& reflectionName);

// Records the namespace of a `Namespace.Type::Member` method name: the
// part before the "::", then everything before its last dot.
void RecordMethodTarget(const std::string& methodName);

} // namespace ILSpy::Decompiler::CSharp::RequiredImports
