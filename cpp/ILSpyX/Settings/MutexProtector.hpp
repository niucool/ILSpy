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

// Port of ICSharpCode.ILSpyX/Settings/MutexProtector.cs: the guard around
// a config-file read-modify-write so two processes editing the same
// sidecar fold their changes in turn instead of overwriting each other.
//
// C#-to-C++ porting decisions:
//  * The C# named Mutex ports to a POSIX advisory file lock (flock) on a
//    lock file named after the mutex under the temp directory -- the
//    cross-process guard a POSIX host has (a Windows named mutex needs
//    no file). The lock is held until destruction, exactly the C#
//    acquire-in-ctor/release-in-Dispose shape.
//  * The C# Mutex is re-entrant for the owning thread: nesting a second
//    MutexProtector on the same name from the same process succeeds. The
//    port keeps a process-local held-name set with a depth counter, so a
//    nested acquire is a no-op and the depth's last release drops the
//    lock (process-affine where the C# is thread-affine -- the CLI host
//    is single-process, and the settings sidecar it guards is shared
//    across processes, which is the direction that matters).
//  * The C# AbandonedMutexException catch has no POSIX analogue: the
//    kernel releases an flock when its owning process dies, so there is
//    no abandoned state to recover from.

#pragma once

#include <string>

namespace ILSpy::ILSpyX::Settings {

class MutexProtector {
public:
    // Acquires (blocks until held).
    explicit MutexProtector(std::string name);
    // Releases.
    ~MutexProtector();
    MutexProtector(const MutexProtector&) = delete;
    MutexProtector& operator=(const MutexProtector&) = delete;

private:
    std::string name_;
    int lockFd_ = -1;
    bool releasedDepth_ = false;
};

}  // namespace ILSpy::ILSpyX::Settings
