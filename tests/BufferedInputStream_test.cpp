#include "UnitTestFramework.h"
#include "BufferedInputStream.h"

TEST_CASE(ReadAscii) {
    std::istringstream in("abc");
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 4);
    REQUIRE_NE(bis, nullptr);
    //since we don't have ASSERT in our test frame work, i have to add the following if statement.
    // it will be replaced by ASSERT when it is added to test framework
    if (!bis) {
        return;
    }
    REQUIRE_EQ(bis->getChar(), 'a');
    REQUIRE_EQ(bis->getChar(), 'b');
    REQUIRE_EQ(bis->peekChar(), 'c');
    REQUIRE_EQ(bis->getChar(), 'c');
    REQUIRE_EQ(bis->getChar(), -1); // EOF
}

TEST_CASE(MultibyteUTF8) {
    std::string sample = u8"πβ"; // two 2‑byte codepoints
    std::istringstream in(sample);
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 4);
    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    REQUIRE_EQ(bis->getChar(), 0x03C0); // π
    REQUIRE_EQ(bis->getChar(), 0x03B2); // β
    REQUIRE_EQ(bis->getChar(), -1);
    REQUIRE(bis->eof());
}

TEST_CASE(ReadWhileLetters) {
    std::istringstream in("abc123");
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 4);
    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    std::string out;
    bis->readWhile(out, [](int32_t ch) { return std::isalpha(static_cast<unsigned char>(ch)); });
    REQUIRE_EQ(out, "abc");
    REQUIRE_EQ(bis->getChar(), '1');
}

TEST_CASE(ReadUntilDelimiter) {
    std::istringstream in("hello,world");
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 16);
    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    std::string out;
    bis->readUntil(out, ',');
    REQUIRE_EQ(out, "hello");
    REQUIRE_EQ(bis->getChar(), ',');
}

TEST_CASE(LineColumnTracking) {
    std::istringstream in("a\nb\r\nc"); // Mix LF and CR‑LF
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 4);
    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    REQUIRE_EQ(bis->getChar(), 'a');
    REQUIRE_EQ(bis->getCurrentLine(), 1u);
    REQUIRE_EQ(bis->getCurrentColumn(), 2u);

    REQUIRE_EQ(bis->getChar(), '\n');
    REQUIRE_EQ(bis->getCurrentLine(), 2u);
    REQUIRE_EQ(bis->getCurrentColumn(), 1u);

    REQUIRE_EQ(bis->getChar(), 'b');
    REQUIRE_EQ(bis->getCurrentLine(), 2u);
    REQUIRE_EQ(bis->getCurrentColumn(), 2u);

    REQUIRE_EQ(bis->getChar(), '\r'); // CR of CR‑LF
    REQUIRE_EQ(bis->getCurrentLine(), 3u);
    REQUIRE_EQ(bis->getCurrentColumn(), 1u);
}

TEST_CASE(PeekCRLFStateRegression) {
    std::istringstream in("A\r\nB");
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 2); // tiny buffer forces boundary crossing

    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    REQUIRE_EQ(bis->getChar(),'A');                    // consume 'A'
    REQUIRE_EQ(bis->getCurrentLine(),1u);
    REQUIRE_EQ(bis->getCurrentColumn(),2u);

    REQUIRE_EQ(bis->getChar(),'\r');                  // consume CR, should advance line
    REQUIRE_EQ(bis->getCurrentLine(),2u);
    REQUIRE_EQ(bis->getCurrentColumn(),1u);

    REQUIRE_EQ(bis->peekChar(),'\n');                 // look‑ahead LF across boundary
    // State after peek should be unchanged
    REQUIRE_EQ(bis->getCurrentLine(),2u);
    REQUIRE_EQ(bis->getCurrentColumn(),1u);

    REQUIRE_EQ(bis->getChar(),'\n');                  // now actually consume LF
    REQUIRE_EQ(bis->getCurrentLine(),3u);             // line must increment *once*
    REQUIRE_EQ(bis->getCurrentColumn(),1u);

    REQUIRE_EQ(bis->getChar(),'B');                    // final char
}

TEST_CASE(ReadWhileCompactionRegression) {
    std::string text(50,'x');                         // 50 'x' characters
    std::istringstream in(text);
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 8); // small buffer triggers multiple compactions

    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    std::string out;
    bis->readWhile(out,[](int32_t ch){ return ch=='x'; });
    REQUIRE_EQ(out.size(), text.size());              // length must match
    REQUIRE_EQ(out, text);                            // content must be intact
}

TEST_CASE(PeekDoesNotConsumeRegression) {
    std::istringstream in("Z");
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 1);

    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    REQUIRE_EQ(bis->peekChar(),'Z'); // first peek
    REQUIRE_EQ(bis->peekChar(),'Z'); // second peek – still 'Z'
    REQUIRE_EQ(bis->getChar(),'Z');  // now consume
    REQUIRE_EQ(bis->getChar(),-1);   // EOF after consumption
}

TEST_CASE(SingleByteBufferASCII) {
    std::istringstream in("hello");
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 1);

    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    std::string out;
    bis->readWhile(out,[](int32_t ch){ return ch != -1; });
    REQUIRE_EQ(out,"hello");
}

TEST_CASE(SingleByteBufferUTF8) {
    std::string s = u8"π";                // two‑byte UTF‑8 char
    std::istringstream in(s);
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 1); // force boundary in middle of codepoint

    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    REQUIRE_EQ(bis->getChar(),0x03C0);     // π decoded correctly
    REQUIRE_EQ(bis->getChar(),-1);
}

TEST_CASE(LargeBufferSmallInput) {
    std::string text = "short";
    std::istringstream in(text);
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 1024*1024); // 1‑MiB buffer, tiny input

    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    std::string out;
    bis->readWhile(out,[](int32_t ch){ return ch != -1; });
    REQUIRE_EQ(out, text);
}

TEST_CASE(ZeroBufferSizeConstructor) {
    std::istringstream in("data");
    LXMLFormatter::BufferedInputStream::StateError err;
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 0, &err); 
    REQUIRE_EQ(bis, nullptr);
    REQUIRE_EQ(err, LXMLFormatter::BufferedInputStream::StateError::ZeroBufferSize);
}


TEST_CASE(MoveCtorPreservesState) {
    std::istringstream in("A\nB");
    auto p = LXMLFormatter::BufferedInputStream::Create(in, 2);
    REQUIRE(p);
    if (!p) {
        return;
    }

    REQUIRE_EQ(p->getChar(), 'A');   // consume 'A'
    REQUIRE_EQ(p->peekChar(), '\n'); // set up newline-related flags

    LXMLFormatter::BufferedInputStream b(std::move(*p));

    // The moved-from object (still owned by p) should be inert/safe
    REQUIRE_EQ(p->isValid(), false);

    REQUIRE_EQ(b.getChar(), '\n');
    REQUIRE_EQ(b.getCurrentLine(), 2u);
    REQUIRE_EQ(b.getCurrentColumn(), 1u);
    REQUIRE_EQ(b.getChar(), 'B');
    REQUIRE_EQ(b.getChar(), -1);
}

TEST_CASE(MovedFromActsAsEOFEverywhere) {
    std::istringstream in("123");
    auto p = LXMLFormatter::BufferedInputStream::Create(in, 2);
    REQUIRE(p);

    LXMLFormatter::BufferedInputStream moved(std::move(*p));

    REQUIRE(!p->isValid());

    std::string out;
    REQUIRE_EQ(p->getChar(), -1);
    REQUIRE_EQ(p->peekChar(), -1);
    p->readWhile(out, [](int32_t){ return true; });
    REQUIRE_EQ(out.size(), 0u);
    p->readUntil(out, ',');
    REQUIRE_EQ(out.size(), 0u);

    REQUIRE_EQ(moved.getChar(), '1');
}