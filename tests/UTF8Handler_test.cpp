#define NO_UT_MAIN

#include "UnitTestFramework.h"
#include "UTF8Handler.h"

#include <initializer_list>
#include <vector>
#include <random>
#include <algorithm>
#include <iostream>

using S = UTF8Handler::DecodeStatus;
using R = UTF8Handler::DecodeResult;

static R lazyD(const std::initializer_list<uint8_t>& bytes, std::size_t avail) {
    std::vector<uint8_t> v(bytes);
    return UTF8Handler::decode(v.data(), avail);
}

TEST_CASE(UTF8Handler_ASCIISingleByte) {
    auto r = lazyD({0x41}, 1);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x41u);
    REQUIRE_EQ(r.width, 1);

    r = lazyD({0x7F}, 1);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x7Fu);
    REQUIRE_EQ(r.width, 1);
}

TEST_CASE(UTF8Handler_ContinuationAsFirstByte) {
    uint8_t byte=0x80; // 10xxxxxx cannot start
    auto r = UTF8Handler::decode(&byte, 1);
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, 1);
}

TEST_CASE(UTF8Handler_TwoByte_Valid) {
    // U+0080 = 0xC2 0x80
    auto r = lazyD({0xC2, 0x80}, 2);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x80u);
    REQUIRE_EQ(r.width, 2);

    // U+00A9 © = C2 A9
    r = lazyD({0xC2, 0xA9}, 2);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0xA9u);
    REQUIRE_EQ(r.width, 2);

    // Upper 2-byte boundary: U+07FF = DF BF
    r = lazyD({0xDF, 0xBF}, 2);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x07FFu);
    REQUIRE_EQ(r.width, 2);
}

TEST_CASE(UTF8Handler_TwoByte_TruncatedAndBadCont) {
    auto r = lazyD({0xC2}, 1);
    REQUIRE_EQ(r.status, S::NeedMore);
    REQUIRE_EQ(r.width, 2);

    r = lazyD({0xC2, 0x00}, 2); // second byte not continuation
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, 1);
}

TEST_CASE(UTF8Handler_ThreeByte_Valid) {
    // U+0800 = E0 A0 80 (3-byte minimum)
    auto r = lazyD({0xE0, 0xA0, 0x80}, 3);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x0800u);
    REQUIRE_EQ(r.width, 3);

    // U+20AC € = E2 82 AC
    r = lazyD({0xE2, 0x82, 0xAC}, 3);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x20ACu);
    REQUIRE_EQ(r.width, 3);

    // Upper 3-byte boundary: U+FFFF = EF BF BF (non-surrogate)
    r = lazyD({0xEF, 0xBF, 0xBF}, 3);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0xFFFFu);
    REQUIRE_EQ(r.width, 3);
}

TEST_CASE(UTF8Handler_ThreeByte_TruncatedAndOverlongAndSurrogate) {
    // Truncations
    auto r = lazyD({0xE2}, 1);
    REQUIRE_EQ(r.status, S::NeedMore);
    REQUIRE_EQ(r.width, 3);

    r = lazyD({0xE2, 0x82}, 2);
    REQUIRE_EQ(r.status, S::NeedMore);
    REQUIRE_EQ(r.width, 3);

    // Overlong 3-byte (would encode < 0x800): E0 80 80
    r = lazyD({0xE0, 0x80, 0x80}, 3);
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, 1);

    // Bad continuation in middle
    r = lazyD({0xE2, 0x28, 0xA1}, 3); // 0x28 not 10xxxxxx
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, 1);

    // Surrogate range (U+D800): ED A0 80
    r = lazyD({0xED, 0xA0, 0x80}, 3);
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, 1);
}

TEST_CASE(UTF8Handler_FourByte_Valid) {
    // U+10000 = F0 90 80 80 (4-byte minimum)
    auto r = lazyD({0xF0, 0x90, 0x80, 0x80}, 4);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x10000u);
    REQUIRE_EQ(r.width, 4);

    // U+1F600 😀 = F0 9F 98 80
    r = lazyD({0xF0, 0x9F, 0x98, 0x80}, 4);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x1F600u);
    REQUIRE_EQ(r.width, 4);

    // Upper valid limit U+10FFFF = F4 8F BF BF
    r = lazyD({0xF4, 0x8F, 0xBF, 0xBF}, 4);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x10FFFFu);
    REQUIRE_EQ(r.width, 4);
}

TEST_CASE(UTF8Handler_FourByte_TruncatedAndRange) {
    // Truncations
    auto r = lazyD({0xF0}, 1);
    REQUIRE_EQ(r.status, S::NeedMore);
    REQUIRE_EQ(r.width, 4);

    r = lazyD({0xF0, 0x9F}, 2);
    REQUIRE_EQ(r.status, S::NeedMore);
    REQUIRE_EQ(r.width, 4);

    r = lazyD({0xF0, 0x9F, 0x98}, 3);
    REQUIRE_EQ(r.status, S::NeedMore);
    REQUIRE_EQ(r.width, 4);

    // > U+10FFFF (invalid) e.g., F4 90 80 80
    r = lazyD({0xF4, 0x90, 0x80, 0x80}, 4);
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, 1);
}

TEST_CASE(UTF8Handler_InvalidFirstByteHigh) {
    // 0xF5..0xFF cannot start a valid sequence
    auto r = lazyD({0xF5}, 1);
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, 1);
}

TEST_CASE(UTF8Handler_AvailZero) {
    auto r = lazyD({0x41}, 0);
    REQUIRE_EQ(r.status, S::NeedMore);
    REQUIRE_EQ(r.width, 1);
}

TEST_CASE(UTF8Handler_OffsetOverload) {
    const std::vector<uint8_t> buf = {0x41, 0xC2, 0xA9}; // 'A' + '©'
    // offset==0 => 'A'
    auto r = UTF8Handler::decode(buf.data(), buf.size(), 0);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x41u);
    REQUIRE_EQ(r.width, 1);

    // offset==1 => '©'
    r = UTF8Handler::decode(buf.data(), buf.size(), 1);
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, 0x00A9u);
    REQUIRE_EQ(r.width, 2);

    // offset==2 => continuation byte -> Invalid, width=1
    r = UTF8Handler::decode(buf.data(), buf.size(), 2);
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, 1);

    // offset==size => NeedMore, width=1 (as per your overload)
    r = UTF8Handler::decode(buf.data(), buf.size(), buf.size());
    REQUIRE_EQ(r.status, S::NeedMore);
    REQUIRE_EQ(r.width, 1);
}

TEST_CASE(UTF8Handler_WidthMatchesSequenceLengthOnOk) {
    // Spot-check widths for each class
    auto r1 = lazyD({0x24}, 1);                        // '$'
    auto r2 = lazyD({0xC2, 0xA3}, 2);                  // U+00A3 '£'
    auto r3 = lazyD({0xE2, 0x98, 0x83}, 3);            // U+2603 '☃'
    auto r4 = lazyD({0xF0, 0x9F, 0x92, 0xA9}, 4);      // U+1F4A9

    REQUIRE_EQ(r1.status, S::Ok); REQUIRE_EQ(r1.width, 1);
    REQUIRE_EQ(r2.status, S::Ok); REQUIRE_EQ(r2.width, 2);
    REQUIRE_EQ(r3.status, S::Ok); REQUIRE_EQ(r3.width, 3);
    REQUIRE_EQ(r4.status, S::Ok); REQUIRE_EQ(r4.width, 4);
}

//--advanced tests--//
static std::vector<uint8_t> refEncode(uint32_t cp) {
    uint8_t buf[4];
    auto result = UTF8Handler::encode(cp, buf, 4); // to ensure consistency
    if (result.status == UTF8Handler::EncodeStatus::Ok)
        return std::vector<uint8_t>(buf, buf + result.width);

    return {};
}

static void expectOkRoundtrip(uint32_t cp) {
    auto bytes = refEncode(cp);
    if (bytes.empty()) 
        return; // invalid scalar -> skip
    auto r = UTF8Handler::decode(bytes.data(), bytes.size());
    REQUIRE_EQ(r.status, S::Ok);
    REQUIRE_EQ(r.cp, cp);
    REQUIRE_EQ(r.width, bytes.size());
}

static void expectNeedmoreOnTruncations(uint32_t cp) {
    auto bytes = refEncode(cp);
    if (bytes.size() <= 1) 
        return; // only for multibyte
    const auto need = bytes.size();
    for (size_t cut = 1; cut < need; ++cut) {
        auto r = UTF8Handler::decode(bytes.data(), cut);
        REQUIRE_EQ(r.status, S::NeedMore);
        REQUIRE_EQ(r.width, need);
    }
}

static void expectInvalidOnContMutations(uint32_t cp) {
    auto bytes = refEncode(cp);
    if (bytes.size() <= 1) 
        return;
    for (size_t i = 1; i < bytes.size(); ++i) {
        auto mutated = bytes;
        mutated[i] &= 0x7F; // force 0xxxxxxx
        auto r = UTF8Handler::decode(mutated.data(), mutated.size());
        REQUIRE_EQ(r.status, S::Invalid);
        REQUIRE_EQ(r.width, 1);
        mutated[i] |= 0xC0; // 11xxxxxx
        r = UTF8Handler::decode(mutated.data(), mutated.size());
        REQUIRE_EQ(r.status, S::Invalid);
        REQUIRE_EQ(r.width, 1);
    }
}

static std::vector<uint8_t> overlong2(uint32_t cp) { // cp < 0x80
    return {
        static_cast<uint8_t>(0xC0 | (cp >> 6)),
        static_cast<uint8_t>(0x80 | (cp & 0x3F))
    };
}
static std::vector<uint8_t> overlong3(uint32_t cp) { // cp < 0x800
    return {
        static_cast<uint8_t>(0xE0 | (cp >> 12)),
        static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)),
        static_cast<uint8_t>(0x80 | (cp & 0x3F))
    };
}
static std::vector<uint8_t> overlong4(uint32_t cp) { // cp < 0x10000
    return {
        static_cast<uint8_t>(0xF0 | (cp >> 18)),
        static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F)),
        static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)),
        static_cast<uint8_t>(0x80 | (cp & 0x3F))
    };
}

static void expectInvalidWidth(const std::vector<uint8_t>& v, uint8_t expectedWidth=1) {
    auto r = UTF8Handler::decode(v.data(), v.size());
    REQUIRE_EQ(r.status, S::Invalid);
    REQUIRE_EQ(r.width, expectedWidth);
}


// Exhaustive 1-byte (0..0x7F) and 2-byte (0x80..0x7FF)
TEST_CASE(UTF8Handler_RoundTrip_Exhaustive_1_And_2_Byte) {
    for (uint32_t cp = 0x00; cp <= 0x7F; ++cp) 
        expectOkRoundtrip(cp);
    for (uint32_t cp = 0x80; cp <= 0x7FF; ++cp) 
        expectOkRoundtrip(cp);
}

// Dense sample across 3-byte non-surrogate planes
TEST_CASE(UTF8Handler_RoundTrip_Dense_3Bytes) {
    // [0x800..0xD7FF] (exclude surrogates)
    for (uint32_t cp = 0x800; cp <= 0xD7FF; cp += 0x31) 
        expectOkRoundtrip(cp);
    // [0xE000..0xFFFF]
    for (uint32_t cp = 0xE000; cp <= 0xFFFF; cp += 0x31) 
        expectOkRoundtrip(cp);
}

// Dense sample across 4-byte plane
TEST_CASE(UTF8Handler_RoundTrip_Dense_4Bytes) {
    for (uint32_t cp = 0x10000; cp <= 0x10FFFF; cp += 0x111) 
        expectOkRoundtrip(cp);
}

TEST_CASE(UTF8Handler_Truncation_All_2Bytes) {
    for (uint32_t cp = 0x80; cp <= 0x7FF; ++cp) 
        expectNeedmoreOnTruncations(cp);
}

TEST_CASE(UTF8Handler_Truncation_Sample_3Bytes_And_4Bytes) {
    for (uint32_t cp = 0x800; cp <= 0xD7FF; cp += 0x77) 
        expectNeedmoreOnTruncations(cp);
    for (uint32_t cp = 0xE000; cp <= 0xFFFF; cp += 0x77) 
        expectNeedmoreOnTruncations(cp);
    for (uint32_t cp = 0x10000; cp <= 0x10FFFF; cp += 0x3FF) 
        expectNeedmoreOnTruncations(cp);
}

TEST_CASE(UTF8Handler_Mutation_Samples_All_Classes) {
    for (uint32_t cp = 0x80; cp <= 0x7FF; cp += 17) 
        expectInvalidOnContMutations(cp);
    for (uint32_t cp = 0x800; cp <= 0xD7FF; cp += 257) 
        expectInvalidOnContMutations(cp);
    for (uint32_t cp = 0xE000; cp <= 0xFFFF; cp += 257) 
        expectInvalidOnContMutations(cp);
    for (uint32_t cp = 0x10000; cp <= 0x10FFFF; cp += 0x1FFF) 
        expectInvalidOnContMutations(cp);
}

TEST_CASE(UTF8Handler_Overlong_Rejects_2Bytes_For_ASCII) {
    for (uint32_t cp = 0; cp <= 0x7F; cp += 7) {
        expectInvalidWidth(overlong2(cp));
    }
}

TEST_CASE(UTF8Handler_Overlong_Rejects_3Byte_For_Sub800) {
    for (uint32_t cp = 0; cp < 0x800; cp += 19) {
        expectInvalidWidth(overlong3(cp));
    }
}

TEST_CASE(UTF8Handler_Overlong_Rejects_4Byte_For_Sub10000) {
    for (uint32_t cp = 0; cp < 0x10000; cp += 257) {
        expectInvalidWidth(overlong4(cp));
    }
}

TEST_CASE(UTF8Handler_Overlong_Specific_Edges) {
    // 3-byte “E0 9F BF” encodes < 0x800 => invalid
    expectInvalidWidth({0xE0, 0x9F, 0xBF});
    // 4-byte “F0 8F BF BF” encodes < 0x10000 => invalid
    expectInvalidWidth({0xF0, 0x8F, 0xBF, 0xBF});
}

TEST_CASE(UTF8Handler_FirstByte_Classes) {
    for (int b = 0x00; b <= 0x7F; ++b) {
        uint8_t x = static_cast<uint8_t>(b);
        auto r = UTF8Handler::decode(&x, 1);
        REQUIRE_EQ(r.status, S::Ok);
        REQUIRE_EQ(r.width, 1);
    }
    for (int b = 0x80; b <= 0xBF; ++b) {
        uint8_t x = static_cast<uint8_t>(b);
        auto r = UTF8Handler::decode(&x, 1);
        REQUIRE_EQ(r.status, S::Invalid);
        REQUIRE_EQ(r.width, 1);
    }
    for (int b = 0xC0; b <= 0xC1; ++b) {
        uint8_t x = static_cast<uint8_t>(b);
        auto r = UTF8Handler::decode(&x, 1);
        REQUIRE_EQ(r.status, S::Invalid);
        REQUIRE_EQ(r.width, 1);
    }
    for (int b = 0xC2; b <= 0xDF; ++b) {
        uint8_t x = static_cast<uint8_t>(b);
        auto r = UTF8Handler::decode(&x, 1);
        REQUIRE_EQ(r.status, S::NeedMore);
        REQUIRE_EQ(r.width, 2);
    }
    for (int b = 0xE0; b <= 0xEF; ++b) {
        uint8_t x = static_cast<uint8_t>(b);
        auto r = UTF8Handler::decode(&x, 1);
        REQUIRE_EQ(r.status, S::NeedMore);
        REQUIRE_EQ(r.width, 3);
    }
    for (int b = 0xF0; b <= 0xF4; ++b) {
        uint8_t x = static_cast<uint8_t>(b);
        auto r = UTF8Handler::decode(&x, 1);
        REQUIRE_EQ(r.status, S::NeedMore);
        REQUIRE_EQ(r.width, 4);
    }
    for (int b = 0xF5; b <= 0xFF; ++b) {
        uint8_t x = static_cast<uint8_t>(b);
        auto r = UTF8Handler::decode(&x, 1);
        REQUIRE_EQ(r.status, S::Invalid);
        REQUIRE_EQ(r.width, 1);
    }
}

TEST_CASE(UTF8Handler_Surrogates_Edges) {
    // U+D7FF (valid) -> ED 9F BF
    auto r = lazyD({0xED, 0x9F, 0xBF}, 3);
    REQUIRE_EQ(r.status, S::Ok); 
    REQUIRE_EQ(r.cp, 0xD7FFu); 
    REQUIRE_EQ(r.width, 3);

    // U+D800 (invalid) -> ED A0 80
    r = lazyD({0xED, 0xA0, 0x80}, 3);
    REQUIRE_EQ(r.status, S::Invalid); 
    REQUIRE_EQ(r.width, 1);

    // U+DFFF (invalid) -> ED BF BF
    r = lazyD({0xED, 0xBF, 0xBF}, 3);
    REQUIRE_EQ(r.status, S::Invalid); 
    REQUIRE_EQ(r.width, 1);

    // U+E000 (valid) -> EE 80 80
    r = lazyD({0xEE, 0x80, 0x80}, 3);
    REQUIRE_EQ(r.status, S::Ok); 
    REQUIRE_EQ(r.cp, 0xE000u); 
    REQUIRE_EQ(r.width, 3);
}

TEST_CASE(UTF8Handler_Scanner_Mixed_Stream_Progress_And_Correctness) {
    std::vector<uint32_t> cps = {
        0x24, 0x7F, 0x80, 0xA9, 0x7FF, 0x800, 0x20AC, 0xD7FF,
        0xE000, 0xFFFF, 0x10000, 0x1F600, 0x10FFFF
    };
    // Build buffer with some invalid noise between valid scalars
    std::vector<uint8_t> buf;
    std::vector<uint32_t> expected;
    for (auto cp : cps) {
        auto enc = refEncode(cp);
        expected.push_back(cp);
        buf.insert(buf.end(), enc.begin(), enc.end());
        // inject a bad byte (continuation as starter)
        buf.push_back(0x80);
    }

    size_t i = 0;
    std::vector<uint32_t> seen;
    while (i < buf.size()) {
        auto r = UTF8Handler::decode(&buf[i], buf.size() - i);
        REQUIRE(r.width >= 1u);
        if (r.status == S::Ok) seen.push_back(r.cp);
        i += r.width;
    }

    // expect that decoded exactly the valid cps that are inserted (ignoring bad bytes)
    REQUIRE_EQ(seen, expected);
}

//--- resync on invalid ---//
static const std::vector<uint8_t> kInvalidStarters = []{
    std::vector<uint8_t> v;
    // 0x80..0xBF (continuations as starters)
    for (int b = 0x80; b <= 0xBF; ++b) 
        v.push_back(static_cast<uint8_t>(b));
    // 0xC0..0xC1 (overlong 2-byte starters)
    v.push_back(0xC0); 
    v.push_back(0xC1);
    // 0xF5..0xFF (above Unicode max)
    for (int b = 0xF5; b <= 0xFF; ++b) 
        v.push_back(static_cast<uint8_t>(b));
    return v;
}();

static void scanStream(const std::vector<uint8_t>& buf,
                       std::vector<uint32_t>& seen,
                       size_t& ok_count,
                       size_t& invalid_count,
                       size_t& needmore_count)
{
    size_t i = 0;
    ok_count = invalid_count = needmore_count = 0;
    while (i < buf.size()) {
        auto r = UTF8Handler::decode(&buf[i], buf.size() - i);
        REQUIRE(r.width >= 1u);
        switch (r.status) {
            case S::Ok:
                ++ok_count;
                seen.push_back(r.cp); 
                break;
            case S::Invalid:
                ++invalid_count;
                break;
            case S::NeedMore:
                ++needmore_count;
                break;
        }
        i += r.width;
    }
}

TEST_CASE(UTF8Handler_Resync_LongGarbageOnly_MakesLinearProgress_NoNeedMore) {
    // 4096 bytes of invalid starters only
    std::vector<uint8_t> buf(4096);
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = kInvalidStarters[i % kInvalidStarters.size()];
    }
    std::vector<uint32_t> seen;
    size_t ok = 0, inv = 0, more = 0;
    scanStream(buf, seen, ok, inv, more);

    REQUIRE_EQ(ok, 0u);
    REQUIRE_EQ(more, 0u);         // No NeedMore for these starters
    REQUIRE_EQ(inv, buf.size());  // One invalid per byte, width == 1 each
    REQUIRE(seen.empty());
}

TEST_CASE(UTF8Handler_Resync_RunsOfInvalid_ThenOneValid_AlwaysRecovers) {
    const uint32_t cp = 0x1F600; // 😀
    const auto enc = refEncode(cp);
    REQUIRE(enc.empty() == false);

    const std::vector<uint8_t> samples = {0x80, 0xBF, 0xC0, 0xC1, 0xF5, 0xFF};
    const std::vector<size_t> lengths = {1, 2, 7, 31, 257};

    for (auto bad : samples) {
        for (auto len : lengths) {
            std::vector<uint8_t> buf(len, bad);
            buf.insert(buf.end(), enc.begin(), enc.end());

            std::vector<uint32_t> seen;
            size_t ok = 0, inv = 0, more = 0;
            scanStream(buf, seen, ok, inv, more);

            REQUIRE_EQ(more, 0u);
            REQUIRE_EQ(inv, len);
            REQUIRE_EQ(ok, 1u);
            REQUIRE_EQ(seen.size(), 1u);
            REQUIRE_EQ(seen[0], cp);
        }
    }
}

TEST_CASE(UTF8Handler_Resync_GarbageBetweenBytesOfAValidSequence_DoesNotGlue) {
    // Interleave junk inside what would have been a 4-byte scalar
    // Then append the correct 4-byte scalar; we must decode exactly once.
    const uint32_t cp = 0x1F600; // 😀 F0 9F 98 80
    auto enc = refEncode(cp);
    REQUIRE_EQ(enc.size(), 4u);

    std::vector<uint8_t> buf;
    buf.push_back(enc[0]);
    buf.push_back(0xFF);
    buf.push_back(enc[1]);
    buf.push_back(0x80);
    buf.push_back(enc[2]);
    buf.push_back(0xFF);
    buf.push_back(enc[3]);              // this alone is still invalid as starter (0x80)
    // Now append full correct scalar
    auto good = refEncode(cp);
    buf.insert(buf.end(), good.begin(), good.end());

    std::vector<uint32_t> seen;
    size_t ok = 0, inv = 0, more = 0;
    scanStream(buf, seen, ok, inv, more);

    REQUIRE(inv >= 1u);
    REQUIRE_EQ(more, 0u);
    REQUIRE_EQ(ok, 1u);
    REQUIRE_EQ(seen.size(), 1u);
    REQUIRE_EQ(seen[0], cp);
}

TEST_CASE(UTF8Handler_Resync_RandomInvalidBlocksBetweenValids_StableRecovery) {
    std::mt19937 rng(0xC0FFEE);
    std::uniform_int_distribution<int> pick_bad(0, (int)kInvalidStarters.size() - 1);
    std::uniform_int_distribution<int> bad_len(0, 50);

    // Build: [CP][garbage][CP][garbage]... with 200 valids
    std::vector<uint32_t> expected;
    std::vector<uint8_t> buf;
    auto addCP = [&](uint32_t cp) {
        auto e = refEncode(cp);
        if (e.empty()) return; // skip invalid scalar (shouldn't happen with chosen cps)
        expected.push_back(cp);
        buf.insert(buf.end(), e.begin(), e.end());
    };

    // Cycle across ranges (ASCII, 2-byte, 3-byte, 4-byte)
    const std::vector<uint32_t> seeds = {
        0x24, 0x7A, 0x7F,       // ASCII
        0x80, 0xA9, 0x7FF,      // 2-byte
        0x800, 0x20AC, 0xD7FF, 0xE000, 0xFFFF, // 3-byte (avoid surrogates)
        0x10000, 0x1F600, 0x10FFFF // 4-byte
    };

    for (int i = 0; i < 200; ++i) {
        addCP(seeds[i % seeds.size()]);
        int nbad = bad_len(rng);
        for (int k = 0; k < nbad; ++k) {
            buf.push_back(kInvalidStarters[pick_bad(rng)]);
        }
    }

    std::vector<uint32_t> seen;
    size_t ok = 0, inv = 0, more = 0;
    scanStream(buf, seen, ok, inv, more);

    REQUIRE_EQ(more, 0u);               // invalid starters never request more
    REQUIRE_EQ(ok, expected.size());
    REQUIRE_EQ(seen.size(), expected.size());
    REQUIRE_EQ(seen, expected);         // exact recovery of the valid sequence
}

TEST_CASE(UTF8Handler_Resync_BadStarterFollowedByGoodSequence_ResyncsOnce) {
    // 0x80 (invalid starter), then © (C2 A9)
    std::vector<uint8_t> buf = { 0x80, 0xC2, 0xA9 };
    std::vector<uint32_t> seen;
    size_t ok = 0, inv = 0, more = 0;
    scanStream(buf, seen, ok, inv, more);

    REQUIRE_EQ(inv, 1u);
    REQUIRE_EQ(more, 0u);
    REQUIRE_EQ(ok, 1u);
    REQUIRE_EQ(seen.size(), 1u);
    REQUIRE_EQ(seen[0], 0x00A9u);
}

TEST_CASE(UTF8Handler_Resync_MixedNoiseFlood_StillDecodesAnchors) {
    // Garbage flood with periodic anchors (valid codepoints) sprinkled in.
    std::vector<uint8_t> buf;
    std::vector<uint32_t> anchors = {0x24, 0xA9, 0x20AC, 0x1F600, 0x10FFFF};
    std::vector<uint32_t> expected;

    // 50 cycles of: 100 junk bytes + anchor
    for (int cycle = 0; cycle < 50; ++cycle) {
        for (int i = 0; i < 100; ++i) 
            buf.push_back(kInvalidStarters[(cycle + i) % kInvalidStarters.size()]);
        uint32_t cp = anchors[cycle % anchors.size()];
        auto enc = refEncode(cp);
        expected.push_back(cp);
        buf.insert(buf.end(), enc.begin(), enc.end());
    }

    std::vector<uint32_t> seen;
    size_t ok = 0, inv = 0, more = 0;
    scanStream(buf, seen, ok, inv, more);

    REQUIRE_EQ(more, 0u);
    REQUIRE_EQ(ok, expected.size());
    REQUIRE_EQ(seen, expected);
}

//--- Exhaustive correctness ---//
TEST_CASE(UTF8Handler_Exhaustive_RoundTrip_AllValidScalars) {
    uint8_t buf[4];

    for (uint32_t cp = 0; cp <= 0x10FFFF; ++cp) {
        // Skip UTF-16 surrogate code points explicitly.
        if (cp >= 0xD800 && cp <= 0xDFFF) 
            continue;

        auto enc = UTF8Handler::encode(cp, buf, 4); // to ensure consistency with reference
        const int len = enc.width;
        REQUIRE_EQ(enc.status , UTF8Handler::EncodeStatus::Ok);

        auto r = UTF8Handler::decode(buf, static_cast<std::size_t>(len));
        if (r.status != S::Ok || 
            r.cp != cp || 
            r.width != static_cast<uint8_t>(len)) {
            std::cout << "Round-trip failed for cp=0x" << std::hex << cp
                          << " status=" << static_cast<int>(r.status)
                          << " got_cp=0x" << std::hex << r.cp
                          << " width=" << std::dec << int(r.width)
                          << " expected_width=" << len;
            // Fail-fast to avoid flooding logs:
            REQUIRE(false);
            break;
        }
    }
}

TEST_CASE(UTF8Handler_Exhaustive_Truncations_NeedMore_WithCorrectWidth) {
    uint8_t buf[4];

    for (uint32_t cp = 0; cp <= 0x10FFFF; ++cp) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;

        auto enc = UTF8Handler::encode(cp, buf, 4); // to ensure consistency with reference
        const int len = enc.width;
        REQUIRE_EQ(enc.status , UTF8Handler::EncodeStatus::Ok);

        if (len == 1) 
            continue; // No truncation case for ASCII

        // Cut at every position before full length and expect NeedMore with width==len.
        for (int cut = 1; cut < len; ++cut) {
            auto r = UTF8Handler::decode(buf, static_cast<std::size_t>(cut));
            if (r.status != S::NeedMore || r.width != static_cast<uint8_t>(len)) {
                std::cout << "Truncation failed for cp=0x" << std::hex << cp
                              << " len=" << std::dec << len
                              << " cut=" << cut
                              << " status=" << static_cast<int>(r.status)
                              << " width=" << int(r.width);
                REQUIRE(false);
                goto done;
            }
        }
    }
done:
    (void)0;
}

//--- garbage blocks ---//
// Bytes that are *never* valid starters for a UTF-8 scalar
// - 0x80..0xBF: continuation bytes
// - 0xC0..0xC1: overlong 2-byte starters (forbidden)
// - 0xF5..0xFF: above Unicode max (U+10FFFF)
static std::vector<uint8_t> invalidStarters() {
    std::vector<uint8_t> v;
    for (int b = 0x80; b <= 0xBF; ++b) 
        v.push_back(static_cast<uint8_t>(b));
    v.push_back(0xC0); 
    v.push_back(0xC1);
    for (int b = 0xF5; b <= 0xFF; ++b) 
        v.push_back(static_cast<uint8_t>(b));
    return v;
}

static void scanAndAssertAllInvalid(const std::vector<uint8_t>& buf) {
    size_t i = 0;
    size_t invalid_count = 0, needmore_count = 0, ok_count = 0;
    while (i < buf.size()) {
        auto r = UTF8Handler::decode(&buf[i], buf.size() - i);
        REQUIRE(r.width >= 1u);
        if (r.status == S::Invalid) {
            ++invalid_count;
            REQUIRE_EQ(r.width, 1u);
        } else if (r.status == S::NeedMore) {
            ++needmore_count;
        } else if (r.status == S::Ok) {
            ++ok_count;
        }
        i += r.width;
    }

    // For a buffer made purely of invalid starters:
    REQUIRE_EQ(ok_count, 0u);
    REQUIRE_EQ(needmore_count, 0u);        // never request more
    REQUIRE_EQ(invalid_count, buf.size()); // one invalid per byte (width == 1)
}

TEST_CASE(UTF8Handler_GarbageBlock_ContinuationBytesOnly_LongRun) {
    std::vector<uint8_t> buf;
    for (int i = 0; i < 8192; ++i) 
        buf.push_back(static_cast<uint8_t>(0x80 + (i % 64))); // 0x80..0xBF
    scanAndAssertAllInvalid(buf);
}

TEST_CASE(UTF8Handler_GarbageBlock_OverlongStartersOnly_LongRun) {
    std::vector<uint8_t> buf(4096);
    for (size_t i = 0; i < buf.size(); ++i) 
        buf[i] = (i & 1) ? 0xC0 : 0xC1; // alternate C0/C1
    scanAndAssertAllInvalid(buf);
}

TEST_CASE(UTF8Handler_GarbageBlock_AboveMaxOnly_LongRun) {
    std::vector<uint8_t> buf;
    for (int i = 0; i < 4096; ++i) 
        buf.push_back(static_cast<uint8_t>(0xF5 + (i % (0xFF - 0xF5 + 1))));
    scanAndAssertAllInvalid(buf);
}

TEST_CASE(UTF8Handler_GarbageBlock_MixedInvalidStarters_Shuffled_LongRun) {
    auto pool = invalidStarters();
    std::vector<uint8_t> buf;
    buf.reserve(16384);
    // Repeat pool to fill ~16KB
    while (buf.size() + pool.size() <= 16384) 
        buf.insert(buf.end(), pool.begin(), pool.end());

    // Shuffle deterministically (no <random> dep)
    // Simple LCG for reproducible permutation
    auto lcg = [](uint32_t& s){ s = s*1664525u + 1013904223u; return s; };
    uint32_t seed = 0xDEADBEEF;
    for (size_t i = buf.size(); i > 1; --i) {
        size_t j = lcg(seed) % i;
        std::swap(buf[i-1], buf[j]);
    }

    scanAndAssertAllInvalid(buf);
}

// Sanity: very short garbage blocks of different categories
TEST_CASE(UTF8Handler_GarbageBlock_TinyBlocks) {
    scanAndAssertAllInvalid(std::vector<uint8_t>{0x80});
    scanAndAssertAllInvalid(std::vector<uint8_t>{0xC0});
    scanAndAssertAllInvalid(std::vector<uint8_t>{0xF5});
    scanAndAssertAllInvalid(std::vector<uint8_t>{0xBF, 0x80, 0xBF});
}

//--- fuzz tests ---//
// Classify first byte quickly: 0 invalid, 1..4 = expected length
static inline int classifyFirst(uint8_t b) noexcept {
    if (b <= 0x7F) return 1;
    if (b >= 0x80 && b <= 0xBF) return 0;       // continuation can't start
    if (b == 0xC0 || b == 0xC1) return 0;       // overlong starters
    if (b >= 0xC2 && b <= 0xDF) return 2;
    if (b >= 0xE0 && b <= 0xEF) return 3;
    if (b >= 0xF0 && b <= 0xF4) return 4;
    return 0; // 0xF5..0xFF invalid
}

// Canonical OK-checker: given a byte span and a claimed width 1..4,
// verify it is a *well-formed* minimal UTF-8, and return its codepoint.
// Returns 0xFFFFFFFF on invalid.
static uint32_t validateOk(const uint8_t* p, int width) noexcept {
    auto isCont = [](uint8_t b){ 
        return (b & 0xC0) == 0x80; 
    };

    if (width == 1) {
        return (p[0] <= 0x7F) ? p[0] : 0xFFFFFFFFu;
    }
    if (width == 2) {
        uint8_t b0 = p[0], b1 = p[1];
        if (b0 < 0xC2 || b0 > 0xDF || !isCont(b1)) 
            return 0xFFFFFFFFu;
        uint32_t cp = ((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
        if (cp < 0x80u) 
            return 0xFFFFFFFFu; // overlong guard
        return cp;
    }
    if (width == 3) {
        uint8_t b0 = p[0], b1 = p[1], b2 = p[2];
        if (b0 < 0xE0 || b0 > 0xEF || !isCont(b1) || !isCont(b2)) 
            return 0xFFFFFFFFu;
        uint32_t cp = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
        if (cp < 0x800u) 
            return 0xFFFFFFFFu; // overlong
        if ((cp & 0xFFFFF800u) == 0xD800u) 
            return 0xFFFFFFFFu; // surrogate range
        return cp;
    }
    if (width == 4) {
        uint8_t b0 = p[0], b1 = p[1], b2 = p[2], b3 = p[3];
        if (b0 < 0xF0 || b0 > 0xF4 || !isCont(b1) || !isCont(b2) || !isCont(b3))
            return 0xFFFFFFFFu;
        uint32_t cp = ((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) |
                      ((b2 & 0x3Fu) << 6)  | (b3 & 0x3Fu);
        if (cp < 0x10000u || cp > 0x10FFFFu) 
            return 0xFFFFFFFFu;
        return cp;
    }
    return 0xFFFFFFFFu;
}

TEST_CASE(UTF8Handler_Fuzz_Regression_D4_01_BF_40_FB) {
    const uint8_t buf[5] = {0xD4, 0x01, 0xBF, 0x40, 0xFB};
    auto r = UTF8Handler::decode(buf, 5);
    REQUIRE_EQ(r.status, UTF8Handler::DecodeStatus::Invalid);
    REQUIRE_EQ(r.width, 1u);
}

// Random full-buffer fuzz. Ensures invariants and canonical re-encode on Ok.
TEST_CASE(UTF8Handler_Fuzz_RandomFullBuffer) {
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<int> lenDist(1, 6);
    std::uniform_int_distribution<int> byteDist(0, 255);

    for (int iter = 0; iter < 50000; ++iter) {
        const int n = lenDist(rng);
        uint8_t buf[8];
        for (int i = 0; i < n; ++i) 
            buf[i] = static_cast<uint8_t>(byteDist(rng));

        auto r = UTF8Handler::decode(buf, static_cast<std::size_t>(n));
        REQUIRE(r.width >= 1u);
        REQUIRE(r.width <= 4u);

        const int cls = classifyFirst(buf[0]);

        if (r.status == S::NeedMore) {
            // NeedMore only when the first byte is a valid starter and too few bytes is provided.
            REQUIRE(cls == 2 || cls == 3 || cls == 4);
            REQUIRE(static_cast<int>(r.width) == cls);
            REQUIRE(n < cls);
        } else if (r.status == S::Invalid) {
            // Invalid must consume exactly 1 and starter must be impossible (not ASCII).
            REQUIRE_EQ(r.width, 1u);
            // ASCII must never be invalid.
            REQUIRE(cls != 1);
            // If the first byte claims a multibyte starter but we didn't provide enough bytes,
            // decoder should have said NeedMore, not Invalid.
            if (cls == 2 || cls == 3 || cls == 4) {
                REQUIRE(n >= cls);
            }
        } else { // Ok
            // Width must be canonical length for the scalar and re-encode must match bytes consumed.
            const int w = static_cast<int>(r.width);
            REQUIRE(w >= 1 && w <= 4);
            // Validate shape and compute cp with independent checker
            uint32_t cp2 = validateOk(buf, w);
            REQUIRE(cp2 != 0xFFFFFFFFu);
            REQUIRE_EQ(r.cp, cp2);

            uint8_t re[4]; 
            auto enc = UTF8Handler::encode(r.cp, re, 4); // to ensure consistency with reference
            REQUIRE_EQ(enc.status , UTF8Handler::EncodeStatus::Ok);


            REQUIRE_EQ(enc.width, w);
            for (int i = 0; i < w; ++i) {
                REQUIRE_EQ(re[i], buf[i]);
            }
            // If buffer had extra bytes beyond width, the fuzz doesn't require anything about them.
        }
    }
}

// Random partial-availability fuzz. Ensures NeedMore semantics match class + cutoff.
TEST_CASE(UTF8Handler_Fuzz_RandomPartialAvailability) {
    std::mt19937 rng(0xBADC0DEu);
    std::uniform_int_distribution<int> lenDist(0, 6);  // allow 0 to hit avail==0 path
    std::uniform_int_distribution<int> byteDist(0, 255);

    for (int iter = 0; iter < 50000; ++iter) {
        const int total = std::max(1, lenDist(rng)); // ensure we have at least one byte in buffer
        uint8_t buf[8];
        for (int i = 0; i < total; ++i) 
            buf[i] = static_cast<uint8_t>(byteDist(rng));

        // Choose a cutoff 0..total
        std::uniform_int_distribution<int> cutDist(0, total);
        const int cut = cutDist(rng);

        auto r = UTF8Handler::decode(buf, static_cast<std::size_t>(cut));
        REQUIRE(r.width >= 1u);
        REQUIRE(r.width <= 4u);

        if (cut == 0) {
            // Contract: NeedMore width=1 when avail==0
            REQUIRE_EQ(r.status, S::NeedMore);
            REQUIRE_EQ(r.width, 1u);
            continue;
        }

        const int cls = classifyFirst(buf[0]);

        if (r.status == S::NeedMore) {
            REQUIRE(cls == 2 || cls == 3 || cls == 4);
            REQUIRE_EQ(static_cast<int>(r.width), cls);
            REQUIRE(cut < cls);
        } else if (r.status == S::Invalid) {
            REQUIRE_EQ(r.width, 1u);
            // ASCII must never be invalid.
            REQUIRE(cls != 1);
            // If we cut before the required width for a multibyte starter, it should have been NeedMore.
            if (cls == 2 || cls == 3 || cls == 4) {
                REQUIRE(cut >= cls);
            }
            // Note: Even with class 2/3/4 and cut >= required, Invalid can happen due to bad continuations/overlong.
            // With small cut < required, decoder should have said NeedMore, which we asserted above.
        } else { // Ok
            const int w = static_cast<int>(r.width);
            REQUIRE(w <= cut); // cannot consume beyond avail
            // Check canonical with independent checker on just the available bytes we consumed
            uint32_t cp2 = validateOk(buf, w);
            REQUIRE(cp2 != 0xFFFFFFFFu);
            REQUIRE_EQ(r.cp, cp2);

            uint8_t re[4];
            auto enc = UTF8Handler::encode(r.cp, re, 4); // to ensure consistency with reference 
            REQUIRE_EQ(enc.status , UTF8Handler::EncodeStatus::Ok);
            REQUIRE_EQ(enc.width, w);
            for (int i = 0; i < w; ++i)
                REQUIRE_EQ(re[i], buf[i]);
        }
    }
}

//--- encode tests ---//
static void expect_bytes(const uint8_t* got, int n, std::initializer_list<uint8_t> exp) {
    int i = 0;
    for (auto b : exp) {
        REQUIRE(i < n);
        REQUIRE_EQ(got[i], b);
        ++i;
    }
    REQUIRE_EQ(i, n);
}

TEST_CASE(UTF8_Encode_ASCII_OneByte) {
    uint8_t out[4]{};
    auto er = UTF8Handler::encode(0x41, out); // 'A'
    REQUIRE_EQ(er.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(er.width, 1);
    expect_bytes(out, er.width, {0x41});

    // round-trip
    auto dr = UTF8Handler::decode(out, er.width);
    REQUIRE_EQ(dr.status, UTF8Handler::DecodeStatus::Ok);
    REQUIRE_EQ(dr.width, 1);
    REQUIRE_EQ(dr.cp, 0x41u);
}

TEST_CASE(UTF8_Encode_TwoByte) {
    uint8_t out[4]{};
    auto er = UTF8Handler::encode(0x00A9, out); // ©
    REQUIRE_EQ(er.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(er.width, 2);
    expect_bytes(out, er.width, {0xC2, 0xA9});

    auto dr = UTF8Handler::decode(out, er.width);
    REQUIRE_EQ(dr.status, UTF8Handler::DecodeStatus::Ok);
    REQUIRE_EQ(dr.cp, 0x00A9u);
}

TEST_CASE(UTF8_Encode_ThreeByte) {
    uint8_t out[4]{};
    auto er = UTF8Handler::encode(0x20AC, out); // €
    REQUIRE_EQ(er.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(er.width, 3);
    expect_bytes(out, er.width, {0xE2, 0x82, 0xAC});

    auto dr = UTF8Handler::decode(out, er.width);
    REQUIRE_EQ(dr.status, UTF8Handler::DecodeStatus::Ok);
    REQUIRE_EQ(dr.cp, 0x20ACu);
}

TEST_CASE(UTF8_Encode_FourByte) {
    uint8_t out[4]{};
    auto er = UTF8Handler::encode(0x1F600, out); // 😀
    REQUIRE_EQ(er.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(er.width, 4);
    expect_bytes(out, er.width, {0xF0, 0x9F, 0x98, 0x80});

    auto dr = UTF8Handler::decode(out, er.width);
    REQUIRE_EQ(dr.status, UTF8Handler::DecodeStatus::Ok);
    REQUIRE_EQ(dr.cp, 0x1F600u);
}

TEST_CASE(UTF8_Encode_Boundaries) {
    uint8_t out[4]{};

    auto e1 = UTF8Handler::encode(0x007F, out); // max 1-byte
    REQUIRE_EQ(e1.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(e1.width, 1);

    auto e2 = UTF8Handler::encode(0x0080, out); // min 2-byte
    REQUIRE_EQ(e2.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(e2.width, 2);

    auto e3 = UTF8Handler::encode(0x07FF, out); // max 2-byte
    REQUIRE_EQ(e3.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(e3.width, 2);

    auto e4 = UTF8Handler::encode(0x0800, out); // min 3-byte
    REQUIRE_EQ(e4.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(e4.width, 3);

    auto e5 = UTF8Handler::encode(0xFFFF, out); // max 3-byte (non-surrogate)
    REQUIRE_EQ(e5.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(e5.width, 3);

    auto e6 = UTF8Handler::encode(0x10000, out); // min 4-byte
    REQUIRE_EQ(e6.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(e6.width, 4);

    auto e7 = UTF8Handler::encode(0x10FFFF, out); // max valid scalar
    REQUIRE_EQ(e7.status, UTF8Handler::EncodeStatus::Ok);
    REQUIRE_EQ(e7.width, 4);
}

TEST_CASE(UTF8_Encode_Reject_Surrogates) {
    uint8_t out[4]{};
    auto lo = UTF8Handler::encode(0xD800, out);
    REQUIRE_EQ(lo.status, UTF8Handler::EncodeStatus::Invalid);

    auto hi = UTF8Handler::encode(0xDFFF, out);
    REQUIRE_EQ(hi.status, UTF8Handler::EncodeStatus::Invalid);
}

TEST_CASE(UTF8_Encode_Reject_AboveUnicodeMax) {
    uint8_t out[4]{};
    auto er = UTF8Handler::encode(0x110000, out); // > U+10FFFF
    REQUIRE_EQ(er.status, UTF8Handler::EncodeStatus::Invalid);
}

TEST_CASE(UTF8_Encode_BoundedBuffer_NeedMore) {
    uint8_t out[2]{};

    // 2-byte codepoint into 1-byte buffer → NeedMore(2)
    auto e2 = UTF8Handler::encode(0x00A9, out, 1);
    REQUIRE_EQ(e2.status, UTF8Handler::EncodeStatus::NeedMore);
    REQUIRE_EQ(e2.width, 2);

    // 3-byte into 2-byte buffer → NeedMore(3)
    auto e3 = UTF8Handler::encode(0x20AC, out, 2);
    REQUIRE_EQ(e3.status, UTF8Handler::EncodeStatus::NeedMore);
    REQUIRE_EQ(e3.width, 3);

    // 4-byte into 3-byte buffer → NeedMore(4)
    uint8_t out3[3]{};
    auto e4 = UTF8Handler::encode(0x1F600, out3, 3);
    REQUIRE_EQ(e4.status, UTF8Handler::EncodeStatus::NeedMore);
    REQUIRE_EQ(e4.width, 4);
}

TEST_CASE(UTF8_Encode_RoundTrip_Sweep) {
    // A small sweep across ranges to catch regressions
    const uint32_t cps[] = {
        0x0000, 0x0024, 0x007F, 0x0080, 0x07FF,
        0x0800, 0x20AC, 0xD7FF, 0xE000, 0xFFFF,
        0x10000, 0x10FFFF
    };
    for (uint32_t cp : cps) {
        uint8_t enc[4]{};
        auto er = UTF8Handler::encode(cp, enc);
        REQUIRE_EQ(er.status, UTF8Handler::EncodeStatus::Ok);
        auto dr = UTF8Handler::decode(enc, er.width);
        REQUIRE_EQ(dr.status, UTF8Handler::DecodeStatus::Ok);
        REQUIRE_EQ(dr.cp, cp);
    }
}