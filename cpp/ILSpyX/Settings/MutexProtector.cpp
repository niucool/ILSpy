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

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#ifdef _WIN32
#include "Decompiler/Util/Utf.hpp"
#endif

namespace ILSpy::ILSpyX::Settings {
namespace {

// The process-local held-lock table: the name -> lock handle + depth. The
// C# named Mutex is re-entrant for the owning thread; the port's depth
// counter gives the same nesting semantics process-wide (the CLI host is
// single-process).
#ifdef _WIN32
struct HeldLock {
    void* handle = nullptr;  // HANDLE of the named mutex
    int depth = 0;
};
#else
struct HeldLock {
    int fd = -1;
    int depth = 0;
};
#endif
std::mutex g_heldLocksMutex;
std::map<std::string, HeldLock>& HeldLocks()
{
    static std::map<std::string, HeldLock> locks;
    return locks;
}

#ifndef _WIN32
// The lock file for a mutex name under the temp directory (path-unsafe
// characters sanitized). Windows uses a kernel named mutex and needs no
// file.
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
#endif

}  // namespace

#ifdef _WIN32

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
    const std::u16string wideName = ::ILSpy::Decompiler::Util::Utf8ToUtf16(name_);
    // A named kernel mutex is the direct C# System.Threading.Mutex
    // analogue: cross-process, with the abandoned-owner state the C#
    // AbandonedMutexException catch anticipates.
    held.handle = ::CreateMutexW(nullptr, FALSE,
        reinterpret_cast<const wchar_t*>(wideName.c_str()));
    if (held.handle == nullptr) {
        // The C# Mutex ctor throws for an uncreatable name; that only
        // happens when the kernel object namespace itself is broken.
        throw std::runtime_error("Cannot create the named mutex '"
            + name_ + "'.");
    }
    // Blocks until held (the C# WaitOne on a mutex another process
    // owns). WAIT_ABANDONED still grants ownership: the previous
    // holder died, which the C# MutexProtector catches and proceeds
    // past (a settings sidecar is safe to rewrite).
    DWORD wait = ::WaitForSingleObject(held.handle, INFINITE);
    (void)wait;
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
    ::ReleaseMutex(it->second.handle);
    ::CloseHandle(it->second.handle);
    it->second.handle = nullptr;
}

#else  // !_WIN32

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

#endif  // _WIN32

}  // namespace ILSpy::ILSpyX::Settings
