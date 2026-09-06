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

// Port of ICSharpCode.Decompiler/Metadata/UnresolvedAssemblyNameReference.cs
// and ICSharpCode.Decompiler/Metadata/ReferenceLoadInfo.cs: the
// resolution-diagnostics bookkeeping `DotNetCorePathFinder`'s missing-deps
// warning and `UniversalAssemblyResolver`'s load-info surface feed (the GUI
// renders the entries; the CLI constructs but never reads it).

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// The C# `public enum MessageKind`.
enum class MessageKind {
    Error,
    Warning,
    Info,
};

// The C# `public sealed class UnresolvedAssemblyNameReference` -- one
// reference's accumulated messages. The port models the
// `List<(MessageKind, string)>` as a vector of pairs and exposes the raw
// list's mutation surface (the only mutator is ReferenceLoadInfo's own
// AddMessage pair appending to it).
class UnresolvedAssemblyNameReference {
public:
    explicit UnresolvedAssemblyNameReference(std::string fullName)
        : fullName_(std::move(fullName)) {}

    // The C# `string FullName { get; }`.
    const std::string& FullName() const { return fullName_; }

    // The C# `bool HasErrors` -- any message carries the Error kind.
    bool HasErrors() const;

    // The C# `List<(MessageKind, string)> Messages { get; }`.
    const std::vector<std::pair<MessageKind, std::string>>& Messages() const {
        return messages_;
    }
    std::vector<std::pair<MessageKind, std::string>>& Messages() { return messages_; }

private:
    std::string fullName_;
    std::vector<std::pair<MessageKind, std::string>> messages_;
};

// The C# `public class ReferenceLoadInfo` -- the per-full-name dictionary of
// unresolved references with its lock (the port is single-threaded, but the
// mutex keeps the locked-shape contract). The C# Dictionary's
// insertion-order enumeration (Entries) ports as an ordered vector.
class ReferenceLoadInfo {
public:
    // The C# `AddMessage(fullName, kind, message)`: creates the entry on
    // first sight and appends unconditionally.
    void AddMessage(const std::string& fullName, MessageKind kind,
        const std::string& message);

    // The C# `AddMessageOnce(fullName, kind, message)`: creates the entry on
    // first sight (appending); on an existing entry appends only when BOTH
    // the kind and the text differ from the LAST message (the C# `&&`).
    void AddMessageOnce(const std::string& fullName, MessageKind kind,
        const std::string& message);

    // The C# `bool TryGetInfo(string fullName, out info)` -- the port returns
    // null for the not-found arm.
    const UnresolvedAssemblyNameReference* TryGetInfo(const std::string& fullName) const;

    // The C# `IReadOnlyList<UnresolvedAssemblyNameReference> Entries` -- the
    // insertion-order snapshot the locked Values.ToList() produces.
    std::vector<const UnresolvedAssemblyNameReference*> Entries() const;

    // The C# `bool HasErrors` -- any entry has errors.
    bool HasErrors() const;

private:
    // The lock the C# takes over the dictionary (kept for the contract).
    mutable std::mutex mutex_;
    // The dictionary's insertion-ordered storage.
    std::vector<std::shared_ptr<UnresolvedAssemblyNameReference>> entries_;
};

}  // namespace ILSpy::Decompiler::Metadata
