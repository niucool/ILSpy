// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.BamlDecompiler/BamlDecompilerSettings.cs (Siegfried
// Pammer, 2021, MIT): the settings bag the XAML decompiler and its
// UniversalAssemblyResolver consult. The only behavior-carrying member is
// `ThrowOnAssemblyResolveErrors` (default true) -- the flag
// `XamlDecompiler.CreateTypeSystemFromFile` passes to the resolver, deciding
// whether an unresolvable assembly reference throws or degrades.
//
// C#-to-C++ porting decisions:
//  * The `INotifyPropertyChanged` surface (the `PropertyChanged` event and the
//    `OnPropertyChanged` raiser the setter calls) is a GUI data-binding hook --
//    no ported consumer subscribes (the CLI constructs a fresh instance and
//    only reads the flag), so the event stays a documented deferral. The
//    setter keeps the read-modify-compare-no-op shape (the flag write).
//  * `[Browsable(false)]` is a designer attribute with no runtime behavior.

#pragma once

namespace ILSpy::BamlDecompiler {

// The C# `public class BamlDecompilerSettings : INotifyPropertyChanged`.
class BamlDecompilerSettings {
public:
    // The C# `bool ThrowOnAssemblyResolveErrors { get; set; }` (the backing
    // field initializes to true).
    bool ThrowOnAssemblyResolveErrors() const
    {
        return throwOnAssemblyResolveErrors_;
    }

    void SetThrowOnAssemblyResolveErrors(bool value)
    {
        throwOnAssemblyResolveErrors_ = value;
    }

private:
    // The C# `bool throwOnAssemblyResolveErrors = true;`.
    bool throwOnAssemblyResolveErrors_ = true;
};

} // namespace ILSpy::BamlDecompiler
