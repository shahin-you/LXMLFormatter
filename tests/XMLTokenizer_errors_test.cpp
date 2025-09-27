#define NO_UT_MAIN

#include "UnitTestFramework.h"
#include "XMLTokenizerTypes.h"
#include "XMLTokenizer.h"
#include <sstream>

using namespace LXMLFormatter;

TEST_CASE(XMLTokenizer_EmitError_BasicErrorToken) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken token;
    
    bool result = tokenizer.emitError(token, 
                                        TokenizerErrorCode::LimitExceeded,
                                        ErrorSeverity::Fatal, 
                                        "Test error message", 18);
    
    REQUIRE(result);
    REQUIRE_EQ(token.type, XMLTokenType::Error);
    REQUIRE_EQ(token.length, 18u);
    REQUIRE_EQ(std::string(token.data, token.length), "Test error message");
    REQUIRE_EQ(token.line, 1u);
    REQUIRE_EQ(token.column, 1u);
    REQUIRE_EQ(token.byteOffset, 0u);
}

TEST_CASE(XMLTokenizer_EmitError_NullMessageHandling) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken token;
    
    bool result = tokenizer.emitError(token,
                                        TokenizerErrorCode::InvalidCharAfterLT,
                                        ErrorSeverity::Fatal,
                                        nullptr, 0);
    
    REQUIRE(result);
    REQUIRE_EQ(token.type, XMLTokenType::Error);
    REQUIRE_EQ(std::string(token.data, token.length), "Tokenizer error");
}

TEST_CASE(XMLTokenizer_EmitError_ErrorCollection) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken token;
    
    REQUIRE_EQ(tokenizer.errors().size(), 0u);
    
    tokenizer.emitError(token,
                       TokenizerErrorCode::ExpectedEqualsAfterAttrName,
                       ErrorSeverity::Recoverable,
                       "Missing equals", 14);
    
    const auto& errors = tokenizer.errors();
    REQUIRE_EQ(errors.size(), 1u);
    REQUIRE_EQ(errors[0].code, TokenizerErrorCode::ExpectedEqualsAfterAttrName);
    REQUIRE_EQ(errors[0].sev, ErrorSeverity::Recoverable);
    REQUIRE_EQ(errors[0].msg, "Missing equals");
    REQUIRE_EQ(errors[0].where.line, 1u);
    REQUIRE_EQ(errors[0].where.column, 1u);
}

TEST_CASE(XMLTokenizer_EmitError_FatalSetsEndedFlag) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    {
        XMLTokenizer tokenizer(*bis);
        XMLToken token;
        
        tokenizer.emitError(token,
                           TokenizerErrorCode::UnexpectedEOF,
                           ErrorSeverity::Fatal,
                           "Unexpected end", 14);
        
        // TODO: Once nextToken() is implemented, verify it returns false
        // For now, just verify the error was recorded correctly
        const auto& errors = tokenizer.errors();
        REQUIRE_EQ(errors.size(), 1u);
        REQUIRE_EQ(errors[0].sev, ErrorSeverity::Fatal);
    }
}

TEST_CASE(XMLTokenizer_EmitError_NonFatalDoesNotSetEndedFlag) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    {
        XMLTokenizer tokenizer(*bis);
        XMLToken token;
        
        // Emit recoverable error  
        tokenizer.emitError(token,
                           TokenizerErrorCode::ExpectedEqualsAfterAttrName,
                           ErrorSeverity::Recoverable,
                           "Recoverable issue", 16);
        
        // TODO: Once nextToken() is implemented, verify parsing can continue
        // For now, verify the error was recorded as non-fatal
        const auto& errors = tokenizer.errors();
        REQUIRE_EQ(errors.size(), 1u);
        REQUIRE_EQ(errors[0].sev, ErrorSeverity::Recoverable);
    }
}

TEST_CASE(XMLTokenizer_EmitError_PositionTracking) {
    std::istringstream in("hello world");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    
    // Advance the input position by reading some characters
    bis->getChar(); // 'h'
    bis->getChar(); // 'e' 
    bis->getChar(); // 'l'
    
    XMLToken token;
    tokenizer.emitError(token,
                       TokenizerErrorCode::InvalidCharInName,
                       ErrorSeverity::Fatal,
                       "Position test", 13);
    
    // Should capture current position (after reading 3 chars)
    REQUIRE_EQ(token.byteOffset, 3u);
    REQUIRE_EQ(token.line, 1u);
    REQUIRE_EQ(token.column, 4u);  // Column after 'l'
}

TEST_CASE(XMLTokenizer_EmitError_PendingStartPosition) {
    std::istringstream in("test data");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    
    // Advance stream position
    bis->getChar(); // 't'
    bis->getChar(); // 'e'
    
    // Mark token start at current position
    // Note: This is calling a private method through public interface
    // We need to access markTokenStart somehow, or test this through scanText
    
    XMLToken token;
    tokenizer.emitError(token,
                       TokenizerErrorCode::LimitExceeded,
                       ErrorSeverity::Fatal,
                       "Pending test", 12);
    
    // Position should be current since no markTokenStart was called
    REQUIRE_EQ(token.byteOffset, 2u);
    REQUIRE_EQ(token.column, 3u);
}

TEST_CASE(XMLTokenizer_EmitError_MultipleErrors) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken token1, token2, token3;
    
    // Emit multiple errors
    tokenizer.emitError(token1, TokenizerErrorCode::LimitExceeded,
                       ErrorSeverity::Warning, "First error", 11);
    tokenizer.emitError(token2, TokenizerErrorCode::InvalidCharAfterLT,
                       ErrorSeverity::Recoverable, "Second error", 12);
    tokenizer.emitError(token3, TokenizerErrorCode::UnexpectedEOF,
                       ErrorSeverity::Fatal, "Third error", 11);
    
    // Verify all errors were collected
    const auto& errors = tokenizer.errors();
    REQUIRE_EQ(errors.size(), 3u);
    REQUIRE_EQ(errors[0].msg, "First error");
    REQUIRE_EQ(errors[1].msg, "Second error");
    REQUIRE_EQ(errors[2].msg, "Third error");
    
    // Verify different severities
    REQUIRE_EQ(errors[0].sev, ErrorSeverity::Warning);
    REQUIRE_EQ(errors[1].sev, ErrorSeverity::Recoverable);
    REQUIRE_EQ(errors[2].sev, ErrorSeverity::Fatal);
}

TEST_CASE(XMLTokenizer_EmitError_MessageStability) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken token1, token2;
    
    tokenizer.emitError(token1, TokenizerErrorCode::LimitExceeded,
                       ErrorSeverity::Fatal, "Stable message 1", 16);
    
    const char* firstPtr = token1.data;
    
    tokenizer.emitError(token2, TokenizerErrorCode::InvalidCharAfterLT,
                       ErrorSeverity::Fatal, "Stable message 2", 16);
    
    REQUIRE_EQ(std::string(firstPtr, 16), "Stable message 1");
    REQUIRE_EQ(std::string(token2.data, 16), "Stable message 2");
    
    const auto& errors = tokenizer.errors();
    REQUIRE_EQ(errors.size(), 2u);
    REQUIRE_EQ(errors[0].msg, "Stable message 1");
    REQUIRE_EQ(errors[1].msg, "Stable message 2");
}

TEST_CASE(XMLTokenizer_EmitError_LongMessage) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken token;
    
    // Create a long error message
    std::string longMsg(500, 'x');
    longMsg += " end marker";
    
    tokenizer.emitError(token, TokenizerErrorCode::LimitExceeded,
                       ErrorSeverity::Fatal, longMsg.c_str(), 
                       static_cast<U32>(longMsg.length()));
    
    REQUIRE_EQ(token.type, XMLTokenType::Error);
    REQUIRE_EQ(token.length, longMsg.length());
    REQUIRE_EQ(std::string(token.data, token.length), longMsg);
    
    // Verify error was properly stored
    const auto& errors = tokenizer.errors();
    REQUIRE_EQ(errors.size(), 1u);
    REQUIRE_EQ(errors[0].msg, longMsg);
}

TEST_CASE(XMLTokenizer_EmitError_ClearErrors) {
    std::istringstream in("test");
    auto bis = BufferedInputStream::Create(in, 1024);
    REQUIRE(bis);
    
    XMLTokenizer tokenizer(*bis);
    XMLToken token;
    
    // Emit some errors
    tokenizer.emitError(token, TokenizerErrorCode::LimitExceeded,
                       ErrorSeverity::Fatal, "Error 1", 7);
    tokenizer.emitError(token, TokenizerErrorCode::InvalidCharAfterLT,
                       ErrorSeverity::Fatal, "Error 2", 7);
    
    REQUIRE_EQ(tokenizer.errors().size(), 2u);
    
    // Clear errors
    tokenizer.clearErrors();
    REQUIRE_EQ(tokenizer.errors().size(), 0u);
}