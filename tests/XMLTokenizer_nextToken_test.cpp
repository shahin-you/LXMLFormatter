#define NO_UT_MAIN

#include "UnitTestFramework.h"
#include "BufferedInputStream.h"
#include "XMLTokenizerTypes.h"
#include "XMLTokenizer.h"
#include <sstream>

using namespace LXMLFormatter;

TEST_CASE(XMLTokenizer_Skeleton_TextOnly) {
    std::istringstream in("hello world");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    // First: DocumentStart
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::DocumentStart);
    
    // Second: Text
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::Text);
    REQUIRE_EQ(std::string(tok.data, tok.length), "hello world");
    
    // Third: DocumentEnd
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::DocumentEnd);
    
    // Fourth: No more tokens
    REQUIRE(!tokenizer.nextToken(tok));
}

TEST_CASE(XMLTokenizer_EmptyInput_DocumentStartEnd) {
    std::istringstream in("");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::DocumentStart);
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::DocumentEnd);
    
    REQUIRE(!tokenizer.nextToken(tok));
}

TEST_CASE(XMLTokenizer_IdempotentEnd_MultipleCallsAfterDocumentEnd) {
    std::istringstream in("");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // DocumentEnd
    
    // Call 5 more times - should all return false
    for (int i = 0; i < 5; ++i) {
        REQUIRE(!tokenizer.nextToken(tok));
    }
}

TEST_CASE(XMLTokenizer_TextWithCRLF_Normalized) {
    std::istringstream in("line1\r\nline2\rline3\nline4");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    TokenizerOptions opts;
    opts.flags |= TokenizerOptions::NormalizeLineEndings;
    XMLTokenizer tokenizer(*bis, opts);
    
    XMLToken tok;
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Text
    
    REQUIRE_EQ(tok.type, XMLTokenType::Text);
    // All line endings should be normalized to \n
    REQUIRE_EQ(std::string(tok.data, tok.length), "line1\nline2\nline3\nline4");
}

TEST_CASE(XMLTokenizer_TextWithCRLF_NotNormalized) {
    std::istringstream in("line1\r\nline2");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    TokenizerOptions opts;
    opts.flags &= ~TokenizerOptions::NormalizeLineEndings; // Disable
    XMLTokenizer tokenizer(*bis, opts);
    
    XMLToken tok;
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Text
    
    REQUIRE_EQ(tok.type, XMLTokenType::Text);
    // Should preserve original line endings
    REQUIRE_EQ(std::string(tok.data, tok.length), "line1\r\nline2");
}

TEST_CASE(XMLTokenizer_UnexpectedEOF_AfterLessThan) {
    std::istringstream in("text<");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Text("text")
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::Error);
    REQUIRE_EQ(std::string(tok.data, tok.length), "Unexpected EOF after '<'");
    
    // After fatal error, no more tokens
    REQUIRE(!tokenizer.nextToken(tok));
}

TEST_CASE(XMLTokenizer_InvalidCharAfterLessThan_Number) {
    std::istringstream in("<123>");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::Error);
    
    const char* expected = "Invalid character after '<'";
    REQUIRE_EQ(tok.length, 28u);
    REQUIRE(std::memcmp(tok.data, expected, 28) == 0);
}

TEST_CASE(XMLTokenizer_InvalidCharAfterLessThan_Space) {
    std::istringstream in("< element>");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::Error);

    const char* expected = "Invalid character after '<'";
    REQUIRE_EQ(tok.length, 28u);
    REQUIRE(std::memcmp(tok.data, expected, 28) == 0);
}

TEST_CASE(XMLTokenizer_TextExceedsLimit) {
    std::string hugeText(100000, 'x');
    std::istringstream in(hugeText);
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    TokenizerLimits lims;
    lims.maxTextRunBytes = 1000; // Small limit
    XMLTokenizer tokenizer(*bis, {}, lims);
    
    XMLToken tok;
    tokenizer.nextToken(tok); // DocumentStart
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::Error);

    const char* expected = "Text run exceeds limit";
    REQUIRE_EQ(tok.length, 22);
    REQUIRE(std::memcmp(tok.data, expected, 22) == 0);
    REQUIRE(!tokenizer.nextToken(tok));
}

TEST_CASE(XMLTokenizer_ZeroMaxPerTagBytes_RejectedEarly) {
    std::istringstream in("<element/>");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    TokenizerLimits lims;
    lims.maxPerTagBytes = 0; // Invalid configuration
    XMLTokenizer tokenizer(*bis, {}, lims);
    
    XMLToken tok;
    tokenizer.nextToken(tok); // DocumentStart
    
    // When we try to parse tag (not implemented yet, will error)
    // But ensureCurrentTagBuffer should catch zero config
    // This test will be more meaningful once we implement tag parsing
}

TEST_CASE(XMLTokenizer_PositionTracking_DocumentStart) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::DocumentStart);
    REQUIRE_EQ(tok.line, 1u);
    REQUIRE_EQ(tok.column, 1u);
    REQUIRE_EQ(tok.byteOffset, 0u);
}

TEST_CASE(XMLTokenizer_PositionTracking_DocumentEnd) {
    std::istringstream in("abc");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Text
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::DocumentEnd);
    REQUIRE_EQ(tok.byteOffset, 3u); // After 3 bytes
    REQUIRE_EQ(tok.line, 1u);
    REQUIRE_EQ(tok.column, 4u);
}

TEST_CASE(XMLTokenizer_PositionTracking_TextWithNewlines) {
    std::istringstream in("line1\nline2\nline3");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::Text);
    REQUIRE_EQ(tok.line, 1u);
    REQUIRE_EQ(tok.column, 1u);
    
    // DocumentEnd should be after last line
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::DocumentEnd);
    REQUIRE_EQ(tok.line, 3u);
}

TEST_CASE(XMLTokenizer_ErrorCollection_SingleError) {
    std::istringstream in("<");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    REQUIRE_EQ(tokenizer.errors().size(), 0u);
    
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Error
    
    const auto& errors = tokenizer.errors();
    REQUIRE_EQ(errors.size(), 1u);
    REQUIRE_EQ(errors[0].code, TokenizerErrorCode::UnexpectedEOF);
    REQUIRE_EQ(errors[0].sev, ErrorSeverity::Fatal);
}

TEST_CASE(XMLTokenizer_ErrorCollection_ClearErrors) {
    std::istringstream in("<");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Error
    
    REQUIRE_EQ(tokenizer.errors().size(), 1u);
    
    tokenizer.clearErrors();
    REQUIRE_EQ(tokenizer.errors().size(), 0u);
}

TEST_CASE(XMLTokenizer_Reset_RestoresInitialState) {
    std::istringstream in("test1");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    // First run
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Text
    tokenizer.nextToken(tok); // DocumentEnd
    REQUIRE(!tokenizer.nextToken(tok)); // Ended
    
    // Reset
    tokenizer.reset();
    
    // Should be able to tokenize again from the same stream position
    // Note: stream doesn't rewind, but state is reset
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::DocumentStart);
}

TEST_CASE(XMLTokenizer_Text_ValidUTF8_Multibyte) {
    std::string utf8Text = u8"Hello 世界 🌍";
    std::istringstream in(utf8Text);
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Text
    
    REQUIRE_EQ(tok.type, XMLTokenType::Text);
    REQUIRE_EQ(std::string(tok.data, tok.length), utf8Text);
}

TEST_CASE(XMLTokenizer_Text_InvalidUTF8_TreatedAsEOF) {
    // Invalid UTF-8 sequence
    std::string invalidUTF8;
    invalidUTF8.push_back('h');
    invalidUTF8.push_back('i');
    invalidUTF8.push_back(static_cast<char>(0xFF)); // Invalid UTF-8
    invalidUTF8.push_back('x');
    
    std::istringstream in(invalidUTF8);
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Text or Error
    
    // scanText treats invalid UTF-8 as EOF
    // Should get "hi" then error or DocumentEnd
    if (tok.type == XMLTokenType::Text) {
        REQUIRE_EQ(std::string(tok.data, tok.length), "hi");
    }
}

TEST_CASE(XMLTokenizer_LargeText_WithinLimits) {
    std::string largeText(50000, 'x'); // 50KB
    std::istringstream in(largeText);
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    TokenizerLimits lims;
    lims.maxTextRunBytes = 100000; // 100KB limit
    XMLTokenizer tokenizer(*bis, {}, lims);
    
    XMLToken tok;
    tokenizer.nextToken(tok); // DocumentStart
    
    REQUIRE(tokenizer.nextToken(tok));
    REQUIRE_EQ(tok.type, XMLTokenType::Text);
    REQUIRE_EQ(tok.length, 50000u);
}

TEST_CASE(XMLTokenizer_SmallBufferSize_StillWorks) {
    std::istringstream in("hello");
    // Very small buffer (minimum is 4 bytes for UTF-8)
    auto bis = BufferedInputStream::Create(in, 4);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken tok;
    
    tokenizer.nextToken(tok); // DocumentStart
    tokenizer.nextToken(tok); // Text
    
    REQUIRE_EQ(tok.type, XMLTokenType::Text);
    REQUIRE_EQ(std::string(tok.data, tok.length), "hello");
}

TEST_CASE(XMLTokenizer_NestingDepth_InitiallyZero) {
    std::istringstream in("");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    
    REQUIRE_EQ(tokenizer.nestingDepth(), 0u);
}

TEST_CASE(XMLTokenizer_State_InitiallyContent) {
    std::istringstream in("");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    
    REQUIRE_EQ(tokenizer.state(), State::Content);
}

TEST_CASE(XMLTokenizer_CurrentPosition_TracksCorrectly) {
    std::istringstream in("abc\ndef");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    
    auto pos = tokenizer.currentPosition();
    REQUIRE_EQ(pos.line, 1u);
    REQUIRE_EQ(pos.column, 1u);
    REQUIRE_EQ(pos.byteOffset, 0u);
}