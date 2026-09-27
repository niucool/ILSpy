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

#include "Decompiler/CSharp/RequiredImportsRecorder.hpp"

#include <vector>

namespace ILSpy::Decompiler::CSharp::RequiredImports {

namespace {

// The render thread's recording stack: each scope accumulates into the shared
// sink and carries its own current-namespace filter.
struct Scope {
    std::set<std::string>* sink;
    std::string currentNamespace;
};

thread_local std::vector<Scope> g_scopes;

// The C# FindRequiredImports.IsParentOfCurrentNamespace: the candidate is
// the global namespace (always a parent), or an exact/prefix parent of the
// current namespace at a '.' boundary. Such namespaces resolve through the
// enclosing namespace declaration and need no using.
bool IsParentOfCurrentNamespace(const std::string& ns,
                                const std::string& currentNamespace) {
    if (ns.empty())
        return true;
    if (currentNamespace.rfind(ns, 0) == 0) {
        if (currentNamespace.size() == ns.size())
            return true;
        if (currentNamespace[ns.size()] == '.')
            return true;
    }
    return false;
}

} // namespace

RecordingScope::RecordingScope(std::set<std::string>* sink,
                               const std::string& currentNamespace) {
    g_scopes.push_back({sink, currentNamespace});
}

RecordingScope::~RecordingScope() {
    if (!g_scopes.empty())
        g_scopes.pop_back();
}

bool IsRecording() {
    return !g_scopes.empty();
}

void RecordNamespace(const std::string& ns) {
    if (g_scopes.empty())
        return;
    const Scope& scope = g_scopes.back();
    if (scope.sink == nullptr)
        return;
    if (ns.empty())
        return;
    if (IsParentOfCurrentNamespace(ns, scope.currentNamespace))
        return;
    scope.sink->insert(ns);
}

void RecordTypeName(const std::string& reflectionName) {
    if (g_scopes.empty())
        return;
    // The namespace is everything before the last dot that sits OUTSIDE a
    // bracket region (a generic argument list's own dots would split the
    // wrong segment); a name with no such dot is a global-namespace or
    // already-short type and records nothing.
    int depth = 0;
    std::size_t dot = std::string::npos;
    for (std::size_t i = 0; i < reflectionName.size(); i++) {
        char c = reflectionName[i];
        if (c == '<' || c == '[' || c == '(')
            depth++;
        else if (c == '>' || c == ']' || c == ')')
            depth--;
        else if (c == '.' && depth == 0)
            dot = i;
    }
    if (dot == std::string::npos)
        return;
    RecordNamespace(reflectionName.substr(0, dot));
}

void RecordMethodTarget(const std::string& methodName) {
    if (g_scopes.empty())
        return;
    std::size_t sep = methodName.rfind("::");
    if (sep == std::string::npos)
        return;
    RecordTypeName(methodName.substr(0, sep));
}

} // namespace ILSpy::Decompiler::CSharp::RequiredImports
