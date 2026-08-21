// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
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

// Port of ICSharpCode.Decompiler/Semantics/ErrorResolveResult.cs -- the
// `ResolveResult` (D424) that represents a resolve error. This is the twelfth
// `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the
// long-pole remaining blocker). The C# source declares:
//   * `public static readonly ErrorResolveResult UnknownError` -- a singleton
//     instance with `Type` = `SpecialType.UnknownType`.
//   * `ErrorResolveResult(IType type) : base(type)` -- forwards the error type.
//   * `ErrorResolveResult(IType type, string message, TextLocation location) :
//     base(type)` -- forwards the type and stores a diagnostic message + source
//     location.
//   * `override bool IsError => true` -- the load-bearing crux: an error result
//     is always an error regardless of the stored type.
//   * `string Message { get; private set; }` / `TextLocation Location { get;
//     private set; }` -- the diagnostic message and source location (default
//     `null` / `TextLocation.Empty` for the 1-arg ctor).
//
// KEY PORT CONVENTIONS:
//  * The C# `static readonly ErrorResolveResult UnknownError` (a GC-owned
//    singleton field) ports to a static accessor function returning `const
//    ErrorResolveResult&` backed by a function-local static (the Meyers-singleton
//    pattern, the D398 `StringComparer::Ordinal` / D419
//    `DefaultAssemblyReference::CurrentAssembly` precedent). The singleton is
//    constructed with the D417 `UnknownType()` convenience (the C#
//    `SpecialType.UnknownType` null object).
//  * The C# `string Message` (a `get; private set;` property defaulting to
//    `null`) ports to a `std::string message_` member defaulting to the empty
//    string (the established `std::string`-for-nullable-`string` convention; the
//    1-arg ctor leaves it empty). The accessor returns `const std::string&` (the
//    established convention for string accessors).
//  * The C# `TextLocation Location` (a `get; private set;` property of the
//    value-type `TextLocation` struct defaulting to `TextLocation.Empty`) ports
//    to a `TextLocation location_` member default-constructed (the C++
//    `TextLocation()` default ctor yields `Empty` = (0, 0), faithful to the C#
//    `default(TextLocation)`). The accessor returns by value (the
//    trivially-copyable 8-byte struct is returned directly, faithful to the C#
//    property which returns the struct by value).
//  * The C# `public class ErrorResolveResult` (NOT `sealed` -- the C# class is
//    unsealed) ports to a C++ subclass (NOT `final`) of `ResolveResult`. The C++
//    port additionally overrides `ClassName()` (the D424 polymorphic-class-name
//    convention) and `ShallowClone()` (the D424 runtime-type-preservation
//    convention), both of which every `ResolveResult` subclass port carries.
//  * The `IsError` override returning `true` unconditionally is the load-bearing
//    crux distinguishing an `ErrorResolveResult` from every other
//    `ResolveResult` (the base default is `false`; `TypeResolveResult` D425
//    returns `true` only for `TypeKind::Unknown`; `SizeOfResolveResult` D430
//    returns `true` only for reference types -- `ErrorResolveResult` is the
//    unconditional error result the resolver constructs when resolution fails).

#ifndef ILSPY_DECOMPILER_SEMANTICS_ERRORRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_ERRORRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ErrorResolveResult : ResolveResult` (NOT `sealed`) ports
// to a C++ subclass (NOT `final`) of `ResolveResult`. The C# surface is the two
// forwarding ctors, the `IsError => true` override, and the `Message` / `Location`
// properties; the C++ port additionally overrides `ClassName()` and
// `ShallowClone()` to faithfully reproduce the C# `GetType().Name` (polymorphic
// class name in the inherited `ToString`) and `MemberwiseClone`
// (runtime-type-preserving shallow clone) -- the documented `ResolveResult`
// conventions every subclass port carries.
class ErrorResolveResult : public ResolveResult {
public:
    // The C# `ErrorResolveResult(IType type) : base(type)` -- forwards the error
    // type to the `ResolveResult` base ctor (which `assert`-guards the non-null
    // `IType` per the D424 `ArgumentNullException`-to-`assert` convention). The
    // `Message` and `Location` stay at their defaults (empty string / `Empty`).
    explicit ErrorResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : ResolveResult(std::move(type)) {}

    // The C# `ErrorResolveResult(IType type, string message, TextLocation
    // location) : base(type)` -- forwards the type to the base and stores the
    // diagnostic `message` and source `location`. The C# does NOT guard `message`
    // or `location` with `ArgumentNullException` (both may be `null` /
    // `default`), so the C++ port does NOT `assert` on them -- the faithful
    // null-handling distinct from the `IType`-guarding `assert`.
    ErrorResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type,
                       std::string message,
                       ILSpy::Decompiler::CSharp::Syntax::TextLocation location)
        : ResolveResult(std::move(type)),
          message_(std::move(message)),
          location_(location) {}

    // The C# `public static readonly ErrorResolveResult UnknownError` (a
    // GC-owned singleton with `Type` = `SpecialType.UnknownType`) ports to a
    // static accessor returning `const ErrorResolveResult&` backed by a
    // function-local static (the Meyers-singleton pattern). The singleton is
    // constructed with the D417 `UnknownType()` convenience (the C#
    // `SpecialType.UnknownType` null object, `TypeKind::Unknown`).
    static const ErrorResolveResult& UnknownError() {
        static const ErrorResolveResult instance(
            ILSpy::Decompiler::TypeSystem::UnknownType());
        return instance;
    }

    // The C# `override bool IsError => true` -- the load-bearing crux: an error
    // result is always an error, unconditionally (regardless of the stored type).
    // This is the virtual-dispatch override the resolver relies on to detect a
    // failed resolution.
    bool IsError() const override { return true; }

    // The C# `string Message { get; private set; }` -- the diagnostic message
    // (defaults to `null` for the 1-arg ctor, faithful to the C# default which
    // the `std::string` empty value represents). Returns `const std::string&`
    // (the established convention for string accessors).
    const std::string& Message() const { return message_; }

    // The C# `TextLocation Location { get; private set; }` -- the source location
    // (defaults to `TextLocation.Empty` = (0, 0) for the 1-arg ctor, faithful to
    // the C# `default(TextLocation)` which the C++ `TextLocation()` default ctor
    // reproduces). Returns by value (the trivially-copyable 8-byte struct is
    // returned directly, faithful to the C# property which returns the struct by
    // value).
    ILSpy::Decompiler::CSharp::Syntax::TextLocation Location() const { return location_; }

protected:
    // The C# `ToString` (inherited from `ResolveResult`) uses `GetType().Name`
    // which is polymorphic and yields "ErrorResolveResult". The C++ port
    // reproduces this via the `ClassName()` override so the inherited `ToString`
    // reports the subclass name (not the base "ResolveResult").
    std::string ClassName() const override { return "ErrorResolveResult"; }

public:
    // The C# `ShallowClone` (inherited from `ResolveResult`) uses
    // `MemberwiseClone` which preserves the runtime type, so a cloned
    // `ErrorResolveResult` stays an `ErrorResolveResult` (not sliced to the
    // `ResolveResult` base). The C++ port reproduces this by overriding
    // `ShallowClone` to construct an `ErrorResolveResult` copy (the default copy
    // ctor shares the `type_` `shared_ptr` and copies the `message_` string and
    // `location_` value struct, faithful to the C# `MemberwiseClone` which
    // reference-copies the `IType` field and value-copies the `string` and
    // `TextLocation` fields).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ErrorResolveResult>(*this);
    }

private:
    std::string message_;
    ILSpy::Decompiler::CSharp::Syntax::TextLocation location_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_ERRORRESOLVERESULT_HPP
