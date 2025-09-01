#define NO_UT_MAIN
#include "UnitTestFramework.h"
#include "XMLTokenizerTypes.h"
#include <type_traits>
#include <string>
#include <cstring>

// ------------------------------
// CharClass tests (functional)
// ------------------------------
TEST_CASE(CharClass_NameStart_ASCII_AllowList) {
    using CC = LXMLFormatter::CharClass;

    // Allowed: ':', '_', A..Z, a..z
    REQUIRE(CC::isNameStart(':'));
    REQUIRE(CC::isNameStart('_'));
    for (unsigned c = 'A'; c <= 'Z'; ++c)
        REQUIRE(CC::isNameStart(c));
    for (unsigned c = 'a'; c <= 'z'; ++c) 
        REQUIRE(CC::isNameStart(c));

    // Disallowed: digits, '-', '.', space, NUL, DEL
    for (unsigned c = '0'; c <= '9'; ++c) 
        REQUIRE(!CC::isNameStart(c));
    REQUIRE(!CC::isNameStart('-'));
    REQUIRE(!CC::isNameStart('.'));
    REQUIRE(!CC::isNameStart(' '));
    REQUIRE(!CC::isNameStart(0u));
    REQUIRE(!CC::isNameStart(127u)); // DEL
    REQUIRE(!CC::isNameStart('\t'));
}

TEST_CASE(CharClass_NameChar_ASCII_Superset) {
    using CC = LXMLFormatter::CharClass;

    // NameChar must include NameStart
    for (unsigned c = 0; c < 128; ++c) {
        if (CC::isNameStart(c)) {
            REQUIRE(CC::isNameChar(c));
        }
    }

    // Additional allowed: '-', '.', '0'..'9'
    REQUIRE(CC::isNameChar('-'));
    REQUIRE(CC::isNameChar('.'));
    for (unsigned c = '0'; c <= '9'; ++c) 
        REQUIRE(CC::isNameChar(c));

    // Disallowed samples: whitespace & XML punctuation
    const unsigned dis[] = { ' ', '\t', '\r', '\n', '<', '>', '&', '\'', '\"', '/', '\\' };
    for (unsigned c : dis) 
        REQUIRE(!CC::isNameChar(c));
}

TEST_CASE(CharClass_IsXMLWhitespace_ExactSet) {
    using CC = LXMLFormatter::CharClass;
    // XML whitespace: exactly these 4 characters
    REQUIRE(CC::isXMLWhitespace(0x20)); // space
    REQUIRE(CC::isXMLWhitespace(0x09)); // tab
    REQUIRE(CC::isXMLWhitespace(0x0A)); // LF
    REQUIRE(CC::isXMLWhitespace(0x0D)); // CR
    
    // Test all other ASCII characters are NOT whitespace
    for (uint32_t cp = 0; cp <= 127; ++cp) {
        if (cp != 0x20 && cp != 0x09 && cp != 0x0A && cp != 0x0D) {
            REQUIRE(!CC::isXMLWhitespace(cp));
        }
    }
    
    // Non-ASCII should not be XML whitespace
    REQUIRE(!CC::isXMLWhitespace(0x00A0)); // non-breaking space
    REQUIRE(!CC::isXMLWhitespace(0x1680)); // ogham space mark
}
TEST_CASE(CharClass_PubidChar_ASCII_Exact) {
    using CC = LXMLFormatter::CharClass;

    // Allowed punctuation per XML 1.0 PUBLIC ID
    const char* ok = " -'()+,./:=?;!*#@$_%";
    for (const char* p = ok; *p; ++p) REQUIRE(CC::isPubidChar(static_cast<unsigned>(*p)));

    // Alnum allowed
    for (unsigned c = 'A'; c <= 'Z'; ++c) REQUIRE(CC::isPubidChar(c));
    for (unsigned c = 'a'; c <= 'z'; ++c) REQUIRE(CC::isPubidChar(c));
    for (unsigned c = '0'; c <= '9'; ++c) REQUIRE(CC::isPubidChar(c));

    // Not allowed: backslash, backtick, caret, pipe, tilde, quotes mismatch
    const unsigned bad[] = { '\\', '`', '^', '|', '~', '"' };
    for (unsigned c : bad) REQUIRE(!CC::isPubidChar(c));

    // Non-ASCII must be false
    REQUIRE(!CC::isPubidChar(0x80u));
    REQUIRE(!CC::isPubidChar(0x20ACu)); // €
}

TEST_CASE(CharClass_NonASCII_CurrentPolicy) {
    using CC = LXMLFormatter::CharClass;
    REQUIRE(CC::isNameStart(0x80u));
    REQUIRE(CC::isNameChar(0x80u));
    REQUIRE(CC::isNameStart(0x400u));
    REQUIRE(CC::isNameChar(0x400u));
    REQUIRE(CC::isNameStart(0x1F600u)); // smiling face :)
    REQUIRE(CC::isNameChar(0x1F600u)); 
}

TEST_CASE(CharClass_IsPubidChar_AllValidPunctuation) {
    // Test every punctuation character explicitly listed in the switch
    const char valid_punct[] = {
        ' ', '\r', '\n', '-', '\'', '(', ')', '+', ',', '.', '/', 
        ':', '=', '?', ';', '!', '*', '#', '@', '$', '_', '%'
    };
    
    for (char c : valid_punct) {
        REQUIRE(LXMLFormatter::CharClass::isPubidChar(c));
    }
}

TEST_CASE(CharClass_IsPubidChar_KeyInvalidChars) {
    // Test specific characters that should be invalid
    using CC = LXMLFormatter::CharClass;
    REQUIRE(!CC::isPubidChar('"')); // double quote not allowed
    REQUIRE(!CC::isPubidChar('<'));
    REQUIRE(!CC::isPubidChar('>'));
    REQUIRE(!CC::isPubidChar('&'));
    REQUIRE(!CC::isPubidChar('\t')); // tab not in pubid set
    REQUIRE(!CC::isPubidChar('\\'));
    REQUIRE(!CC::isPubidChar('`'));
    REQUIRE(!CC::isPubidChar('^'));
    REQUIRE(!CC::isPubidChar('|'));
    REQUIRE(!CC::isPubidChar('~'));
    REQUIRE(!CC::isPubidChar('{'));
    REQUIRE(!CC::isPubidChar('}'));
    REQUIRE(!CC::isPubidChar('['));
    REQUIRE(!CC::isPubidChar(']'));
}

TEST_CASE(TokenizerFlags_SetClrTest_Correctness) {
    LXMLFormatter::TokenizerFlags f{};
    constexpr auto S = LXMLFormatter::TokenizerFlags::Started;
    constexpr auto E = LXMLFormatter::TokenizerFlags::Ended;
    constexpr auto A = LXMLFormatter::TokenizerFlags::InAttr;
    constexpr auto R = LXMLFormatter::TokenizerFlags::SawCR;

    // Initially all false (no default-ctor checks beyond functional test)
    REQUIRE(!f.test(S));
    REQUIRE(!f.test(E));
    REQUIRE(!f.test(A));
    REQUIRE(!f.test(R));

    // Set multiple bits
    f.set(S); f.set(A);
    REQUIRE(f.test(S));
    REQUIRE(f.test(A));
    REQUIRE(!f.test(E));
    REQUIRE(!f.test(R));

    // Idempotent
    f.set(S); f.set(A);
    REQUIRE(f.test(S));
    REQUIRE(f.test(A));

    // Clear one, others remain
    f.clr(S);
    REQUIRE(!f.test(S));
    REQUIRE(f.test(A));

    // Set remaining
    f.set(E); f.set(R);
    REQUIRE(f.test(E));
    REQUIRE(f.test(R));

    // Clear all
    f.clr(A); f.clr(E); f.clr(R);
    REQUIRE(!f.test(A));
    REQUIRE(!f.test(E));
    REQUIRE(!f.test(R));
}

TEST_CASE(TokenizerFlags_SetMultipleFlags) {
    LXMLFormatter::TokenizerFlags flags{};
    constexpr auto S = LXMLFormatter::TokenizerFlags::Started;
    constexpr auto E = LXMLFormatter::TokenizerFlags::Ended;
    constexpr auto A = LXMLFormatter::TokenizerFlags::InAttr;
    constexpr auto R = LXMLFormatter::TokenizerFlags::SawCR;

    // Set multiple flags at once
    flags.set(S | A);
    REQUIRE(flags.test(S));
    REQUIRE(!flags.test(E));
    REQUIRE(flags.test(A));
    REQUIRE(!flags.test(R));
    
    // Add another flag
    flags.set(R);
    REQUIRE(flags.test(S));
    REQUIRE(!flags.test(E));
    REQUIRE(flags.test(A));
    REQUIRE(flags.test(R));
}


TEST_CASE(XMLToken_CopyMove_PreservesFields) {
    using namespace LXMLFormatter;

    static const char sample[] = "abc";
    XMLToken t{};
    // Fill with non-default values (functional copy/move test; no ctor-default checks)
    t.data       = sample;
    t.byteOffset = 0x1122334455667788ULL;
    t.length     = 3;
    t.line       = 1234;
    t.column     = 56;
    t.type       = XMLTokenType::StartTag;

    // Copy
    XMLToken c = t;
    REQUIRE_EQ(c.data, t.data);
    REQUIRE_EQ(c.byteOffset, t.byteOffset);
    REQUIRE_EQ(c.length, t.length);
    REQUIRE_EQ(c.line, t.line);
    REQUIRE_EQ(c.column, t.column);
    REQUIRE_EQ(static_cast<unsigned>(c.type), static_cast<unsigned>(t.type));

    // Move (for trivially copyable this is effectively a copy)
    XMLToken m = std::move(t);
    REQUIRE_EQ(m.data, sample);
    REQUIRE_EQ(m.byteOffset, 0x1122334455667788ULL);
    REQUIRE_EQ(m.length, 3u);
    REQUIRE_EQ(m.line, 1234u);
    REQUIRE_EQ(m.column, 56u);
    REQUIRE_EQ(static_cast<unsigned>(m.type), static_cast<unsigned>(XMLTokenType::StartTag));
}

TEST_CASE(XMLToken_ABI_RuntimeEcho) {
    REQUIRE_EQ(sizeof(LXMLFormatter::XMLToken), 32u);
    REQUIRE_EQ(alignof(LXMLFormatter::XMLToken), 32u);
}

TEST_CASE(XMLToken_CopyPreservesAllFields) {
    using namespace LXMLFormatter;
    XMLToken original;
    
    // Fill with non-default values
    const char* test_data = "test_string";
    original.data = test_data;
    original.byteOffset = 12345;
    original.length = 67890;
    original.line = 42;
    original.column = 88;
    original.type = XMLTokenType::StartTag;

#if defined(LXML_DEBUG_SLICES)
    original.arena = ArenaId::Tag;
    original.generation = 99;
#endif
    
    // Copy and verify all fields preserved
    XMLToken copy = original;
    
    REQUIRE_EQ(copy.data, original.data);
    REQUIRE_EQ(copy.byteOffset, original.byteOffset);
    REQUIRE_EQ(copy.length, original.length);
    REQUIRE_EQ(copy.line, original.line);
    REQUIRE_EQ(copy.column, original.column);
    REQUIRE_EQ(copy.type, original.type);

#if defined(LXML_DEBUG_SLICES)
    REQUIRE_EQ(copy.arena, original.arena);
    REQUIRE_EQ(copy.generation, original.generation);
#endif
}

TEST_CASE(XMLToken_MoveActsLikeCopyForPOD) {
    using namespace LXMLFormatter;
    XMLToken original;
    
    // Fill with non-default values
    const char* test_data = "move_test";
    original.data = test_data;
    original.byteOffset = 54321;
    original.length = 98765;
    original.line = 17;
    original.column = 29;
    original.type = XMLTokenType::AttributeName;

#if defined(LXML_DEBUG_SLICES)
    original.arena = ArenaId::Text;
    original.generation = 77;
#endif
    
    // Make copy for comparison
    XMLToken expected = original;
    
    // Move and verify same as copy (POD should have same semantics)
    XMLToken moved = std::move(original);
    
    REQUIRE_EQ(moved.data, expected.data);
    REQUIRE_EQ(moved.byteOffset, expected.byteOffset);
    REQUIRE_EQ(moved.length, expected.length);
    REQUIRE_EQ(moved.line, expected.line);
    REQUIRE_EQ(moved.column, expected.column);
    REQUIRE_EQ(moved.type, expected.type);

#if defined(LXML_DEBUG_SLICES)
    REQUIRE_EQ(moved.arena, expected.arena);
    REQUIRE_EQ(moved.generation, expected.generation);
#endif
}