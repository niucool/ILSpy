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

#include "Decompiler/IL/Transforms/LocalFunctionDecompiler.hpp"

#include <cassert>

namespace ILSpy::Decompiler::IL {

void LocalFunctionDecompiler::Run(ILFunction& function,
                                  ILTransformContext& context) {
    if (!context.Settings.LocalFunctions) return;
    // The C# Run body (FindUseSites + the scope/capture machinery) is deferred
    // with the surfaces it needs; the shell keeps the settings gate the
    // pipeline wiring consults.
}

bool LocalFunctionDecompiler::ParseLocalFunctionName(const std::string& name,
                                                     std::string& callerName,
                                                     std::string& functionName) {
    callerName.clear();
    functionName.clear();
    // The C# regex `^<(.*)>g__([^\|]*)\|{0,1}\d+(_\d+)?$`; the port parses
    // the same anchored shape by hand (the CodeMappingInfo probe precedent).
    // `(.*)` is greedy, so the caller-name part runs to the LAST `>g__`.
    if (name.size() < 6 || name.front() != '<') return false;
    std::size_t close = std::string::npos;
    std::size_t searchFrom = 1;
    while (true) {
        const std::size_t found = name.find(">g__", searchFrom);
        if (found == std::string::npos) break;
        close = found;
        searchFrom = found + 1;
    }
    if (close == std::string::npos || close + 4 >= name.size()) return false;
    const std::size_t rest = close + 4;
    callerName = name.substr(1, close - 1);
    // `([^\|]*)` -- the function name up to the `|` ordinal suffix (or end).
    std::size_t ordinalStart = rest;
    while (ordinalStart < name.size() && name[ordinalStart] != '|')
        ordinalStart++;
    if (ordinalStart == rest) return false;  // an empty function name
    functionName = name.substr(rest, ordinalStart - rest);
    // `\|{0,1}\d+(_\d+)?$` -- an optional `|` then a decimal ordinal with an
    // optional `_<n>` suffix, running to the end.
    std::size_t pos = ordinalStart;
    if (pos < name.size() && name[pos] == '|') pos++;
    const std::size_t digitsStart = pos;
    while (pos < name.size() && name[pos] >= '0' && name[pos] <= '9') pos++;
    if (pos == digitsStart) return false;  // no ordinal digits
    if (pos < name.size() && name[pos] == '_') {
        pos++;
        const std::size_t suffixStart = pos;
        while (pos < name.size() && name[pos] >= '0' && name[pos] <= '9') pos++;
        if (pos == suffixStart) return false;  // `_<n>` requires digits
    }
    return pos == name.size();
}

} // namespace ILSpy::Decompiler::IL