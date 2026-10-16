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

// Port of the `Match` struct in
// ICSharpCode.Decompiler/CSharp/Syntax/PatternMatching/Match.cs. `Match` is the
// result of a pattern-matching operation: a container of (group-name, node)
// captures plus a Success flag. The C# struct holds a `List<KeyValuePair<string,
// INode?>>` reference that is null on the failure sentinel (default-constructed
// `Match`) and present on a `CreateNew()` match, so `Success` is `results != null`.
//
// That null-vs-present distinction is ported with a `shared_ptr<vector>`: a
// default-constructed `Match` holds a null shared_ptr (Success == false) and
// `CreateNew()` allocates the vector (Success == true). The C# passes `Match` by
// value through the recursion; the struct copy shares the underlying list (a
// reference type), so captures and checkpoints recorded deep in the recursion
// propagate back to the caller. A `shared_ptr` copy has exactly the same
// share-and-mutate semantics, so `Match` may be passed by value here too: every
// copy aliases the one capture vector.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {

// Forward declaration: `Match` stores `INode*` capture values but does not need
// the interface's definition (the `Get<T>` template is instantiated at the call
// site, where `INode` is complete).
class INode;

// The result of a pattern-matching operation. The failure sentinel is the
// default-constructed value (no capture vector); `CreateNew()` makes a success
// match. Copies share the capture vector, matching the C# struct-by-value
// sharing of the underlying `List`.
class Match {
    using Entry = std::pair<std::string, INode*>;
    using Results = std::vector<Entry>;
    std::shared_ptr<Results> results;

public:
    Match() = default;

    // The C# `static Match CreateNew()` -- allocates the capture vector so the
    // match is a success.
    static Match CreateNew() {
        Match m;
        m.results = std::make_shared<Results>();
        return m;
    }

    // The C# `bool Success => results != null`.
    bool Success() const {
        return static_cast<bool>(results);
    }

    // The C# `internal int CheckPoint()` -- the current capture count, used as a
    // restore point by the backtracking algorithm.
    int CheckPoint() const {
        return static_cast<int>(results->size());
    }

    // The C# `internal void RestoreCheckPoint(int)` -- truncates the captures back
    // to the checkpoint (RemoveRange(checkPoint, Count - checkPoint) == resize).
    void RestoreCheckPoint(int checkPoint) {
        results->resize(static_cast<std::size_t>(checkPoint));
    }

    // The C# `void Add(string?, INode?)` -- records a capture only when both a
    // group name and a node are present (a null group name means "do not capture").
    void Add(std::optional<std::string_view> groupName, INode* node) {
        if (groupName && node != nullptr)
            results->emplace_back(std::string(*groupName), node);
    }

    // The C# `internal void AddNull(string?)` -- records a null capture for the
    // group (a NamedNode whose inner pattern matched an absent candidate), so the
    // group is present in `Has`/`Get` with a null value.
    void AddNull(std::optional<std::string_view> groupName) {
        if (groupName)
            results->emplace_back(std::string(*groupName), nullptr);
    }

    // The C# `bool Has(string)` -- whether any capture was recorded for the group.
    bool Has(std::string_view groupName) const {
        if (!results)
            return false;
        for (const auto& entry : *results)
            if (entry.first == groupName)
                return true;
        return false;
    }

    // The C# `IEnumerable<INode?> Get(string)` -- every capture for the group, in
    // recording order (includes nulls recorded by `AddNull`).
    std::vector<INode*> Get(std::string_view groupName) const {
        std::vector<INode*> found;
        if (results) {
            for (const auto& entry : *results)
                if (entry.first == groupName)
                    found.push_back(entry.second);
        }
        return found;
    }

    // The C# `IEnumerable<T> Get<T>(string) where T : INode` -- every capture for
    // the group down-cast to `T`. The C# `(T)pair.Value!` is a checked cast (it
    // throws `InvalidCastException` on a wrong type); `dynamic_cast<T*>` is the
    // faithful checked-downcast, returning null for a wrong-typed or null capture.
    template<class T>
    std::vector<T*> Get(std::string_view groupName) const {
        std::vector<T*> found;
        if (results) {
            for (const auto& entry : *results)
                if (entry.first == groupName)
                    found.push_back(dynamic_cast<T*>(entry.second));
        }
        return found;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching
