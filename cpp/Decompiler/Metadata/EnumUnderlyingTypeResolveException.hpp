// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Metadata/EnumUnderlyingTypeResolveException.cs
// -- the `public class EnumUnderlyingTypeResolveException : Exception` the
// TypeProvider's `GetUnderlyingEnumType` throws when a custom-attribute blob's
// enum type has no resolvable underlying-type definition (the
// `ICustomAttributeTypeProvider` contract's EnumUnderlyingType arm). The C#
// carries a PARAMETERLESS ctor with no base message (the gold: the thrown
// Message is the .NET default-form "Exception of type
// 'ICSharpCode.Decompiler.Metadata.EnumUnderlyingTypeResolveException' was
// thrown."), plus string and (message, inner) ctors used elsewhere; the port
// models the parameterless arm (the only ctor the TypeProvider calls) as the
// std::runtime_error carrying the same default-form message. The
// ReflectionDisassembler's `catch (Exception ex) when (ex is
// BadImageFormatException || ex is EnumUnderlyingTypeResolveException)` shows
// the catch-site contract: a plain runtime-error family member, caught
// alongside the metadata-read throws.
//
// The same C# file carries the sibling `public class
// MetadataFileNotSupportedException : Exception` -- the exception the
// MetadataFile ctor over a PEReader throws for a valid PE image with no CLI
// directory ("PE file does not contain any managed metadata."). It is the
// one failure shape `UniversalAssemblyResolver.CreatePEFileFromFileName`
// does NOT catch: it escapes the resolver's two catch arms (only
// BadImageFormatException and IOException are handled) and propagates out of
// Resolve/ResolveModule regardless of throwOnError -- gold-pinned against
// the real engine over a native-PE module fixture.
// ReflectionDisassembler's `catch (Exception ex) when (ex is
// BadImageFormatException || ex is EnumUnderlyingTypeResolveException)` shows
// the catch-site contract: a plain runtime-error family member, caught
// alongside the metadata-read throws.

#pragma once

#include <stdexcept>
#include <string>

namespace ILSpy::Decompiler::Metadata {

class EnumUnderlyingTypeResolveException : public std::runtime_error {
public:
    // The C# parameterless ctor: NO base message -- the .NET default-form
    // Message (gold-pinned against the real engine).
    EnumUnderlyingTypeResolveException()
        : std::runtime_error("Exception of type 'ICSharpCode.Decompiler."
                            "Metadata.EnumUnderlyingTypeResolveException' "
                            "was thrown.") {}
};

// The sibling `public class MetadataFileNotSupportedException : Exception`
// of the same C# file -- the MetadataFile-over-PEReader ctor's no-CLI-directory
// rejection. `CreatePEFileFromFileName` does not catch it, so it propagates
// out of Resolve/ResolveModule regardless of throwOnError.
class MetadataFileNotSupportedException : public std::runtime_error {
public:
    // The C# string ctor (the only ctor the resolver path reaches).
    explicit MetadataFileNotSupportedException(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace ILSpy::Decompiler::Metadata

