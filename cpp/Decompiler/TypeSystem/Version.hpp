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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the BCL `System.Version` value type, the type `IModule.AssemblyVersion`
// returns by value. Absorbed into the `ILSpy::Decompiler::TypeSystem` namespace
// (the D384 `MethodSemanticsAttributes` / D381 `EntityHandle` BCL-absorption
// precedent: the C++ port has no `System` namespace mirror, so a BCL type the
// TypeSystem surface needs lands in `TypeSystem`). A four-component version
// number (Major/Minor/Build/Revision); `Build` and `Revision` are `-1` when
// unspecified, matching the `System.Version` convention, and `ToString` follows
// the `System.Version.ToString` shape exactly ("Major.Minor", with ".Build" and
// ".Revision" appended when each is specified). The decompiler's one consumer of
// the string form is `MetadataModule` formatting `assembly.Version.ToString()`
// into an `[AssemblyVersion]` attribute.

#pragma once

#include <string>

namespace ILSpy::Decompiler::TypeSystem {

struct Version {
    int Major = 0;
    int Minor = 0;
    int Build = -1;
    int Revision = -1;

    Version() = default;
    Version(int major, int minor) : Major(major), Minor(minor) {}
    Version(int major, int minor, int build) : Major(major), Minor(minor), Build(build) {}
    Version(int major, int minor, int build, int revision)
        : Major(major), Minor(minor), Build(build), Revision(revision) {}

    bool operator==(const Version& o) const noexcept {
        return Major == o.Major && Minor == o.Minor &&
               Build == o.Build && Revision == o.Revision;
    }
    bool operator!=(const Version& o) const noexcept { return !(*this == o); }

    // Mirrors `System.Version.ToString`: "Major.Minor" when `Build` is
    // unspecified (-1), "Major.Minor.Build" when `Revision` is unspecified,
    // "Major.Minor.Build.Revision" otherwise.
    std::string ToString() const {
        std::string r;
        r += std::to_string(Major);
        r += '.';
        r += std::to_string(Minor);
        if (Build != -1) {
            r += '.';
            r += std::to_string(Build);
            if (Revision != -1) {
                r += '.';
                r += std::to_string(Revision);
            }
        }
        return r;
    }
};

} // namespace ILSpy::Decompiler::TypeSystem
