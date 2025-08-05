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

