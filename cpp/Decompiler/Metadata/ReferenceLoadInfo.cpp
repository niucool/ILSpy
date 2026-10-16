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

// The implementation of ReferenceLoadInfo.hpp (the
// UnresolvedAssemblyNameReference / ReferenceLoadInfo port of
// ICSharpCode.Decompiler/Metadata/ReferenceLoadInfo.cs and
// ICSharpCode.Decompiler/Metadata/UnresolvedAssemblyNameReference.cs).

#include "Decompiler/Metadata/ReferenceLoadInfo.hpp"

namespace ILSpy::Decompiler::Metadata {

namespace {

// The shared get-or-create behind AddMessage/AddMessageOnce (the C#'s two
// inlined TryGetValue blocks).
UnresolvedAssemblyNameReference& GetOrCreate(
    std::vector<std::shared_ptr<UnresolvedAssemblyNameReference>>& entries,
    const std::string& fullName) {
    for (auto& entry : entries) {
        if (entry->FullName() == fullName) return *entry;
    }
    entries.push_back(std::make_shared<UnresolvedAssemblyNameReference>(fullName));
    return *entries.back();
}

}  // namespace

bool UnresolvedAssemblyNameReference::HasErrors() const {
    for (const auto& message : messages_) {
        if (message.first == MessageKind::Error) return true;
    }
    return false;
}

void ReferenceLoadInfo::AddMessage(const std::string& fullName, MessageKind kind,
    const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    GetOrCreate(entries_, fullName).Messages().emplace_back(kind, message);
}

void ReferenceLoadInfo::AddMessageOnce(const std::string& fullName, MessageKind kind,
    const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    UnresolvedAssemblyNameReference* existing = nullptr;
    for (auto& entry : entries_) {
        if (entry->FullName() == fullName) {
            existing = entry.get();
            break;
        }
    }
    if (existing == nullptr) {
        entries_.push_back(
            std::make_shared<UnresolvedAssemblyNameReference>(fullName));
        entries_.back()->Messages().emplace_back(kind, message);
        return;
    }
    // The C# `lastMsg = referenceInfo.Messages.LastOrDefault()` over the
    // (MessageKind, string) tuple: the empty-list default is
    // (default(MessageKind) = Error, null) -- every entry created by these
    // two methods carries at least one message, so the default arm is
    // unreachable; the port models it with the same (Error, "") shape.
    const std::pair<MessageKind, std::string>& last =
        existing->Messages().empty()
        ? std::pair<MessageKind, std::string>(MessageKind::Error, "")
        : existing->Messages().back();
    if (kind != last.first && message != last.second) {
        existing->Messages().emplace_back(kind, message);
    }
}

const UnresolvedAssemblyNameReference* ReferenceLoadInfo::TryGetInfo(
    const std::string& fullName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : entries_) {
        if (entry->FullName() == fullName) return entry.get();
    }
    return nullptr;
}

std::vector<const UnresolvedAssemblyNameReference*> ReferenceLoadInfo::Entries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const UnresolvedAssemblyNameReference*> result;
    result.reserve(entries_.size());
    for (auto& entry : entries_) result.push_back(entry.get());
    return result;
}

bool ReferenceLoadInfo::HasErrors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : entries_) {
        if (entry->HasErrors()) return true;
    }
    return false;
}

}  // namespace ILSpy::Decompiler::Metadata
