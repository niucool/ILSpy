// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// BusyManager.cpp -- see BusyManager.hpp for the port contract (the
// BusyLock move/pop semantics live here; the thread-static list is the
// function-local thread_local vector, convention (b)).

#include "Decompiler/Util/BusyManager.hpp"

#include <utility>

namespace ILSpy::Decompiler::Util {

namespace {

// The C# `[ThreadStatic] static List<object?>? _activeObjects` -- lazily
// created on first use, per thread (convention (b)).
std::vector<const void*>& ActiveObjects() {
    thread_local std::vector<const void*> objects;
    return objects;
}

} // namespace

BusyLock::BusyLock(BusyLock&& other) noexcept
    : objects_(std::exchange(other.objects_, nullptr)) {}

BusyLock& BusyLock::operator=(BusyLock&& other) noexcept {
    if (this != &other) {
        // The C# Dispose-before-reassign shape (a moved-into lock pops its
        // own held list first -- the `using` pattern never reassigns, so this
        // is the defensive RAII equivalent).
        if (objects_ != nullptr)
            objects_->pop_back();
        objects_ = std::exchange(other.objects_, nullptr);
    }
    return *this;
}

BusyLock::~BusyLock() {
    // The C# `Dispose`: `if (objectList != null) objectList.RemoveAt(
    // objectList.Count - 1);` -- the unconditional LIFO pop (convention (d)).
    if (objects_ != nullptr)
        objects_->pop_back();
}

BusyLock BusyManager::Enter(const void* obj) {
    // The C# scan: `if (activeObjects[i] == obj) return BusyLock.Failed;`
    // (the identity comparison -- `nullptr` equals `nullptr`, the null-key
    // gold pair).
    std::vector<const void*>& objects = ActiveObjects();
    for (const void* key : objects) {
        if (key == obj)
            return BusyLock();
    }
    objects.push_back(obj);
    return BusyLock(&objects);
}

} // namespace ILSpy::Decompiler::Util
