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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Definitions of the four `TokenWriter` static factory functions declared in
// `OutputVisitor/TokenWriter.hpp` (the D316-deferred surface, now unblocked by the concrete
// `TextWriterTokenWriter` D319, `InsertRequiredSpacesDecorator` D320, and
// `InsertMissingTokensDecorator` D321 ports). The C# `ITokenWriter.cs` declares these as
// `public static TokenWriter Create(...)` / `CreateWriterThatSetsLocationsInAST(...)` /
// `InsertRequiredSpaces(...)` / `WrapInWriterThatSetsLocationsInAST(...)` on `TokenWriter`; the
// definitions live here (out-of-line) because they construct the concrete
// `TextWriterTokenWriter` / `InsertRequiredSpacesDecorator` / `InsertMissingTokensDecorator`
// types, whose headers include `TokenWriter.hpp` -- defining them inline in the interface header
// would pull the concrete headers into `TokenWriter.hpp` and form an include cycle (the
// `AstNode.cpp` D223 out-of-line precedent for header methods that dereference a forward-declared
// type whose full definition lives in an including header).
//
// Ownership model (the C#-to-C++ porting crux for the factories):
//  * The C# factories return the top of the freshly-composed stack and rely on the GC to own the
//    whole stack (the decorator holds a non-owning reference to its inner writer).
//  * The two `Create` factories (`Create`, `CreateWriterThatSetsLocationsInAST`) compose a
//    FRESH stack from a new `TextWriterTokenWriter`, so the C++ port makes the outermost
//    decorator OWN its inner writer (via the `DecoratingTokenWriter` owning-mode ctor) -- a
//    single `std::unique_ptr<TokenWriter>` handle to the top owns every layer, and destroying
//    it destroys the whole stack (the faithful equivalent of the GC collecting the stack).
//  * The two `Wrap` factories (`InsertRequiredSpaces`, `WrapInWriterThatSetsLocationsInAST`)
//    wrap a CALLER-OWNED writer, so the C++ port returns a handle that owns ONLY the new
//    decorator (the non-owning inner pointer is faithful -- the caller keeps the original writer
//    alive for the decorator's lifetime, exactly as in C#).

#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"  // TokenWriter (the declarations being defined)

#include <memory>
#include <ostream>
#include <stdexcept>  // std::logic_error (InvalidOperationException)
#include <string>
#include <utility>

// The concrete writer + decorator headers (declared here, not in TokenWriter.hpp, to keep the
// interface header free of concrete-type includes).
#include "Decompiler/CSharp/OutputVisitor/InsertMissingTokensDecorator.hpp"
#include "Decompiler/CSharp/OutputVisitor/InsertRequiredSpacesDecorator.hpp"
#include "Decompiler/CSharp/OutputVisitor/TextWriterTokenWriter.hpp"

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

// The C# `public static TokenWriter Create(TextWriter writer, string indentation = "\t")` --
// the standard pipeline: a `TextWriterTokenWriter` wrapped in the `InsertRequiredSpacesDecorator`
// (the spaces-only stack, no span recording). The C# uses an object initializer
// (`{ IndentationString = indentation }`); the port constructs the writer then calls the
// `IndentationString` setter. The decorator takes ownership of the writer, so the returned
// handle owns the whole two-layer stack.
std::unique_ptr<TokenWriter> TokenWriter::Create(std::ostream* writer, std::string indentation) {
	auto target = std::make_unique<TextWriterTokenWriter>(writer);
	target->IndentationString(std::move(indentation));
	return std::make_unique<InsertRequiredSpacesDecorator>(std::move(target));
}

// The C# `public static TokenWriter CreateWriterThatSetsLocationsInAST(TextWriter writer,
// string indentation = "\t")` -- the full pipeline: a `TextWriterTokenWriter` wrapped in the
// `InsertMissingTokensDecorator` (which records source spans back onto the AST nodes) then the
// `InsertRequiredSpacesDecorator`. The `InsertMissingTokensDecorator` reads the current
// location from the inner `TextWriterTokenWriter` (which implements `ILocatable`), so the
// same writer plays both the wrapped-writer and the location-provider roles. The
// `ILocatable*` is grabbed from the writer BEFORE it is moved into the decorator; the writer
// object persists inside the decorator's `ownedWriter_` member, so the pointer stays valid for
// the decorator's lifetime. The outer `InsertRequiredSpacesDecorator` owns the
// `InsertMissingTokensDecorator` (which owns the `TextWriterTokenWriter`), so the returned
// handle owns the whole three-layer stack.
std::unique_ptr<TokenWriter> TokenWriter::CreateWriterThatSetsLocationsInAST(std::ostream* writer, std::string indentation) {
	auto target = std::make_unique<TextWriterTokenWriter>(writer);
	target->IndentationString(std::move(indentation));
	// `target` IS-A `ILocatable` (`TextWriterTokenWriter : TokenWriter, ILocatable`); grab the
	// subobject pointer before the `std::move` empties `target` (the object lives on, owned by
	// the decorator below, so the pointer is stable).
	ILocatable* locationProvider = static_cast<ILocatable*>(target.get());
	auto middle = std::make_unique<InsertMissingTokensDecorator>(std::move(target), locationProvider);
	return std::make_unique<InsertRequiredSpacesDecorator>(std::move(middle));
}

// The C# `public static TokenWriter InsertRequiredSpaces(TokenWriter writer)` -- wrap a
// caller-owned writer in the `InsertRequiredSpacesDecorator`. The caller keeps `writer` alive
// for the decorator's lifetime; the returned handle owns ONLY the new decorator (the non-owning
// inner pointer, faithful to the C# where the caller still holds the original).
std::unique_ptr<TokenWriter> TokenWriter::InsertRequiredSpaces(TokenWriter* writer) {
	return std::make_unique<InsertRequiredSpacesDecorator>(writer);
}

// The C# `public static TokenWriter WrapInWriterThatSetsLocationsInAST(TokenWriter writer)` --
// wrap a caller-owned `ILocatable` writer in the `InsertMissingTokensDecorator`, which reads
// the current location from the writer's `ILocatable` subobject. The C# `if (!(writer is
// ILocatable)) throw new InvalidOperationException(...)` ports to a `dynamic_cast<ILocatable*>`
// cross-cast (returns non-null iff the writer's runtime type derives from `ILocatable`) and a
// `std::logic_error` (the closest standard exception to `InvalidOperationException` -- a method
// call invalid for the object's current state). The returned handle owns ONLY the new decorator;
// the caller keeps `writer` alive (faithful to the C#).
std::unique_ptr<TokenWriter> TokenWriter::WrapInWriterThatSetsLocationsInAST(TokenWriter* writer) {
	ILocatable* locationProvider = dynamic_cast<ILocatable*>(writer);
	if (locationProvider == nullptr) {
		throw std::logic_error("writer does not provide locations!");
	}
	return std::make_unique<InsertMissingTokensDecorator>(writer, locationProvider);
}

}  // namespace ILSpy::Decompiler::CSharp::OutputVisitor
