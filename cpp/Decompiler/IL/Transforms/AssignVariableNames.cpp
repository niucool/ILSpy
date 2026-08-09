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

#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cctype>
#include <map>
#include <set>
#include <string>

namespace ILSpy::Decompiler::IL {

namespace {

// ECMA known-type -> default variable name, matching the C#
// typeNameToVariableNameDict (AssignVariableNames.cs).
const std::map<std::string, std::string>& TypeNameDict() {
    static const std::map<std::string, std::string> dict = {
        {"System.Boolean", "flag"},
        {"System.Byte", "b"}, {"System.SByte", "b"},
        {"System.Int16", "num"}, {"System.Int32", "num"}, {"System.Int64", "num"},
        {"System.UInt16", "num"}, {"System.UInt32", "num"}, {"System.UInt64", "num"},
        {"System.Single", "num"}, {"System.Double", "num"}, {"System.Decimal", "num"},
        {"System.String", "text"},
        {"System.Object", "obj"},
        {"System.Char", "c"},
    };
    return dict;
}

// Infer a base variable name from a type. Known primitive types use the dict;
// other types fall back to the lowercased short type name (System.Version ->
// "version", stripping a trailing `N generic-arity suffix). Returns "" for an
// unknown type so the caller keeps the original V_N name.
std::string InferName(const TypeSystem::IType* type) {
    if (!type) return "";
    std::string rn = type->ReflectionName();
    auto it = TypeNameDict().find(rn);
    if (it != TypeNameDict().end()) return it->second;
    // Short name: the segment after the last '.', lowercased first letter.
    auto pos = rn.rfind('.');
    std::string name = (pos != std::string::npos) ? rn.substr(pos + 1) : rn;
    auto bt = name.find('`');
    if (bt != std::string::npos) name = name.substr(0, bt);  // arity suffix
    if (name.empty()) return "";
    name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    return name;
}

} // namespace

void AssignVariableNames::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    // Names already taken: parameters (kept as-is) and locals renamed so far.
    std::set<std::string> taken;
    for (auto& v : function.Variables) {
        if (v && v->Kind == VariableKind::Parameter)
            taken.insert(v->Name);
    }
    for (auto& v : function.Variables) {
        if (!v || v->Kind == VariableKind::Parameter) continue;
        std::string base = InferName(v->Type.get());
        if (base.empty()) continue;  // leave V_N when the type is unknown
        std::string name = base;
        for (int i = 1; taken.count(name); ++i)
            name = base + "_" + std::to_string(i);
        taken.insert(name);
        v->Name = name;
    }
}

} // namespace ILSpy::Decompiler::IL
