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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/Log.cs -- the resolver's opt-in debug
// logging helper ("Wraps System.Diagnostics.Debug so that resolver-specific logging can
// be enabled/disabled on demand. (it's a huge amount of debug spew and slows down the
// resolver quite a bit)"). This is the thirteenth `cpp/Decompiler/CSharp/Resolver/`
// leaf toward the `CSharpResolver` leaf deps (the long-pole remaining blocker of
// `TypeSystemAstBuilder` / `CSharpAmbience`), after the D467 twin Alias `ResolveResult`
// subclasses, the D468 twin enum leaves (`NameLookupMode` + `OverloadResolutionErrors`),
// the D469 `DynamicMemberResolveResult`, the D470 `AwaitResolveResult`, the D471
// `DynamicInvocationResolveResult` + `DynamicInvocationType`, the D472
// `CSharpInvocationResolveResult`, the D473 `LambdaResolveResult` + `LambdaConversion`,
// the D474 `MethodListWithDeclaringType` + `MethodGroupResolveResult`, and the D475
// `MemberLookup` accessibility surface. The C# source is an `internal static class`
// with a compile-time off-switch and five members:
//   * `const bool logEnabled = false;`
//   * `[Conditional(logEnabled ? "DEBUG" : "LOG_DISABLED")] internal static void
//     WriteLine(string text)`
//   * `[Conditional(...)] internal static void WriteLine(string format,
//     params object[] args)`
//   * `[Conditional(...)] internal static void WriteCollection<T>(string text,
//     IEnumerable<T> lines)`
//   * `[Conditional(...)] public static void Indent()` / `Unindent()`
//
// KEY PORT CONVENTIONS:
//  * The C# `[Conditional]` attribute removes each CALL (and its argument evaluation)
//    from every call site unless the attribute's symbol is defined; with
//    `logEnabled == false` the whole logging trail vanishes at compile time. C++ has no
//    [Conditional] analogue for functions, so the port keeps every call site compilable
//    and moves the elision INSIDE the methods: each body is guarded by
//    `if constexpr (IsEnabled)` with `IsEnabled == false`, so the enabled branch is a
//    discarded statement -- never instantiated, never codegen'd. DIVERGENCE
//    (documented): unlike the C# attribute, the ARGUMENTS are still evaluated at the
//    call site before the method body discards them. The only call sites live in the
//    still-unported `OverloadResolution` / `TypeInference` /
//    `MethodGroupResolveResult.PerformOverloadResolution` /
//    `CSharpInvocationResolveResult` logging trails; when those land, a hot-path call
//    site with expensive argument construction can be wrapped in
//    `if constexpr (Log::IsEnabled)` by the CALLER for full elision parity.
//  * The C# `static class` (cannot be instantiated, derived, or carry instance state)
//    ports to a `final` class with a deleted default and copy ctor and only static
//    members.
//  * The C# `Debug.WriteLine` sink ports to `std::cerr`: the port has no
//    System.Diagnostics.TraceListener infrastructure, and the sink is only reachable
//    when IsEnabled is flipped to true for a local debugging session. `Debug.Indent` /
//    `Debug.Unindent` port to a private indent level rendered as a prefix of
//    level * 4 spaces on every line (the .NET `Trace.IndentSize` default of 4, applied
//    per line by the listener's `WriteIndent`); `Unindent` clamps at 0 (the .NET
//    `Trace.IndentLevel` setter floors at 0, so `Unindent` may never drive it
//    negative).
//  * The two C# `WriteLine` overloads collapse into one variadic template taking a
//    `std::string_view` format: a zero-argument call IS the `WriteLine(string text)`
//    overload (the format pipeline with an empty argument set returns the text
//    unchanged). The `{N}` indexed replacements are performed by
//    `LogDetail::Format`, the `string.Format` stand-in. DIVERGENCE (documented):
//    `string.Format` THROWS a `FormatException` when a placeholder index names an
//    argument that does not exist; the port leaves such placeholders unreplaced
//    (resolver debug logging must never throw, and an untouched "{9}" preserves the
//    diagnostic text). Extra arguments without placeholders are ignored, matching the
//    C#.
//  * The C# `WriteCollection<T>` behavior (`<empty collection>` for an empty range,
//    first element on the text line, continuation lines padded with
//    `new string(' ', text.Length)` spaces, a null element rendered "<null>") is
//    factored into `LogDetail::FormatCollectionLines`, so the layout is unit-testable
//    while the `Log::WriteCollection` body itself compiles to nothing.
//  * `LogDetail::ToLogString` is the `object.ToString()` stand-in: a type with a const
//    `ToString()` member (the ported Semantics resolve results carry one) logs through
//    it; any other streamable type logs through `operator<<`; a null `shared_ptr`
//    logs "<null>" (the C# WriteCollection null-element marker, applied uniformly --
//    `string.Format` prints an EMPTY string for a null argument, but the marker is the
//    more useful debug output).

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_LOG_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_LOG_HPP

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// Implementation details of the resolver `Log` (the `string.Format` /
// `object.ToString()` / `WriteCollection`-layout stand-ins). None of this is part of
// the C# API surface; the helpers live outside the disabled `if constexpr` branches so
// the enabled code path stays compiled and unit-testable even while
// `Log::IsEnabled == false` never reaches it -- flipping the switch then exercises an
// already-tested path.
namespace LogDetail {

// Detects a const `ToString()` member returning something convertible to `std::string`
// (the shape the ported Semantics resolve results expose).
template <typename T, typename = void>
struct HasToString : std::false_type {};

template <typename T>
struct HasToString<T, std::void_t<decltype(std::string(std::declval<const T&>().ToString()))>>
    : std::true_type {};

// The `object.ToString()` stand-in for a VALUE argument: prefers a `ToString()` member,
// else streams through `operator<<`.
template <typename T>
std::string ToLogString(const T& value)
{
    if constexpr (HasToString<T>::value) {
        return value.ToString();
    } else {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }
}

// The `shared_ptr` overload (the resolve-result handle the resolver's logging trails
// pass around): a null handle logs "<null>", the C# WriteCollection null-element
// marker, applied uniformly (see the header note).
template <typename T>
std::string ToLogString(const std::shared_ptr<T>& value)
{
    return value ? ToLogString(*value) : std::string("<null>");
}

// The `string.Format` stand-in: replaces every "{N}" with the N-th argument. A
// placeholder whose index has no argument is left unreplaced (string.Format would
// throw; resolver debug logging must never throw). Extra arguments without
// placeholders are ignored, matching the C#.
inline std::string Format(std::string_view format, std::initializer_list<std::string> args)
{
    std::string result(format);
    std::size_t index = 0;
    for (const std::string& arg : args) {
        const std::string token = "{" + std::to_string(index++) + "}";
        std::size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.size(), arg);
        }
    }
    return result;
}

// The `WriteCollection` layout: a "<empty collection>" marker for an empty range, else
// the first element on the text line and each continuation line padded with
// `text.size()` spaces (the C# `new string(' ', text.Length)`).
template <typename Range>
std::vector<std::string> FormatCollectionLines(std::string_view text, const Range& lines)
{
    using std::begin;
    using std::end;
    std::vector<std::string> result;
    auto it = begin(lines);
    const auto last = end(lines);
    if (it == last) {
        result.emplace_back(std::string(text) + "<empty collection>");
        return result;
    }
    result.emplace_back(std::string(text) + ToLogString(*it));
    const std::string pad(text.size(), ' ');
    for (++it; it != last; ++it) {
        result.emplace_back(pad + ToLogString(*it));
    }
    return result;
}

} // namespace LogDetail

// The C# `internal static class Log` -- the resolver logging helper wrapping
// System.Diagnostics.Debug so resolver-specific logging can be enabled/disabled on
// demand. Ports to a final, non-instantiable all-static class; with
// `IsEnabled == false` every method body is a discarded `if constexpr` branch, so the
// calls compile to nothing.
class Log final {
public:
    // The C# `const bool logEnabled = false` -- the compile-time logging switch. Flip
    // to true (and rebuild) for resolver debug output on std::cerr.
    static constexpr bool IsEnabled = false;

    // The C# `WriteLine(string text)` and `WriteLine(string format,
    // params object[] args)` overloads, collapsed into one variadic: a zero-argument
    // call is the text overload (the format pipeline returns the text unchanged).
    template <typename... Args>
    static void WriteLine(std::string_view format, Args&&... args)
    {
        if constexpr (IsEnabled) {
            WriteLineCore(LogDetail::Format(
                format, { LogDetail::ToLogString(std::forward<Args>(args))... }));
        } else {
            // Reference the parameters so the discarded-build warning level stays quiet.
            (void)format;
            ((void)args, ...);
        }
    }

    // The C# `WriteCollection<T>(string text, IEnumerable<T> lines)` -- logs the
    // elements of a collection (arguments, candidates, ...) on one-prefixed lines.
    template <typename Range>
    static void WriteCollection(std::string_view text, const Range& lines)
    {
        if constexpr (IsEnabled) {
            for (const std::string& line : LogDetail::FormatCollectionLines(text, lines)) {
                WriteLineCore(line);
            }
        } else {
            (void)text;
            (void)lines;
        }
    }

    // The C# `Indent()` -- increases the indent level prepended to every line.
    static void Indent()
    {
        if constexpr (IsEnabled) {
            ++indent_level_;
        }
    }

    // The C# `Unindent()` -- decreases the indent level; clamps at 0 (the .NET
    // Trace.IndentLevel setter floors at 0).
    static void Unindent()
    {
        if constexpr (IsEnabled) {
            if (indent_level_ > 0) {
                --indent_level_;
            }
        }
    }

private:
    // The C# static class cannot be instantiated or copied.
    Log() = delete;
    Log(const Log&) = delete;

    // The `Debug.WriteLine` sink: one std::cerr line, prefixed with
    // `indent_level_ * 4` spaces (the .NET `Trace.IndentSize` default of 4, applied per
    // line by the listener's `WriteIndent`).
    static void WriteLineCore(std::string_view line)
    {
        std::cerr << std::string(static_cast<std::size_t>(indent_level_) * 4, ' ') << line << '\n';
    }

    // The `Debug.IndentLevel` stand-in. A plain static (not thread-local): the C#
    // `Debug.IndentLevel` is process-global itself, and the helper only runs when
    // IsEnabled is flipped for a local debugging session.
    inline static int indent_level_ = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_LOG_HPP
