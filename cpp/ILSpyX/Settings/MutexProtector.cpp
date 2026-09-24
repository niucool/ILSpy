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

// Implementation of ILSpyX/Settings/MutexProtector.hpp (the porting
// decisions are on the header).

#include "ILSpyX/Settings/MutexProtector.hpp"

#include <sys/file.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

namespace ILSpy::ILSpyX::Settings {
namespace {

// The process-local held-lock table: the name -> depth + fd. The C# named
// Mutex is re-entrant for the owning thread; the port's depth counter
// gives the same nesting semantics process-wide (the CLI host is
// single-process).
struct HeldLock {
    int fd = -1;
    int depth = 0;
};
std::mutex g_heldLocksMutex;
std::map<std::string, HeldLock>& HeldLocks()
{
    static std::map<std::string, HeldLock> locks;
    return locks;
}

// The lock file for a mutex name under the temp directory (path-unsafe
// characters sanitized).
std::filesystem::path LockFilePath(const std::string& name)
{
    std::string sanitized;
    for (char c : name) {
        sanitized.push_back(
            (c == '/' || c == '\\' || c == ':') ? '_' : c);
    }
    return std::filesystem::temp_directory_path()
        / ("ilspy_mutex_" + sanitized + ".lock");
}

}  // namespace

MutexProtector::MutexProtector(std::string name)
    : name_(std::move(name))
{
    std::lock_guard<std::mutex> guard(g_heldLocksMutex);
    HeldLock& held = HeldLocks()[name_];
    if (held.depth > 0) {
        // The re-entrant acquire (the C# same-thread WaitOne).
        held.depth++;
        return;
    }
    std::filesystem::path path = LockFilePath(name_);
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        // The C# Mutex ctor throws IOException for an uncreatable name;
        // the port's lock file is only uncreatable when the temp
        // directory itself is broken (the caller's settings sidecar
        // would fail there too).
        throw std::runtime_error("Cannot create the mutex lock file '"
            + path.string() + "'.");
    }
    // Blocks until held (the C# WaitOne on a mutex another process
    // owns).
    while (::flock(fd, LOCK_EX) != 0) {
    }
    held.fd = fd;
    held.depth = 1;
}

MutexProtector::~MutexProtector()
{
    std::lock_guard<std::mutex> guard(g_heldLocksMutex);
    auto it = HeldLocks().find(name_);
    if (it == HeldLocks().end() || it->second.depth <= 0)
        return;
    if (--it->second.depth > 0)
        return;
    ::flock(it->second.fd, LOCK_UN);
    ::close(it->second.fd);
    it->second.fd = -1;
}

}  // namespace ILSpy::ILSpyX::Settings
