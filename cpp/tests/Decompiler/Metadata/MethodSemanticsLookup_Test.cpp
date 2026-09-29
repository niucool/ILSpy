// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the MethodSemanticsLookup port (MethodSemanticsLookup.cs + the
// MetadataFile.MethodSemanticsLookup lazy property + MetadataFile::MethodSemanticsRows):
// the accessor->association lookup every MetadataMethod ctor consults.
//
// Every expectation is gold-pinned against the REAL installed ICSharpCode.Decompiler
// 11.0 driven over the identical fixtures (the C:/temp-probe/MslProbe gold probe,
// gold_final.txt):
//   * the crafted MslSynth.dll manifest (the real MetadataBuilder bytes): the full
//     sorted entry list (7 entries), GetSemantics drives over all 13 methods plus
//     the nil and past-the-end handles, the Raiser-filter variant (8 entries -- the
//     raiser row enters), and the Other-filter rejection (the exact
//     NotSupportedException message);
//   * the raw MethodSemanticsRows enumeration (the 12 synth rows: the last-row-wins
//     P4 pair, the nil-method P5 pair, the combined 0x3 row, the raiser row);
//   * the whole-corpus pins over the real fixtures: the FNV-1a-64 digest over the
//     whole sorted entry list AND over the GetSemantics result of EVERY method row
//     (mscorlib 5986 entries / 29257 methods, System.dll 5407 / 18170, CoreLib 6134
//     / 42371, the empty facade, tiny.netmodule), the semantics/association census,
//     and the first/last entry spot pins;
//   * the curated named drives: mscorlib's get_Chars / the property-1 getter / the
//     Equals overloads / Copy / the nil handle / a past-the-end row, System.dll's
//     first event adder/remover.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include <cstdlib>
#include "Decompiler/Metadata/MethodSemanticsLookup.hpp"
#include "TestFixtures/MslSynth.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;

bool FileAvailable(const char* path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

const char* SystemPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

const char* CoreLibPath() {
#if defined(_WIN32)
    // The gold corpus pins the .NET 10 metadata shape, so the locator
    // takes the highest installed 10.x Microsoft.NETCore.App (""
    // when none is installed -- the caller skips the CoreLib entry).
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App";
    std::error_code ec;
    std::string best;
    int bestMinor = -1;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("10.", 0) != 0) continue;
        int minor = 0;
        try {
            minor = std::stoi(name.substr(3));
        } catch (const std::logic_error&) {
            continue;
        }
        std::string candidate = it->path().string() + "\\System.Private.CoreLib.dll";
        if (minor > bestMinor && fs::exists(candidate, ec)) {
            best = candidate;
            bestMinor = minor;
        }
    }
    static const std::string bestPath = best;
    return bestPath.c_str();
#else
    return "";
#endif
}

const char* FacadePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
#else
    return "";
#endif
}

// The probe's SemName: the .NET enum member names, with the numeric fallback
// "(N)" -- SRM's MethodSemanticsAttributes has NO None member, so the miss
// shape (the flag 0) renders "(0)".
std::string SemName(TS::MethodSemanticsAttributes sem) {
    switch (sem) {
        case TS::MethodSemanticsAttributes::Setter:
            return "Setter";
        case TS::MethodSemanticsAttributes::Getter:
            return "Getter";
        case TS::MethodSemanticsAttributes::Other:
            return "Other";
        case TS::MethodSemanticsAttributes::Adder:
            return "Adder";
        case TS::MethodSemanticsAttributes::Remover:
            return "Remover";
        case TS::MethodSemanticsAttributes::Raiser:
            return "Raiser";
        default:
            return "(" + std::to_string(static_cast<std::uint32_t>(sem)) + ")";
    }
}

// The probe's AssocToken: "0x%08x" for a real association.
std::string AssocTokenHex(std::uint32_t token) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", token);
    return buffer;
}

// The probe's entry render: "sem:<SemName> row:<n> assoc:<AssocToken>".
std::string EntryLine(const TM::MethodSemanticsLookup::Entry& e) {
    return "sem:" + SemName(e.Semantics) + " row:" + std::to_string(e.MethodRowNumber)
        + " assoc:" + AssocTokenHex(e.AssociationToken);
}

// The probe's Gs render: "<assoc>|<SemName>" for a found method, "nil|<SemName>"
// for the miss (the nil association).
std::string GsLine(const TM::MethodSemanticsLookup::SemanticsInfo& info) {
    std::string assoc = info.AssociationToken == 0
        ? "nil"
        : AssocTokenHex(info.AssociationToken);
    return assoc + "|" + SemName(info.Semantics);
}

// The probe's FNV-1a-64 (each line feeds the bytes, then 0xff, then the prime
// multiply -- the MetadataFieldTest convention).
class Fnv64 {
public:
    void Add(const std::string& s) {
        for (char ch : s) {
            fnv_ ^= static_cast<std::uint8_t>(ch);
            fnv_ *= 0x100000001b3ULL;
        }
        fnv_ ^= 0xff;
        fnv_ *= 0x100000001b3ULL;
    }
    std::uint64_t Digest() const { return fnv_; }

private:
    std::uint64_t fnv_ = 0xcbf29ce484222325ULL;
};

// ---------------------------------------------------------------------------
// The crafted MslSynth.dll manifest.
// ---------------------------------------------------------------------------

TEST(MethodSemanticsLookupTest, SynthEntriesMatchGold) {
    std::string synthPath = ILSpy::Tests::WriteMslSynthDll();
    TM::MetadataFile file{ synthPath };
    ASSERT_TRUE(file.IsValid());
    const TM::MethodSemanticsLookup& lookup = file.GetMethodSemanticsLookup();

    const auto& entries = lookup.Entries();
    ASSERT_EQ(entries.size(), 7u);
    // The gold S|entry lines, in sorted order.
    EXPECT_EQ(EntryLine(entries[0]), "sem:Getter row:1 assoc:0x17000001");
    EXPECT_EQ(EntryLine(entries[1]), "sem:Setter row:2 assoc:0x17000001");
    EXPECT_EQ(EntryLine(entries[2]), "sem:Getter row:3 assoc:0x17000002");
    EXPECT_EQ(EntryLine(entries[3]), "sem:Setter row:4 assoc:0x17000003");
    // P4: two Getter rows (M5 then M6) -- the LAST wins, so M6 (row 6) is the
    // getter and M5 (row 5) is NOT an accessor.
    EXPECT_EQ(EntryLine(entries[4]), "sem:Getter row:6 assoc:0x17000004");
    EXPECT_EQ(EntryLine(entries[5]), "sem:Adder row:8 assoc:0x14000001");
    EXPECT_EQ(EntryLine(entries[6]), "sem:Remover row:9 assoc:0x14000001");
}

TEST(MethodSemanticsLookupTest, SynthGetSemanticsDrivesMatchGold) {
    std::string synthPath = ILSpy::Tests::WriteMslSynthDll();
    TM::MetadataFile file{ synthPath };
    const TM::MethodSemanticsLookup& lookup = file.GetMethodSemanticsLookup();

    // The gold S|gs lines, one drive per method row.
    struct Drive {
        std::uint32_t row;
        const char* gold;
    };
    const Drive drives[] = {
        { 1, "0x17000001|Getter" },    // M1: P1's getter
        { 2, "0x17000001|Setter" },    // M2: P1's setter
        { 3, "0x17000002|Getter" },    // M3: P2's getter
        { 4, "0x17000003|Setter" },    // M4: P3's setter
        { 5, "nil|(0)" },             // M5: P4's FIRST getter row -- dropped
        { 6, "0x17000004|Getter" },    // M6: P4's LAST getter row wins
        { 7, "nil|(0)" },             // M7: P5's getter row before the nil one
        { 8, "0x14000001|Adder" },     // M8: E1's adder
        { 9, "0x14000001|Remover" },   // M9: E1's remover
        { 10, "nil|(0)" },            // Combo: the combined 0x3 row matches no arm
        { 11, "nil|(0)" },            // Raiser: dropped by the default filter
        { 13, "nil|(0)" },            // Ghost: no row at all
        { 12, "nil|(0)" },            // Plain: no row (order pins the search)
        { 0, "nil|(0)" },             // the nil handle
        { 16, "nil|(0)" },             // a past-the-end row
    };
    for (const auto& d : drives) {
        EXPECT_EQ(GsLine(lookup.GetSemantics((0x06u << 24) | d.row)), d.gold)
            << "method row " << d.row;
    }
}

TEST(MethodSemanticsLookupTest, SynthFilterVariantsMatchGold) {
    std::string synthPath = ILSpy::Tests::WriteMslSynthDll();
    TM::MetadataFile file{ synthPath };

    // The Raiser-including filter: the E2 raiser row enters (8 entries).
    TM::MethodSemanticsLookup withRaiser{ file,
        TM::MethodSemanticsLookup::CSharpAccessors
            | TS::MethodSemanticsAttributes::Raiser };
    const auto& entries = withRaiser.Entries();
    ASSERT_EQ(entries.size(), 8u);
    EXPECT_EQ(EntryLine(entries[0]), "sem:Getter row:1 assoc:0x17000001");
    EXPECT_EQ(EntryLine(entries[5]), "sem:Adder row:8 assoc:0x14000001");
    EXPECT_EQ(EntryLine(entries[6]), "sem:Remover row:9 assoc:0x14000001");
    EXPECT_EQ(EntryLine(entries[7]), "sem:Raiser row:11 assoc:0x14000002");

    // A filter including Other is rejected with the exact NotSupportedException
    // message (the C# ctor throw).
    try {
        TM::MethodSemanticsLookup rejected{ file,
            TS::MethodSemanticsAttributes::Getter
                | TS::MethodSemanticsAttributes::Other };
        FAIL() << "the Other filter must throw";
    } catch (const std::logic_error& ex) {
        EXPECT_STREQ(ex.what(),
            "NotSupportedException: SRM doesn't provide access to 'other' accessors");
    }
}

TEST(MethodSemanticsLookupTest, LazyPropertyIsCachedAndCtorIsPublic) {
    std::string synthPath = ILSpy::Tests::WriteMslSynthDll();
    TM::MetadataFile file{ synthPath };

    // The lazy property: two reads return the SAME instance (the gold's
    // lazy-same-instance=True).
    const TM::MethodSemanticsLookup& a = file.GetMethodSemanticsLookup();
    const TM::MethodSemanticsLookup& b = file.GetMethodSemanticsLookup();
    EXPECT_EQ(&a, &b);

    // An independent direct construction over the same file yields the same
    // entry list (the C# internal ctor is public in the port -- the test seam).
    TM::MethodSemanticsLookup direct{ file };
    ASSERT_EQ(direct.Entries().size(), a.Entries().size());
    for (std::size_t i = 0; i < direct.Entries().size(); i++) {
        EXPECT_EQ(EntryLine(direct.Entries()[i]), EntryLine(a.Entries()[i]));
    }
}

// The port-side raw-table enumeration: the 12 MethodSemantics rows of the
// synth, in table order (the probe's BuildSynth call order -- ascending
// association), with the raw flags column VERBATIM (the combined 0x3 row).
TEST(MethodSemanticsLookupTest, MethodSemanticsRowsRaw) {
    std::string synthPath = ILSpy::Tests::WriteMslSynthDll();
    TM::MetadataFile file{ synthPath };
    std::vector<TM::MetadataFile::MethodSemanticsRowInfo> rows
        = file.MethodSemanticsRows();
    ASSERT_EQ(rows.size(), 12u);
    const auto line = [](const TM::MetadataFile::MethodSemanticsRowInfo& r) {
        char b[64];
        std::snprintf(b, sizeof(b), "raw:0x%x method:0x%x assoc:0x%08x",
            r.RawSemantics, r.MethodToken, r.AssociationToken);
        return std::string(b);
    };
    EXPECT_EQ(line(rows[0]), "raw:0x8 method:0x6000008 assoc:0x14000001");   // E1 adder M8
    EXPECT_EQ(line(rows[1]), "raw:0x10 method:0x6000009 assoc:0x14000001");  // E1 remover M9
    EXPECT_EQ(line(rows[2]), "raw:0x2 method:0x6000001 assoc:0x17000001");   // P1 getter M1
    EXPECT_EQ(line(rows[3]), "raw:0x1 method:0x6000002 assoc:0x17000001");   // P1 setter M2
    EXPECT_EQ(line(rows[4]), "raw:0x20 method:0x600000b assoc:0x14000002");  // E2 raiser M11
    EXPECT_EQ(line(rows[5]), "raw:0x2 method:0x6000003 assoc:0x17000002");   // P2 getter M3
    EXPECT_EQ(line(rows[6]), "raw:0x1 method:0x6000004 assoc:0x17000003");   // P3 setter M4
    EXPECT_EQ(line(rows[7]), "raw:0x3 method:0x600000a assoc:0x17000003");   // P3 combined Combo
    EXPECT_EQ(line(rows[8]), "raw:0x2 method:0x6000005 assoc:0x17000004");   // P4 getter M5
    EXPECT_EQ(line(rows[9]), "raw:0x2 method:0x6000006 assoc:0x17000004");   // P4 getter M6
    EXPECT_EQ(line(rows[10]), "raw:0x2 method:0x6000007 assoc:0x17000005");  // P5 getter M7
    EXPECT_EQ(line(rows[11]), "raw:0x2 method:0x0 assoc:0x17000005");      // P5 getter nil
}

// ---------------------------------------------------------------------------
// The whole-corpus pins over the real fixtures.
// ---------------------------------------------------------------------------

struct RealFileGold {
    const char* path;
    std::uint64_t entryDigest;
    std::uint64_t gsDigest;
    std::size_t entryCount;
    std::uint32_t methodCount;
    std::uint32_t gsNilCount;
    std::string censusSem;
    std::string censusAssoc;
    const char* first[4];
    const char* last[4];
};

const RealFileGold kRealGold[] = {
    // mscorlib 4.8 (Framework64): 5986 entries over 29257 methods.
    { "mscorlib", 0x96134AE7D5DCD088ULL, 0x1BEB5A6FA297205BULL, 5986, 29257,
      23271, "Adder=33,Getter=5002,Remover=33,Setter=918", "prop:5920,event:66",
      { "sem:Getter row:1 assoc:0x17000001", "sem:Getter row:279 assoc:0x17000002",
        "sem:Getter row:280 assoc:0x17000003", "sem:Getter row:281 assoc:0x17000004" },
      { "sem:Getter row:29221 assoc:0x17001390", "sem:Getter row:29222 assoc:0x17001391",
        "sem:Getter row:29223 assoc:0x17001392", "sem:Getter row:29241 assoc:0x17001393" } },
    // System.dll 4.8 (the Framework64 copy -- the gold fixture).
    { "System.dll", 0x5D396E149E5DB352ULL, 0xDFED6616A9F4FF64ULL, 5407, 18170,
      12763, "Adder=115,Getter=4066,Remover=115,Setter=1111", "prop:5177,event:230",
      { "sem:Getter row:3 assoc:0x17000001", "sem:Getter row:4 assoc:0x17000002",
        "sem:Getter row:11 assoc:0x17000003", "sem:Getter row:12 assoc:0x17000004" },
      { "sem:Getter row:18153 assoc:0x17000ff6", "sem:Getter row:18154 assoc:0x17000ff7",
        "sem:Getter row:18167 assoc:0x17000ff8", "sem:Getter row:18169 assoc:0x17000ff9" } },
    // .NET 10 CoreLib (re-pinned at 10.0.10: 2 more MethodDef rows and 2
    // more property tail rows than 10.0.8; entry census unchanged).
    { "System.Private.CoreLib.dll", 0x5B305D678A3AAAB1ULL, 0x90F87A723F0E94B2ULL,
      6134, 42373, 36239, "Adder=32,Getter=5577,Remover=32,Setter=493",
      "prop:6070,event:64",
      { "sem:Getter row:1 assoc:0x17000001", "sem:Getter row:2 assoc:0x17000002",
        "sem:Getter row:348 assoc:0x17000003", "sem:Getter row:445 assoc:0x17000004" },
      { "sem:Setter row:42309 assoc:0x170015cb", "sem:Getter row:42310 assoc:0x170015cc",
        "sem:Setter row:42311 assoc:0x170015cc", "sem:Getter row:42338 assoc:0x170015cd" } },
    // The GAC System.Runtime facade: the empty lookup (no properties, no
    // methods -- a pure forwarder facade).
    { "System.Runtime.dll", 0xCBF29CE484222325ULL, 0xCBF29CE484222325ULL, 0, 0,
      0, "", "prop:0,event:0",
      { "", "", "", "" }, { "", "", "", "" } },
    // tiny.netmodule: one method, no properties (all misses).
    { "tiny.netmodule", 0xCBF29CE484222325ULL, 0xB064D4C59415E272ULL, 0, 1,
      1, "", "prop:0,event:0",
      { "", "", "", "" }, { "", "", "", "" } },
};

TEST(MethodSemanticsLookupTest, RealFileCorpusMatchesGold) {
    for (const auto& gold : kRealGold) {
        std::string path;
        if (std::string(gold.path) == "mscorlib")
            path = MscorlibPath();
        else if (std::string(gold.path) == "System.dll")
            path = SystemPath();
        else if (std::string(gold.path) == "System.Private.CoreLib.dll")
            path = CoreLibPath();
        else if (std::string(gold.path) == "System.Runtime.dll")
            path = FacadePath();
        else
            path = WriteTinyNetModule();
        if (!FileAvailable(path.c_str())) {
            if (std::string(gold.path) == "System.Private.CoreLib.dll") {
                // The CoreLib fixture is version-floating (the locator
                // takes the highest installed 10.x); without one the
                // entry cannot run.
                GTEST_SKIP() << "no .NET 10 runtime installed; skipped "
                                "the System.Private.CoreLib gold";
            }
            ADD_FAILURE() << "fixture not available: " << path;
            continue;
        }
        TM::MetadataFile file{ path };
        ASSERT_TRUE(file.IsValid()) << path;
        const TM::MethodSemanticsLookup& lookup = file.GetMethodSemanticsLookup();

        // The entry-list digest (the probe's R|digest).
        Fnv64 fnv;
        for (const auto& e : lookup.Entries())
            fnv.Add(EntryLine(e));
        EXPECT_EQ(fnv.Digest(), gold.entryDigest) << gold.path;
        EXPECT_EQ(lookup.Entries().size(), gold.entryCount) << gold.path;

        // The census (the semantics kinds and the association splits).
        std::map<std::string, int> semCount;
        int propAssoc = 0, eventAssoc = 0;
        for (const auto& e : lookup.Entries()) {
            semCount[SemName(e.Semantics)]++;
            if ((e.AssociationToken >> 24) == 0x17) propAssoc++;
            else if ((e.AssociationToken >> 24) == 0x14) eventAssoc++;
        }
        std::string census;
        for (const auto& kv : semCount) {
            if (!census.empty()) census += ",";
            census += kv.first + "=" + std::to_string(kv.second);
        }
        EXPECT_EQ(census, gold.censusSem) << gold.path;
        EXPECT_EQ("prop:" + std::to_string(propAssoc) + ",event:"
            + std::to_string(eventAssoc), gold.censusAssoc) << gold.path;

        // The first/last entry spot pins.
        for (int i = 0; i < 4; i++) {
            if (lookup.Entries().empty()) break;
            EXPECT_EQ(EntryLine(lookup.Entries()[i]), gold.first[i])
                << gold.path << " first " << i;
            EXPECT_EQ(EntryLine(lookup.Entries()[lookup.Entries().size() - 4 + i]),
                gold.last[i]) << gold.path << " last " << i;
        }

        // The GetSemantics digest over EVERY method row (the probe's sweep).
        const std::uint32_t methodCount =
            file.CorTableRowCount(TM::CorTableIndex::MethodDef);
        EXPECT_EQ(methodCount, gold.methodCount) << gold.path;
        Fnv64 gs;
        std::uint32_t nilCount = 0;
        for (std::uint32_t row = 1; row <= methodCount; row++) {
            auto info = lookup.GetSemantics((0x06u << 24) | row);
            if (info.AssociationToken == 0) nilCount++;
            gs.Add("row:" + std::to_string(row) + " -> " + GsLine(info));
        }
        EXPECT_EQ(gs.Digest(), gold.gsDigest) << gold.path;
        EXPECT_EQ(nilCount, gold.gsNilCount) << gold.path;
    }
}

// The curated named drives (the probe's section C).
TEST(MethodSemanticsLookupTest, CuratedDrivesMatchGold) {
    if (!FileAvailable(MscorlibPath()))
        GTEST_SKIP() << "mscorlib fixture not available";
    {
        TM::MetadataFile mscorlib{ MscorlibPath() };
        const TM::MethodSemanticsLookup& lookup = mscorlib.GetMethodSemanticsLookup();
        // String.get_Chars (row 1224): property row 0x85's getter.
        EXPECT_EQ(GsLine(lookup.GetSemantics(0x06000000u | 1224)),
            "0x17000085|Getter");
        // The property-1 row's getter is method row 1.
        EXPECT_EQ(GsLine(lookup.GetSemantics(0x06000001u)), "0x17000001|Getter");
        // The five String.Equals overloads (rows 1217-1221) and Copy (row
        // 1359) are no accessor.
        for (std::uint32_t row = 1217; row <= 1221; row++)
            EXPECT_EQ(GsLine(lookup.GetSemantics(0x06000000u | row)), "nil|(0)")
                << row;
        EXPECT_EQ(GsLine(lookup.GetSemantics(0x06000000u | 1359)), "nil|(0)");
        // The nil handle and a past-the-end row.
        EXPECT_EQ(GsLine(lookup.GetSemantics(0)), "nil|(0)");
        EXPECT_EQ(GsLine(lookup.GetSemantics(0x06000000u | 29262)), "nil|(0)");
    }
    {
        TM::MetadataFile system{ SystemPath() };
        const TM::MethodSemanticsLookup& lookup = system.GetMethodSemanticsLookup();
        // System.dll's event row 1: its adder (row 460) and remover (row 461).
        EXPECT_EQ(GsLine(lookup.GetSemantics(0x06000000u | 460)), "0x14000001|Adder");
        EXPECT_EQ(GsLine(lookup.GetSemantics(0x06000000u | 461)), "0x14000001|Remover");
    }
}

}  // namespace
