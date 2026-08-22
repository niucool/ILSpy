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

// Tests for the CSharp/Resolver Log leaf (cpp/Decompiler/CSharp/Resolver/Log.hpp,
// porting ICSharpCode.Decompiler/CSharp/Resolver/Log.cs) -- the resolver's opt-in debug
// logging helper wrapping System.Diagnostics.Debug. The leaf carries two test surfaces:
//   * the committed-off configuration (Log::IsEnabled == false, the C#
//     const bool logEnabled = false) must emit NOTHING: every WriteLine /
//     WriteCollection / Indent / Unindent call is a no-op discarded at compile time,
//     pinned by capturing std::cerr around the calls (a body that ever ran
//     unconditionally would land in the buffer);
//   * the LogDetail formatting helpers (the indexed-placeholder replacement, the
//     ToString-preferring ToLogString dispatch, and the WriteCollection line layout)
//     ARE instantiated and directly verified, so flipping IsEnabled exercises an
//     already-tested code path.

#include "Decompiler/CSharp/Resolver/Log.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

// Redirects std::cerr into an in-memory buffer for the lifetime of the guard, so a
// test can prove the disabled Log emitted NOTHING (any body that ran unconditionally
// would have written into the buffer).
class CerrCapture {
public:
    CerrCapture()
        : previous_(std::cerr.rdbuf(buffer_.rdbuf())) {}

    ~CerrCapture()
    {
        std::cerr.rdbuf(previous_);
    }

    std::string Captured() const
    {
        return buffer_.str();
    }

private:
    std::ostringstream buffer_;
    std::streambuf* previous_;
};

// A minimal ToString()-carrying value type, the shape the ported Semantics resolve
// results expose (their ToString() is the object.ToString() stand-in). Deliberately
// NOT streamable (no operator<<): a ToLogString dispatch that fell through to the
// streaming branch for this type would fail to compile, so the ToString-first dispatch
// is pinned statically as well as at runtime.
struct NamedValue {
    std::string name;

    std::string ToString() const
    {
        return "NV:" + name;
    }
};

} // namespace

// ===========================================================================
// The class shape: the C# `static class Log` cannot be instantiated, derived, or
// carry instance state; the committed IsEnabled == false is a compile-time constant.
// ===========================================================================

TEST(LogTest, IsEnabledIsACompileTimeConstantFalse)
{
    // The C# `const bool logEnabled = false` -- the committed-off switch; flipping it
    // is a local debugging session's deliberate rebuild.
    static_assert(!Res::Log::IsEnabled, "Log::IsEnabled ports the C# const bool logEnabled = false");
    EXPECT_FALSE(Res::Log::IsEnabled);
}

TEST(LogTest, ClassMatchesTheCSharpStaticClassShape)
{
    // The C# `static class` cannot be instantiated or derived and carries no instance
    // state; the port deletes both constructors and is final, with only static members.
    static_assert(std::is_final<Res::Log>::value,
                  "the C# static class cannot be derived from -- the port is final");
    static_assert(!std::is_default_constructible<Res::Log>::value,
                  "the C# static class cannot be instantiated -- the port deletes the default ctor");
    static_assert(!std::is_copy_constructible<Res::Log>::value,
                  "the C# static class cannot be copied -- the port deletes the copy ctor");
    static_assert(std::is_empty<Res::Log>::value,
                  "the C# static class carries no instance state -- the port has only static members");
    SUCCEED();
}

// ===========================================================================
// The disabled configuration: every member is a discarded `if constexpr` branch, so a
// whole resolver-style logging trail writes nothing to the std::cerr sink.
// ===========================================================================

TEST(LogTest, WriteLineTextEmitsNothingWhenDisabled)
{
    CerrCapture capture;
    Res::Log::WriteLine("Type Inference");
    Res::Log::WriteLine(std::string("  Signature: M<TP>"));
    EXPECT_TRUE(capture.Captured().empty());
}

TEST(LogTest, WriteLineFormatEmitsNothingWhenDisabled)
{
    CerrCapture capture;
    Res::Log::WriteLine("No candidate is applicable, trying {0} extension method groups...", 3);
    Res::Log::WriteLine("Overload resolution finished, best candidate is {0}.", std::string("M(int)"));
    auto best = std::make_shared<NamedValue>(NamedValue{ "best" });
    Res::Log::WriteLine("  best: {0}", best);
    EXPECT_TRUE(capture.Captured().empty());
}

TEST(LogTest, WriteCollectionEmitsNothingWhenDisabled)
{
    CerrCapture capture;
    const std::vector<int> ints = { 1, 2, 3 };
    Res::Log::WriteCollection("  Arguments: ", ints);
    Res::Log::WriteCollection("  Arguments: ", std::vector<std::string>{ "a", "b" });
    const std::vector<std::shared_ptr<NamedValue>> results = {
        std::make_shared<NamedValue>(NamedValue{ "x" }), nullptr
    };
    Res::Log::WriteCollection("  Arguments: ", results);
    EXPECT_TRUE(capture.Captured().empty());
}

TEST(LogTest, WriteCollectionOfAnEmptyRangeEmitsNothingWhenDisabled)
{
    CerrCapture capture;
    Res::Log::WriteCollection("  Arguments: ", std::vector<int>{});
    EXPECT_TRUE(capture.Captured().empty());
}

TEST(LogTest, IndentUnindentLeaveNoOutputWhenDisabled)
{
    CerrCapture capture;
    Res::Log::Indent();
    Res::Log::WriteLine("indented one level");
    Res::Log::Unindent();
    Res::Log::WriteLine("back at level zero");
    EXPECT_TRUE(capture.Captured().empty());
}

TEST(LogTest, UnindentBelowZeroIsSafeWhenDisabled)
{
    // An unbalanced Unindent must not corrupt the hidden indent level the enabled
    // configuration would render (the .NET Trace.IndentLevel setter floors at 0); with
    // IsEnabled == false all of these are no-ops, pinned by the still-silent sink.
    CerrCapture capture;
    Res::Log::Unindent();
    Res::Log::Unindent();
    Res::Log::WriteLine("still at level zero");
    EXPECT_TRUE(capture.Captured().empty());
}

TEST(LogTest, ACombinedLoggingTrailEmitsNothingWhenDisabled)
{
    // The shape of the logger trails the still-unported OverloadResolution /
    // TypeInference callers produce (Text + formatted + collection + indent churn).
    CerrCapture capture;
    Res::Log::WriteLine("Type Inference");
    Res::Log::WriteLine("  Signature: M<{0}>", "TP");
    const std::vector<std::string> args = { "int", "string" };
    Res::Log::WriteCollection("  Arguments: ", args);
    Res::Log::Indent();
    Res::Log::WriteLine("  Exact inference for {0}", 0);
    Res::Log::Unindent();
    Res::Log::WriteLine("Type inference finished.");
    EXPECT_TRUE(capture.Captured().empty());
}

// ===========================================================================
// The LogDetail helpers: unconditionally compiled string.Format / object.ToString /
// WriteCollection-layout stand-ins, so the enabled code path is tested even though
// IsEnabled == false never reaches it through Log itself.
// ===========================================================================

TEST(LogDetailTest, HasToStringDetectsTheToStringMemberShape)
{
    static_assert(Res::LogDetail::HasToString<NamedValue>::value,
                  "a const ToString() returning std::string is detected");
    static_assert(!Res::LogDetail::HasToString<int>::value,
                  "arithmetic types have no ToString member -- they stream");
    static_assert(!Res::LogDetail::HasToString<std::string>::value,
                  "std::string has no ToString member -- it streams");
    SUCCEED();
}

TEST(LogDetailTest, ToLogStringPrefersTheToStringMember)
{
    const NamedValue value{ "x" };
    // NamedValue is not streamable: this call only compiles because the ToString-first
    // dispatch selected the member.
    EXPECT_EQ(Res::LogDetail::ToLogString(value), "NV:x");
}

TEST(LogDetailTest, ToLogStringStreamsArithmeticAndStringTypes)
{
    EXPECT_EQ(Res::LogDetail::ToLogString(42), "42");
    EXPECT_EQ(Res::LogDetail::ToLogString(-3), "-3");
    EXPECT_EQ(Res::LogDetail::ToLogString(std::string("text")), "text");
    EXPECT_EQ(Res::LogDetail::ToLogString("literal"), "literal");
}

TEST(LogDetailTest, ToLogStringRendersTheSharedPointee)
{
    const auto value = std::make_shared<NamedValue>(NamedValue{ "best" });
    EXPECT_EQ(Res::LogDetail::ToLogString(value), "NV:best");
}

TEST(LogDetailTest, ToLogStringRendersANullSharedHandleAsTheNullMarker)
{
    // The C# WriteCollection null-element marker (`arr[i] != null ? .ToString() :
    // "<null>"`), applied uniformly to shared handles (see the header note).
    const std::shared_ptr<NamedValue> nullValue;
    EXPECT_EQ(Res::LogDetail::ToLogString(nullValue), "<null>");
    const std::shared_ptr<int> nullInt;
    EXPECT_EQ(Res::LogDetail::ToLogString(nullInt), "<null>");
}

TEST(LogDetailTest, FormatWithoutPlaceholdersReturnsTheText)
{
    // Extra arguments without placeholders are ignored, matching string.Format.
    EXPECT_EQ(Res::LogDetail::Format("plain", {}), "plain");
    EXPECT_EQ(Res::LogDetail::Format("plain", { "ignored" }), "plain");
}

TEST(LogDetailTest, FormatReplacesIndexedPlaceholders)
{
    EXPECT_EQ(Res::LogDetail::Format("{0} of {1}", { "2", "5" }), "2 of 5");
    EXPECT_EQ(Res::LogDetail::Format("{2}-{1}-{0}", { "a", "b", "c" }), "c-b-a");
}

TEST(LogDetailTest, FormatReplacesEveryOccurrenceOfAPlaceholder)
{
    EXPECT_EQ(Res::LogDetail::Format("{0} then {0}", { "x" }), "x then x");
}

TEST(LogDetailTest, FormatLeavesAnUnmatchedPlaceholderAlone)
{
    // string.Format would throw a FormatException; resolver debug logging must never
    // throw, so the port leaves the placeholder unreplaced (preserving the text).
    EXPECT_EQ(Res::LogDetail::Format("{0} {9}", { "x" }), "x {9}");
}

TEST(LogDetailTest, FormatWithNoArgumentsLeavesAllPlaceholdersAlone)
{
    EXPECT_EQ(Res::LogDetail::Format("{0}", {}), "{0}");
}

TEST(LogDetailTest, FormatCollectionLinesMarksAnEmptyRange)
{
    // The C# `arr.Length == 0` arm: the text plus the "<empty collection>" marker.
    const auto lines = Res::LogDetail::FormatCollectionLines("  Arguments: ", std::vector<int>{});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "  Arguments: <empty collection>");
}

TEST(LogDetailTest, FormatCollectionLinesPutsTheFirstElementOnTheTextLineAndPadsContinuations)
{
    // The C# continuation-line indent: new string(' ', text.Length) spaces.
    const std::vector<std::string> items = { "first", "second", "third" };
    const auto lines = Res::LogDetail::FormatCollectionLines("Args: ", items);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "Args: first");
    EXPECT_EQ(lines[1], "      second");
    EXPECT_EQ(lines[2], "      third");
}

TEST(LogDetailTest, FormatCollectionLinesRendersNullElementsAsTheNullMarker)
{
    const std::vector<std::shared_ptr<NamedValue>> items = {
        std::make_shared<NamedValue>(NamedValue{ "x" }), nullptr
    };
    const auto lines = Res::LogDetail::FormatCollectionLines("A:", items);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "A:NV:x");
    EXPECT_EQ(lines[1], "  <null>");
}
