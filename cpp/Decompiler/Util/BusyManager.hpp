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

// Port of `ICSharpCode.Decompiler.Util.BusyManager` (Util/BusyManager.cs) --
// the reentrance guard the recursive type-system resolutions use to prevent
// stack overflows: "representing a 'busy' flag that prevents reentrance when
// another call is running. However, using a simple 'bool busy' is not
// thread-safe, so we use a thread-static BusyManager."
//
// The C# is a static class holding a [ThreadStatic] List<object?> of the
// objects whose operations are currently running: `Enter(obj)` scans the list
// for `obj` and returns `BusyLock.Failed` (Success == false, a null list) when
// found, else appends `obj` and returns a lock whose `Dispose` removes the
// LAST list entry (the LIFO pop the C# `using` pattern produces). The C#
// consumers are `MetadataModule.ResolveForwardedType` (the cyclic-forwarder
// resolution: a module forwarding a type to a module that forwards it back
// must terminate in an UnknownType instead of recursing forever),
// `MetadataTypeDefinition` (the lazy member family) and
// `AbstractTypeParameter` (the constraint walk) -- this slice's consumer is
// the first.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `BusyLock` struct (a readonly `List<object?>?` reference; the
//      null list is the `Failed` lock) ports to a move-only RAII class over a
//      `std::vector<const void*>*`: the default construction is the Failed
//      lock (Success == false, a null list pointer), and a lock holding a list
//      pops the list's last entry in its destructor (the C# `Dispose`). The
//      C# struct is copyable and `Dispose` is idempotent (a null-list Dispose
//      does nothing); the C++ port models the `using` pattern with move-only
//      semantics -- the single lock value lives to the end of its scope, and
//      the moved-from lock transfers the pop obligation (the RAII equivalent
//      of the C# copy-and-Dispose pattern, which no consumer exercises).
//  (b) The C# [ThreadStatic] list ports to a `thread_local` vector inside the
//      .cpp (function-local static, initialized on first use -- the C#
//      `_activeObjects` lazy null-to-new assignment).
//  (c) The C# `object?` key ports to `const void*` (identity comparison --
//      the C# `==` on object references; `Enter(nullptr)` is legal and the
//      null key compares equal to itself, the gold-pinned A4/A5 pair).
//  (d) The pop is `RemoveAt(Count - 1)` -- NOT the removal of the lock's own
//      key: out-of-order disposal pops the LAST entry regardless (the
//      faithful C# quirk; the `using` pattern always disposes LIFO, so the
//      quirk is unreachable through the ported consumers).

#pragma once

#include <vector>

namespace ILSpy::Decompiler::Util {

// The C# `BusyManager.BusyLock` (see the header comment).
class BusyLock {
public:
    // The C# `BusyLock.Failed` (the null-list lock).
    BusyLock() = default;
    BusyLock(BusyLock&& other) noexcept;
    BusyLock& operator=(BusyLock&& other) noexcept;
    BusyLock(const BusyLock&) = delete;
    BusyLock& operator=(const BusyLock&) = delete;
    // The C# `Dispose` (the null-list no-op included): pops the list's last
    // entry. The pop is unconditional on the HELD list, mirroring the C#
    // `objectList.RemoveAt(objectList.Count - 1)` (the LIFO-pop quirk,
    // convention (d)).
    ~BusyLock();

    // The C# `bool Success => objectList != null`.
    bool Success() const { return objects_ != nullptr; }

private:
    friend class BusyManager;
    explicit BusyLock(std::vector<const void*>* objects)
        : objects_(objects) {}

    std::vector<const void*>* objects_ = nullptr;
};

// The C# `public static class BusyManager`.
class BusyManager {
public:
    // The C# `public static BusyLock Enter(object? obj)`: the scan for an
    // equal key (Failed when found), else the append and the new lock.
    static BusyLock Enter(const void* obj);
};

} // namespace ILSpy::Decompiler::Util
