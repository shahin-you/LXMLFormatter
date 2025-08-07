#include "UnitTestFramework.h"
#include "BufferedInputStream.h"

TEST_CASE(ReadAscii) {
    std::istringstream in("abc");
    LXMLFormatter::BufferedInputStream bis(in, 4);
    REQUIRE_EQ(bis.getChar(), 'a');
    REQUIRE_EQ(bis.getChar(), 'b');
    REQUIRE_EQ(bis.peekChar(), 'c');
    REQUIRE_EQ(bis.getChar(), 'c');
    REQUIRE_EQ(bis.getChar(), -1); // EOF
}

TEST_CASE(MultibyteUTF8) {
    std::string sample = u8"πβ"; // two 2‑byte codepoints
    std::istringstream in(sample);
    LXMLFormatter::BufferedInputStream bis(in, 4);
    REQUIRE_EQ(bis.getChar(), 0x03C0); // π
    REQUIRE_EQ(bis.getChar(), 0x03B2); // β
    REQUIRE_EQ(bis.getChar(), -1);
    REQUIRE(bis.eof());
}

TEST_CASE(ReadWhileLetters) {
    std::istringstream in("abc123");
    LXMLFormatter::BufferedInputStream bis(in, 4);
    std::string out;
    bis.readWhile(out, [](int32_t ch) { return std::isalpha(static_cast<unsigned char>(ch)); });
    REQUIRE_EQ(out, "abc");
    REQUIRE_EQ(bis.getChar(), '1');
}

TEST_CASE(ReadUntilDelimiter) {
    std::istringstream in("hello,world");
    LXMLFormatter::BufferedInputStream bis(in, 16);
    std::string out;
    bis.readUntil(out, ',');
    REQUIRE_EQ(out, "hello");
    REQUIRE_EQ(bis.getChar(), ',');
}

TEST_CASE(LineColumnTracking) {
    std::istringstream in("a\nb\r\nc"); // Mix LF and CR‑LF
    LXMLFormatter::BufferedInputStream bis(in, 4);
    REQUIRE_EQ(bis.getChar(), 'a');
    REQUIRE_EQ(bis.getCurrentLine(), 1u);
    REQUIRE_EQ(bis.getCurrentColumn(), 2u);

    REQUIRE_EQ(bis.getChar(), '\n');
    REQUIRE_EQ(bis.getCurrentLine(), 2u);
    REQUIRE_EQ(bis.getCurrentColumn(), 1u);

    REQUIRE_EQ(bis.getChar(), 'b');
    REQUIRE_EQ(bis.getCurrentLine(), 2u);
    REQUIRE_EQ(bis.getCurrentColumn(), 2u);

    REQUIRE_EQ(bis.getChar(), '\r'); // CR of CR‑LF
    REQUIRE_EQ(bis.getCurrentLine(), 3u);
    REQUIRE_EQ(bis.getCurrentColumn(), 1u);
}

TEST_CASE(PeekCRLFStateRegression) {
    std::istringstream in("A\r\nB");
    LXMLFormatter::BufferedInputStream bis(in,2); // tiny buffer forces boundary crossing

    REQUIRE_EQ(bis.getChar(),'A');                    // consume 'A'
    REQUIRE_EQ(bis.getCurrentLine(),1u);
    REQUIRE_EQ(bis.getCurrentColumn(),2u);

    REQUIRE_EQ(bis.getChar(),'\r');                  // consume CR, should advance line
    REQUIRE_EQ(bis.getCurrentLine(),2u);
    REQUIRE_EQ(bis.getCurrentColumn(),1u);

    REQUIRE_EQ(bis.peekChar(),'\n');                 // look‑ahead LF across boundary
    // State after peek should be unchanged
    REQUIRE_EQ(bis.getCurrentLine(),2u);
    REQUIRE_EQ(bis.getCurrentColumn(),1u);

    REQUIRE_EQ(bis.getChar(),'\n');                  // now actually consume LF
    REQUIRE_EQ(bis.getCurrentLine(),3u);             // line must increment *once*
    REQUIRE_EQ(bis.getCurrentColumn(),1u);

    REQUIRE_EQ(bis.getChar(),'B');                    // final char
    //bool savedhasPendingCR = hasPendingCR_;
    //hasPendingCR_ = savedhasPendingCR;
}

TEST_CASE(ReadWhileCompactionRegression) {
    std::string text(50,'x');                         // 50 'x' characters
    std::istringstream in(text);
    LXMLFormatter::BufferedInputStream bis(in,8);                    // small buffer triggers multiple compactions
    std::string out;
    bis.readWhile(out,[](int32_t ch){ return ch=='x'; });
    REQUIRE_EQ(out.size(), text.size());              // length must match
    REQUIRE_EQ(out, text);                            // content must be intact
}

TEST_CASE(PeekDoesNotConsumeRegression) {
    std::istringstream in("Z");
    LXMLFormatter::BufferedInputStream bis(in,1);
    REQUIRE_EQ(bis.peekChar(),'Z'); // first peek
    REQUIRE_EQ(bis.peekChar(),'Z'); // second peek – still 'Z'
    REQUIRE_EQ(bis.getChar(),'Z');  // now consume
    REQUIRE_EQ(bis.getChar(),-1);   // EOF after consumption
}