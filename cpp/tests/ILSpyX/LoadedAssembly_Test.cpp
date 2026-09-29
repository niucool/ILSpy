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

// Tests for the LoadedAssembly port (ICSharpCode.ILSpyX/LoadedAssembly.cs):
// the lazy load pipeline through the loader registry and the PEFileLoader
// fallback, the load-status contract (IsLoaded / IsLoadedAsValidAssembly /
// HasLoadError -- none of which trigger the load), the display Text, the
// target-framework detection and override, the debug-info wiring, and the
// Loaded-notification contract.
//
// The failure-message mapping is pinned against the C# oracle (the
// BadImageFormatException ladder System.Reflection.Metadata throws out of
// the PEReader constructor, probed with ilspycmd 11.0.0.9335-rc; see
// PORT_LOG_BAML.md).

#include "ILSpyX/AssemblyList.hpp"

#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "ILSpyX/FileLoaders/FileLoaderRegistry.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

using ILSpy::ILSpyX::AssemblyList;
using ILSpy::ILSpyX::LoadedAssembly;
namespace FileLoaders = ILSpy::ILSpyX::FileLoaders;

// Parks the load until the test releases it, so tests can observe the
// load's in-flight window from other threads. Registered last in a
// private registry: the stock loaders all decline for garbage bytes,
// so the park happens inside the load proper. After release the stock
// PE fallback re-runs (the eventual failure message is the fallback's
// -- these tests pin the concurrency, not the message).
struct ParkingLoader : public FileLoaders::IFileLoader {
    mutable std::mutex Mtx;
    mutable std::condition_variable Cv;
    bool Released = false;
    mutable std::atomic<int> Calls{0};
    mutable std::atomic<bool> Entered{false};
    // Set when the parked load finished (the post-release epilogue ran).
    mutable std::atomic<bool> Done{false};

    std::optional<FileLoaders::LoadResult> Load(const std::string&,
        const std::uint8_t*, std::size_t,
        const FileLoaders::FileLoadContext&) const override
    {
        ++Calls;
        Entered = true;
        {
            std::unique_lock lock(Mtx);
            Cv.wait(lock, [this] { return Released; });
        }
        // A bounded post-release tail: the destructor-join contract
        // asserts on it (the destroyer must not return before the
        // worker does).
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        Done = true;
        FileLoaders::LoadResult failure;
        failure.FileLoadException = "parking loader declined";
        return failure;
    }

    void Release()
    {
        {
            std::lock_guard lock(Mtx);
            Released = true;
        }
        Cv.notify_all();
    }

    // Spins (bounded) until a loader is parked inside Load.
    static bool WaitEntered(const std::atomic<bool>& entered)
    {
        for (int i = 0; i < 1000 && !entered.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return entered.load();
    }
};

// Writes arbitrary bytes to a temp file and returns the path.
std::string WriteBytes(const std::string& fileName, const std::string& bytes)
{
    fs::path path = fs::temp_directory_path() / fileName;
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    return path.string();
}

TEST(LoadedAssemblyTest, OpenAssemblyLoadsAValidAssembly) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    // Opening does not trigger the lazy load (the C# Lazy deferral); the
    // explicit demand below does.
    EXPECT_FALSE(asm_.IsLoaded());
    (void)asm_.GetLoadResult();
    EXPECT_TRUE(asm_.IsLoaded());
    EXPECT_TRUE(asm_.IsLoadedAsValidAssembly());
    EXPECT_FALSE(asm_.HasLoadError());
    EXPECT_EQ(asm_.FileName(), file);
    EXPECT_EQ(asm_.ShortName(), "ilspy_connid_test");
}

TEST(LoadedAssemblyTest, GetLoadResultCarriesTheMetadataFile) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    const auto& result = asm_.GetLoadResult();
    ASSERT_NE(result.MetadataFile, nullptr);
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(&result.MetadataFile->FileName()[0] != nullptr, true);
    EXPECT_EQ(result.MetadataFile->FileName(), file);
}

TEST(LoadedAssemblyTest, TextRendersAssemblyVersionAndTargetFramework) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    // Compose the expected display from the metadata the C# Text getter
    // reads: "{0} ({1})" over the short name and
    // "<version>[, <tfm with 'Version=' replaced by ' '>]". The fixture's
    // own values pin the composition without pinning fixture bytes.
    const auto& loadResult = asm_.GetLoadResult();
    ASSERT_NE(loadResult.MetadataFile, nullptr);
    const auto& module = *loadResult.MetadataFile;
    auto asmDef = module.GetAssemblyDefinition();
    std::string expected;
    if (asmDef.has_value()) {
        ILSpy::Decompiler::TypeSystem::Version version(
            asmDef->MajorVersion, asmDef->MinorVersion, asmDef->BuildNumber,
            asmDef->RevisionNumber);
        expected = asm_.ShortName() + " (" + version.ToString();
        auto tfm = ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(module);
        if (tfm.has_value() && !tfm->empty()) {
            std::string rendered = *tfm;
            std::size_t pos = 0;
            while ((pos = rendered.find("Version=", pos)) != std::string::npos) {
                rendered.replace(pos, 8, " ");
                pos += 1;
            }
            expected += ", " + rendered;
        }
        expected += ")";
    } else {
        expected = asm_.ShortName() + " (.netmodule)";
    }
    EXPECT_EQ(asm_.Text(), expected);
}

TEST(LoadedAssemblyTest, TextRendersNetmoduleForNonAssemblyModules) {
    AssemblyList list;
    std::string file = WriteTinyNetModule();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    // Text before a completed load falls back to the short name without
    // triggering (the C# IsLoaded guard).
    EXPECT_EQ(asm_.Text(), "ilspy_tiny_test");
    // The tiny fixture is a netmodule (no assembly manifest): after the
    // load the C# Text getter renders ".netmodule".
    (void)asm_.GetLoadResult();
    EXPECT_EQ(asm_.Text(), "ilspy_tiny_test (.netmodule)");
}

TEST(LoadedAssemblyTest, LoadingGarbageReportsUnknownFileFormat) {
    AssemblyList list;
    std::string file =
        WriteBytes("ilspy_la_garbage.bin", "not a pe file at all");
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    // The C# PEReader throws BadImageFormatException("Unknown file
    // format."); the port's LoadCore maps the invalid parse back to that
    // message (the never-throwing MetadataFile divergence shim).
    EXPECT_THROW(
        try { (void)asm_.GetLoadResult(); } catch (
            const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Unknown file format.");
            throw;
        },
        std::runtime_error);
    EXPECT_TRUE(asm_.HasLoadError());
    EXPECT_FALSE(asm_.IsLoadedAsValidAssembly());
    EXPECT_EQ(asm_.GetMetadataFileOrNull(), nullptr);
}

TEST(LoadedAssemblyTest, LoadingShortMzImageReportsImageTooSmall) {
    AssemblyList list;
    // MZ magic but only 44 bytes: PEReader's "Image is either too small or
    // contains an invalid byte offset or count." (probed with the oracle).
    std::string bytes = "MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00\xFF\xFF\x00\x00\x40\x00\x01\x00\x00\x00garbagegarbage";
    std::string file = WriteBytes("ilspy_la_mzshort.bin", bytes);
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    EXPECT_THROW(
        try { (void)asm_.GetLoadResult(); } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(),
                "Image is either too small or contains an invalid byte "
                "offset or count.");
            throw;
        },
        std::runtime_error);
    EXPECT_TRUE(asm_.HasLoadError());
}

TEST(LoadedAssemblyTest, GetMetadataFileThrowsTheLoadFailureMessage) {
    AssemblyList list;
    std::string file = WriteBytes("ilspy_la_garbage2.bin", "not a pe file");
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    EXPECT_THROW(
        try { asm_.GetMetadataFile(); } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Unknown file format.");
            throw;
        },
        std::runtime_error);
}

TEST(LoadedAssemblyTest, StatusPollingDoesNotTriggerTheLoad) {
    AssemblyList list;
    std::string file = (fs::temp_directory_path() / "ilspy_la_missing.dll").string();
    std::error_code ec;
    fs::remove(fs::path(file), ec);
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    // None of the status members may trigger the lazy load (the C# uses
    // Lazy.IsValueCreated so tree-icon polling never starts a load): the
    // file does not exist, so a triggered load would fault immediately.
    EXPECT_FALSE(asm_.IsLoaded());
    EXPECT_FALSE(asm_.IsLoadedAsValidAssembly());
    EXPECT_FALSE(asm_.HasLoadError());
    // Text without a completed load falls back to the short name.
    EXPECT_EQ(asm_.Text(), "ilspy_la_missing");
    // An explicit demand triggers the load and faults it.
    EXPECT_EQ(asm_.GetMetadataFileOrNull(), nullptr);
    EXPECT_TRUE(asm_.HasLoadError());
}

TEST(LoadedAssemblyTest, LoadedListenerFiresOnceWhenTheLoadCompletes) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    int fired = 0;
    asm_.AddLoadedListener([&fired] { ++fired; });
    (void)asm_.GetLoadResult();
    EXPECT_EQ(fired, 1);
    // A second demand does not re-fire (the C# event fires once per load).
    (void)asm_.GetLoadResult();
    EXPECT_EQ(fired, 1);
}

TEST(LoadedAssemblyTest, LoadedListenerIsNotRetroactive) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    int fired = 0;
    asm_.AddLoadedListener([&fired] { ++fired; });
    // The C# doc: "If the load has already finished by the time you
    // subscribe, the event will not fire retroactively -- check
    // IsLoaded after subscribing."
    EXPECT_EQ(fired, 0);
    EXPECT_TRUE(asm_.IsLoaded());
}

// ---- The async load machinery (the promise/thread + detach-on-timeout
// design from PORT_LOG_BAML.md; the library joins, the host may _Exit).

TEST(LoadedAssemblyTest, IsLoadedIsFalseWhileTheLoadIsInFlight) {
    AssemblyList list;
    FileLoaders::FileLoaderRegistry registry;
    auto loader = std::make_unique<ParkingLoader>();
    ParkingLoader* raw = loader.get();
    registry.Register(std::move(loader));
    LoadedAssembly::Options options;
    options.FileLoaders = &registry;
    std::string file = WriteBytes("ilspy_la_parked.bin",
        "parked bytes, not an image");
    auto la = std::make_unique<LoadedAssembly>(list, file, options);

    std::thread demander([&la] {
        try {
            (void)la->GetLoadResult();
        } catch (const std::exception&) {
        }
    });
    ASSERT_TRUE(ParkingLoader::WaitEntered(raw->Entered));
    // The C# window: Lazy.Value created (the demand happened) but
    // Task.IsCompleted false -- IsLoaded is false while the load runs.
    // A watchdog releases the loader so an implementation that blocks
    // the poll behind the in-flight load cannot hang the suite.
    std::mutex gateMtx;
    std::condition_variable gateCv;
    bool testDone = false;
    std::thread watchdog([&] {
        std::unique_lock lock(gateMtx);
        gateCv.wait_for(lock, std::chrono::seconds(2),
            [&] { return testDone; });
        raw->Release();
    });
    EXPECT_FALSE(la->IsLoaded());
    {
        std::lock_guard lock(gateMtx);
        testDone = true;
    }
    gateCv.notify_all();
    raw->Release();
    watchdog.join();
    demander.join();
    EXPECT_TRUE(la->IsLoaded());
    EXPECT_TRUE(la->HasLoadError());
}

TEST(LoadedAssemblyTest, StatusPollsProceedWhileTheLoadIsInFlight) {
    AssemblyList list;
    FileLoaders::FileLoaderRegistry registry;
    auto loader = std::make_unique<ParkingLoader>();
    ParkingLoader* raw = loader.get();
    registry.Register(std::move(loader));
    LoadedAssembly::Options options;
    options.FileLoaders = &registry;
    std::string file = WriteBytes("ilspy_la_poll.bin",
        "parked bytes, not an image");
    auto la = std::make_unique<LoadedAssembly>(list, file, options);

    std::thread demander([&la] {
        try {
            (void)la->GetLoadResult();
        } catch (const std::exception&) {
        }
    });
    ASSERT_TRUE(ParkingLoader::WaitEntered(raw->Entered));
    // A status poll must not block behind the in-flight load: poll from
    // this thread while the loader is parked. A watchdog releases the
    // loader so a wedged implementation cannot hang the suite.
    std::mutex gateMtx;
    std::condition_variable gateCv;
    bool testDone = false;
    std::thread watchdog([&] {
        std::unique_lock lock(gateMtx);
        gateCv.wait_for(lock, std::chrono::seconds(2),
            [&] { return testDone; });
        raw->Release();
    });
    EXPECT_FALSE(la->IsLoaded());
    {
        std::lock_guard lock(gateMtx);
        testDone = true;
    }
    gateCv.notify_all();
    raw->Release();
    watchdog.join();
    demander.join();
    EXPECT_TRUE(la->HasLoadError());
}

TEST(LoadedAssemblyTest, ConcurrentDemandersShareOneLoad) {
    AssemblyList list;
    FileLoaders::FileLoaderRegistry registry;
    auto loader = std::make_unique<ParkingLoader>();
    ParkingLoader* raw = loader.get();
    registry.Register(std::move(loader));
    LoadedAssembly::Options options;
    options.FileLoaders = &registry;
    std::string file = WriteBytes("ilspy_la_shared.bin",
        "parked bytes, not an image");
    auto la = std::make_unique<LoadedAssembly>(list, file, options);

    std::vector<std::string> messages(4);
    std::vector<std::thread> demanders;
    for (int i = 0; i < 4; i++) {
        demanders.emplace_back([&, i] {
            try {
                (void)la->GetLoadResult();
            } catch (const std::exception& ex) {
                messages[i] = ex.what();
            }
        });
    }
    // The first demander is parked inside the load; no demander can
    // complete before the release.
    ASSERT_TRUE(ParkingLoader::WaitEntered(raw->Entered));
    raw->Release();
    for (auto& d : demanders) {
        d.join();
    }
    EXPECT_EQ(raw->Calls.load(), 1);
    // Every demander observed the same load outcome.
    for (const auto& message : messages) {
        EXPECT_EQ(message, messages[0]);
    }
}

TEST(LoadedAssemblyTest, WaitForLoadedTimesOutWhileTheLoadIsInFlight) {
    AssemblyList list;
    FileLoaders::FileLoaderRegistry registry;
    auto loader = std::make_unique<ParkingLoader>();
    ParkingLoader* raw = loader.get();
    registry.Register(std::move(loader));
    LoadedAssembly::Options options;
    options.FileLoaders = &registry;
    std::string file = WriteBytes("ilspy_la_waitfor.bin",
        "parked bytes, not an image");
    auto la = std::make_unique<LoadedAssembly>(list, file, options);

    // The bounded wait is a demand: it starts the load, then waits at
    // most the deadline (the bennu wait_for arm; no C# analogue -- the
    // C# host would Task.Wait(timeout)).
    EXPECT_FALSE(la->WaitForLoaded(std::chrono::milliseconds(50)));
    EXPECT_FALSE(la->IsLoaded());
    raw->Release();
    EXPECT_TRUE(la->WaitForLoaded(std::chrono::seconds(2)));
    EXPECT_TRUE(la->IsLoaded());
    EXPECT_TRUE(la->HasLoadError());
}

TEST(LoadedAssemblyTest, WaitForLoadedAfterCompletionReturnsImmediately) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    (void)asm_.GetLoadResult();
    // Completed loads satisfy the wait instantly, even with a zero
    // deadline.
    EXPECT_TRUE(asm_.WaitForLoaded(std::chrono::milliseconds(0)));
}

TEST(LoadedAssemblyTest, DestructionWaitsForTheInFlightLoad) {
    AssemblyList list;
    FileLoaders::FileLoaderRegistry registry;
    auto loader = std::make_unique<ParkingLoader>();
    ParkingLoader* raw = loader.get();
    registry.Register(std::move(loader));
    LoadedAssembly::Options options;
    options.FileLoaders = &registry;
    std::string file = WriteBytes("ilspy_la_join.bin",
        "parked bytes, not an image");
    auto la = std::make_unique<LoadedAssembly>(list, file, options);

    std::thread demander([&la] {
        try {
            (void)la->GetLoadResult();
        } catch (const std::exception&) {
        }
    });
    ASSERT_TRUE(ParkingLoader::WaitEntered(raw->Entered));
    raw->Release();
    // Destroy while the load's post-release tail is still running: the
    // destructor must wait for the worker (the bennu rule: join on the
    // normal path) so the object never dies under a live load.
    la.reset();
    demander.join();
    EXPECT_TRUE(raw->Done.load());
}

TEST(LoadedAssemblyTest, TargetFrameworkIdOverrideTrimsAndNormalizesBlank) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    asm_.SetTargetFrameworkIdOverride("  .NETFramework,Version=v4.8  ");
    ASSERT_TRUE(asm_.TargetFrameworkIdOverride().has_value());
    EXPECT_EQ(*asm_.TargetFrameworkIdOverride(), ".NETFramework,Version=v4.8");
    // Blank values normalize to no override (a hand-edited
    // TargetFramework="" attribute).
    asm_.SetTargetFrameworkIdOverride("   ");
    EXPECT_FALSE(asm_.TargetFrameworkIdOverride().has_value());
    asm_.SetTargetFrameworkIdOverride("");
    EXPECT_FALSE(asm_.TargetFrameworkIdOverride().has_value());
}

TEST(LoadedAssemblyTest, GetTargetFrameworkIdPrefersTheOverride) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    // An explicit override wins over (and bypasses) detection, even once
    // detection has already cached a value.
    (void)asm_.GetTargetFrameworkId();
    asm_.SetTargetFrameworkIdOverride(".NETFramework,Version=v4.8");
    EXPECT_EQ(asm_.GetTargetFrameworkId(), ".NETFramework,Version=v4.8");
}

TEST(LoadedAssemblyTest, GetTargetFrameworkIdMatchesDetection) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    const auto& loadResult = asm_.GetLoadResult();
    ASSERT_NE(loadResult.MetadataFile, nullptr);
    auto detected =
        ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(
            *loadResult.MetadataFile);
    std::string expected = detected.value_or("");
    EXPECT_EQ(asm_.GetTargetFrameworkId(), expected);
}

TEST(LoadedAssemblyTest, GetRuntimePackMatchesDetection) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    const auto& loadResult = asm_.GetLoadResult();
    ASSERT_NE(loadResult.MetadataFile, nullptr);
    // The C# `assembly.DetectRuntimePack() ?? string.Empty` fallback.
    std::string expected = ILSpy::Decompiler::Metadata::DetectRuntimePack(
        *loadResult.MetadataFile);
    EXPECT_EQ(asm_.GetRuntimePack(), expected);
}

TEST(LoadedAssemblyTest, IsAutoLoadedDefaultsFalseAndIsSettable) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file, /*isAutoLoaded=*/true);
    EXPECT_TRUE(asm_.IsAutoLoaded());
    LoadedAssembly& asm2 = list.OpenAssembly(
        WriteTinyNetModule());
    EXPECT_FALSE(asm2.IsAutoLoaded());
}

TEST(LoadedAssemblyTest, BundleCtorCarriesTheParentBundle) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& bundle = list.OpenAssembly(file);
    LoadedAssembly entry(bundle, "entry.dll");
    EXPECT_EQ(entry.ParentBundle(), &bundle);
}

TEST(LoadedAssemblyTest, IsLoadedIsFalseForANeverLoadedInstance) {
    AssemblyList list;
    std::string file = ILSpy::Tests::WriteConnIdResDll();
    LoadedAssembly& asm_ = list.OpenAssembly(file);
    // Constructing the entry must not start the load (the C# Lazy
    // deferral: constructing entries en masse must not flood the pool).
    EXPECT_FALSE(asm_.IsLoaded());
}

}  // namespace
