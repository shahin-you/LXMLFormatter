#define NO_UT_MAIN

#include "UnitTestFramework.h"
#include "BufferedInputStream.h"
#include "XMLTokenizerTypes.h"
#include "XMLTokenizer.h"

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