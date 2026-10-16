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

// Tests for the ILSpyX Settings port (ICSharpCode.ILSpyX/Settings/): the
// mutex guard's nesting, the ILSpySettings provider (load, the section
// accessor, update-and-save, the missing-provider throw), the service's
// section cache, and the DecompilerSettings wrapper's flag table.

#include "ILSpyX/Settings/DecompilerSettings.hpp"
#include "ILSpyX/Settings/ILSpySettings.hpp"
#include "ILSpyX/Settings/MutexProtector.hpp"
#include "ILSpyX/Settings/SettingsServiceBase.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;
namespace Set = ILSpy::ILSpyX::Settings;
namespace Xml = ILSpy::Decompiler::Xml;

fs::path TempDir(const std::string& name)
{
    fs::path dir = fs::temp_directory_path() / ("ilspy_settings_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

std::string WriteText(const fs::path& dir, const std::string& name,
    const std::string& text)
{
    fs::path file = dir / name;
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << text;
    return file.string();
}

std::string ReadText(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Points the provider at a settings file; restores the previous value on
// destruction.
class ScopedSettingsPath {
public:
    explicit ScopedSettingsPath(std::string path)
    {
        previous_ = Set::ILSpySettings::SettingsFilePathProvider();
        Set::ILSpySettings::SettingsFilePathProvider() = [path] { return path; };
    }
    ~ScopedSettingsPath()
    {
        Set::ILSpySettings::SettingsFilePathProvider() = previous_;
    }

private:
    std::optional<std::function<std::string()>> previous_;
};

// A minimal section the service tests use.
class TestSection final : public Set::ISettingsSection {
public:
    std::string SectionName() const override { return "TestSection"; }
    void LoadFromXml(const Xml::XElement& section) override
    {
        const Xml::XAttribute* attribute =
            section.Attribute(Xml::XName("value"));
        if (attribute != nullptr)
            value = attribute->Value();
    }
    std::shared_ptr<Xml::XElement> SaveToXml() const override
    {
        auto element =
            std::make_shared<Xml::XElement>(Xml::XName("TestSection"));
        element->SetAttributeValue(Xml::XName("value"), value);
        return element;
    }

    std::string value = "default";
};

class TestService : public Set::SettingsServiceBase {
public:
    explicit TestService(std::shared_ptr<Set::ISettingsProvider> provider)
        : Set::SettingsServiceBase(std::move(provider))
    {
    }
};

}  // namespace

// ---- MutexProtector.

TEST(MutexProtectorTest, AcquireReleaseAndNest)
{
    {
        Set::MutexProtector first("test-mutex-name");
        {
            // The re-entrant acquire (the C# same-thread WaitOne).
            Set::MutexProtector nested("test-mutex-name");
        }
        // Still held by the outer scope.
    }
    // Re-acquirable after the full release.
    Set::MutexProtector again("test-mutex-name");
}

// ---- ILSpySettings.

TEST(ILSpySettingsTest, LoadReadsTheSections)
{
    fs::path dir = TempDir("load");
    std::string path = WriteText(dir, "settings.xml",
        "<ILSpy>\r\n"
        "  <TestSection value=\"from-file\" />\r\n"
        "</ILSpy>\r\n");
    ScopedSettingsPath scoped(path);

    Set::ILSpySettings settings = Set::ILSpySettings::Load();
    std::shared_ptr<Xml::XElement> section = settings.Section("TestSection");
    ASSERT_NE(section, nullptr);
    const Xml::XAttribute* attribute = section->Attribute(Xml::XName("value"));
    ASSERT_NE(attribute, nullptr);
    EXPECT_EQ(attribute->Value(), "from-file");
}

TEST(ILSpySettingsTest, MissingAndMalformedFilesLoadEmpty)
{
    fs::path dir = TempDir("empty");

    ScopedSettingsPath missing(
        (dir / "no-such-settings.xml").string());
    Set::ILSpySettings fromMissing = Set::ILSpySettings::Load();
    std::shared_ptr<Xml::XElement> section =
        fromMissing.Section("TestSection");
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(section->Attribute(Xml::XName("value")), nullptr);

    ScopedSettingsPath malformed(WriteText(dir, "bad.xml",
        "<ILSpy><unclosed></ILSpy>"));
    Set::ILSpySettings fromMalformed = Set::ILSpySettings::Load();
    section = fromMalformed.Section("TestSection");
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(section->Attribute(Xml::XName("value")), nullptr);
}

TEST(ILSpySettingsTest, UnknownSectionYieldsAnEmptyElement)
{
    Set::ILSpySettings settings;
    std::shared_ptr<Xml::XElement> section = settings.Section("Whatever");
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(section->Name().LocalName(), "Whatever");
    EXPECT_TRUE(section->Attribute(Xml::XName("value")) == nullptr
        || section->Attribute(Xml::XName("value"))->Value().empty());
}

TEST(ILSpySettingsTest, NoPathProviderThrows)
{
    // Clear the provider (a previous test may have set it).
    Set::ILSpySettings::SettingsFilePathProvider() = std::nullopt;
    try {
        (void)Set::ILSpySettings::Load();
        FAIL() << "expected the ArgumentNullException";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "Value cannot be null. (Parameter 'SettingsFilePathProvider')");
    }
}

TEST(ILSpySettingsTest, SaveSettingsWritesThroughTheProvider)
{
    fs::path dir = TempDir("save");
    std::string path = WriteText(dir, "settings.xml",
        "<ILSpy>\r\n  <OldSection value=\"x\" />\r\n</ILSpy>\r\n");
    ScopedSettingsPath scoped(path);

    Set::ILSpySettings settings = Set::ILSpySettings::Load();
    auto section = std::make_shared<Xml::XElement>(Xml::XName("NewSection"));
    section->SetAttributeValue(Xml::XName("value"), std::string("y"));
    settings.SaveSettings(section);

    // The file carries the version stamp, the old section, and the new one.
    const std::string saved = ReadText(path);
    EXPECT_NE(saved.find("version=\"11.0.0."), std::string::npos);
    EXPECT_NE(saved.find("<OldSection value=\"x\""), std::string::npos);
    EXPECT_NE(saved.find("<NewSection value=\"y\""), std::string::npos);

    // A re-load sees the saved section.
    Set::ILSpySettings reloaded = Set::ILSpySettings::Load();
    const Xml::XAttribute* attribute =
        reloaded.Section("NewSection")->Attribute(Xml::XName("value"));
    ASSERT_NE(attribute, nullptr);
    EXPECT_EQ(attribute->Value(), "y");
}

TEST(ILSpySettingsTest, UpdateStampsVersionAndAppliesTheAction)
{
    fs::path dir = TempDir("update");
    std::string path = (dir / "fresh.xml").string();
    ScopedSettingsPath scoped(path);

    Set::ILSpySettings settings;
    settings.Update([](Xml::XElement& root) {
        root.Add(std::make_shared<Xml::XElement>(Xml::XName("Added")));
    });
    const std::string saved = ReadText(path);
    EXPECT_NE(saved.find("version=\"11.0.0.$INSERTREVISION$\""),
        std::string::npos);
    EXPECT_NE(saved.find("<Added"), std::string::npos);
}

// ---- SettingsServiceBase.

TEST(SettingsServiceBaseTest, GetSettingsCachesAndLoads)
{
    fs::path dir = TempDir("service");
    std::string path = WriteText(dir, "settings.xml",
        "<ILSpy>\r\n  <TestSection value=\"loaded\" />\r\n</ILSpy>\r\n");
    ScopedSettingsPath scoped(path);

    TestService service(
        std::make_shared<Set::ILSpySettings>(Set::ILSpySettings::Load()));
    TestSection& section = service.GetSettings<TestSection>();
    EXPECT_EQ(section.value, "loaded");

    // The cache: the same instance on a second call.
    TestSection& again = service.GetSettings<TestSection>();
    EXPECT_EQ(&again, &section);
}

// ---- The DecompilerSettings wrapper.

TEST(DecompilerSettingsSectionTest, LoadReadsFlagAttributes)
{
    Set::DecompilerSettings settings;
    EXPECT_FALSE(settings.RemoveDeadCode());
    EXPECT_FALSE(settings.UseNestedDirectoriesForNamespaces());

    auto element =
        std::make_shared<Xml::XElement>(Xml::XName("DecompilerSettings"));
    element->SetAttributeValue(Xml::XName("RemoveDeadCode"),
        std::string("true"));
    element->SetAttributeValue(Xml::XName("UseNestedDirectoriesForNamespaces"),
        std::string("1"));
    settings.LoadFromXml(*element);
    EXPECT_TRUE(settings.RemoveDeadCode());
    EXPECT_TRUE(settings.UseNestedDirectoriesForNamespaces());
}

TEST(DecompilerSettingsSectionTest, LoadThrowsForNonBooleanAttributes)
{
    Set::DecompilerSettings settings;
    auto element =
        std::make_shared<Xml::XElement>(Xml::XName("DecompilerSettings"));
    element->SetAttributeValue(Xml::XName("RemoveDeadCode"),
        std::string("yes-please"));
    try {
        settings.LoadFromXml(*element);
        FAIL() << "expected the FormatException";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(),
            "The string 'yes-please' is not a valid Boolean value.");
    }
}

TEST(DecompilerSettingsSectionTest, SaveWritesEveryBrowsableFlag)
{
    Set::DecompilerSettings settings;
    settings.SetRemoveDeadCode(true);
    std::shared_ptr<Xml::XElement> element = settings.SaveToXml();
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(element->Name().LocalName(), "DecompilerSettings");

    // The C# reflection surface: 110 Browsable bool properties.
    const Xml::XAttribute* attribute = element->FirstAttribute();
    std::size_t count = 0;
    bool sawRemoveDeadCode = false;
    bool sawLanguageVersion = false;
    while (attribute != nullptr) {
        count++;
        if (attribute->Name().LocalName() == "RemoveDeadCode") {
            sawRemoveDeadCode = true;
            EXPECT_EQ(attribute->Value(), "true");
        }
        if (attribute->Name().LocalName() == "LanguageVersion")
            sawLanguageVersion = true;
        attribute = attribute->NextAttribute();
    }
    EXPECT_EQ(count, 110u);
    EXPECT_TRUE(sawRemoveDeadCode);
    // LanguageVersion is not a bool and is not in the reflected surface.
    EXPECT_FALSE(sawLanguageVersion);
}

TEST(DecompilerSettingsSectionTest, RoundTripsThroughTheXml)
{
    Set::DecompilerSettings settings;
    settings.SetRemoveDeadCode(true);
    settings.SetRemoveDeadStores(true);
    settings.SetFileScopedNamespaces(false);  // the engine default is true
    std::shared_ptr<Xml::XElement> saved = settings.SaveToXml();

    Set::DecompilerSettings reloaded;
    reloaded.LoadFromXml(*saved);
    EXPECT_TRUE(reloaded.RemoveDeadCode());
    EXPECT_TRUE(reloaded.RemoveDeadStores());
    EXPECT_FALSE(reloaded.FileScopedNamespaces());
}

TEST(DecompilerSettingsSectionTest, IsKnownOptionMatchesTheReflectedSurface)
{
    EXPECT_TRUE(Set::DecompilerSettings::IsKnownOption("RemoveDeadCode"));
    EXPECT_TRUE(
        Set::DecompilerSettings::IsKnownOption("UseNestedDirectoriesForNamespaces"));
    EXPECT_FALSE(Set::DecompilerSettings::IsKnownOption("LanguageVersion"));
    EXPECT_FALSE(Set::DecompilerSettings::IsKnownOption("NoSuchOption"));
}
