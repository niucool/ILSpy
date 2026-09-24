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

// Port of ICSharpCode.ILSpyX/Settings/SettingsServiceBase.cs: the section
// cache over an ISettingsProvider.
//
// C#-to-C++ porting decisions:
//  * The C# primary-constructor `SettingsServiceBase(ISettingsProvider
//    spySettings)` ports as an explicit ctor.
//  * The C# `ConcurrentDictionary<Type, ISettingsSection>` ports as a
//    std::map keyed on std::type_index (the CLI host is single-threaded;
//    the C# concurrency guard has no observable effect there).
//  * `GetSettings<T>() where T : ISettingsSection, new()` ports as the
//    member template below (T default-constructible).
//  * The PropertyChanged wiring does not port (see ISettingsProvider.hpp);
//    Section_PropertyChanged stays as the protected virtual no-op hook.

#pragma once

#include "ILSpyX/Settings/ISettingsProvider.hpp"

#include <map>
#include <memory>
#include <typeindex>

namespace ILSpy::ILSpyX::Settings {

// The C# `public class SettingsServiceBase(ISettingsProvider
// spySettings)`.
class SettingsServiceBase {
public:
    explicit SettingsServiceBase(std::shared_ptr<ISettingsProvider> spySettings)
        : spySettings_(std::move(spySettings))
    {
    }
    virtual ~SettingsServiceBase() = default;

    // The C# `public T GetSettings<T>() where T : ISettingsSection,
    // new()`: the cached section instance, loaded from the provider's
    // section element on first use.
    template <typename T>
    T& GetSettings()
    {
        static_assert(std::is_base_of_v<ISettingsSection, T>,
            "GetSettings<T> requires T : ISettingsSection");
        auto it = sections_.find(std::type_index(typeid(T)));
        if (it == sections_.end()) {
            auto section = std::make_unique<T>();
            std::shared_ptr<Decompiler::Xml::XElement> element =
                spySettings_->Section(section->SectionName());
            section->LoadFromXml(*element);
            it = sections_
                     .emplace(std::type_index(typeid(T)), std::move(section))
                     .first;
        }
        return static_cast<T&>(*it->second);
    }

    // The C# `protected static void SaveSection(ISettingsSection section,
    // XElement root)`: replace-or-add the section's element under root.
    static void SaveSection(ISettingsSection& section,
        Decompiler::Xml::XElement& root);

protected:
    // The C# `protected ISettingsProvider SpySettings { get; set; }`.
    const std::shared_ptr<ISettingsProvider>& SpySettings() const
    {
        return spySettings_;
    }

    // The C# `protected virtual void Section_PropertyChanged(...)`: the
    // no-op hook (the notification source does not port).
    virtual void SectionPropertyChanged() {}

private:
    std::shared_ptr<ISettingsProvider> spySettings_;
    std::map<std::type_index, std::unique_ptr<ISettingsSection>> sections_;
};

}  // namespace ILSpy::ILSpyX::Settings
