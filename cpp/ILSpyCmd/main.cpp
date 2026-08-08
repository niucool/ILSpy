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

// Minimal CLI entry point. Phase 10 replaces this with the full ilspycmd
// option set (decompile, -t/-m, -il, --dump-table --json, -genpdb, ...). For
// now it only proves the cxxopts + ilspy link and gives --help/--version.

#include <cxxopts.hpp>

#include <iostream>

int main(int argc, char** argv) {
    cxxopts::Options options("ilspycmd",
        "C++ port of the ILSpy command-line decompiler (core + CLI)");
    options.allow_unrecognised_options();
    options.add_options()
        ("h,help", "Print help")
        ("v,version", "Print version");

    auto parsed = options.parse(argc, argv);
    if (parsed.count("help") != 0) {
        std::cout << options.help() << '\n';
        return 0;
    }
    if (parsed.count("version") != 0) {
        std::cout << "ilspycmd (C++ port) 1.0.0\n";
        return 0;
    }
    std::cout << "ilspycmd: no input given. See --help.\n"
                 "(Phase 10 of PORT_PLAN.md will implement decompilation.)\n";
    return 0;
}
