#include "UnitTestFramework.h"
#include "BufferedInputStream.h"
#include <limits>

TEST_CASE(Factory_Create_EdgeCases){
    using BIS = LXMLFormatter::BufferedInputStream;
    using S   = BIS::StateError;

    // --- Zero buffer size -> nullptr + ZeroBufferSize
    {
        std::istringstream in("X");
        S err = S::None;
        auto p = BIS::Create(in, 0, &err);
        REQUIRE(!p);
        REQUIRE(err == S::ZeroBufferSize);
    }

    // --- Too small (1..3) -> nullptr + BufferTooSmall
    for (std::size_t sz = 1; sz < 4; ++sz) {
        std::istringstream in("X");
        S err = S::None;
        auto p = BIS::Create(in, sz, &err);
        REQUIRE(!p);
        REQUIRE(err == S::BufferTooSmall);
    }

    // --- Minimum valid size (4) -> non-null + None; object usable
    {
        std::istringstream in("ab");
        S err = S::BufferTooSmall; // prime with non-None to ensure it gets reset
        auto p = BIS::Create(in, 4, &err);
        REQUIRE(p);
        REQUIRE(err == S::None);
        REQUIRE_EQ(p->getChar(), 'a');
        REQUIRE_EQ(p->getChar(), 'b');
        REQUIRE_EQ(p->getChar(), -1);
    }

    // --- Larger valid size -> non-null + None; still usable
    {
        std::istringstream in("hi");
        S err = S::None;
        auto p = BIS::Create(in, 4096, &err);
        REQUIRE(p);
        REQUIRE(err == S::None);
        REQUIRE_EQ(p->getChar(), 'h');
        REQUIRE_EQ(p->getChar(), 'i');
        REQUIRE_EQ(p->getChar(), -1);
    }

    // --- Null error pointer: should still return nullptr on invalid size without crashing
    {
        std::istringstream in("X");
        auto p = BIS::Create(in, 0, nullptr);
        REQUIRE(!p);
    }
    {
        std::istringstream in("X");
        auto p = BIS::Create(in, 4, nullptr);
        REQUIRE(p);
    }

    // --- Best-effort OutOfMemory: request an absurdly large buffer
    // Note: new[] should throw (bad_alloc / bad_array_new_length), which is mapped to OutOfMemory.
    {
        std::istringstream in(""); // data content irrelevant here
        S err = S::None;
        const std::size_t huge = std::numeric_limits<std::size_t>::max() / 2;
        auto p = BIS::Create(in, huge, &err);
        REQUIRE(!p);
        REQUIRE(err == S::OutOfMemory);
    }
}

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
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 4);

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
    REQUIRE_EQ(bis->getCurrentLine(),2u);             // line must increment *once*
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
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 4);

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
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 4);

    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }
    std::string out;
    bis->readWhile(out,[](int32_t ch){ return ch != -1; });
    REQUIRE_EQ(out,"hello");
}

TEST_CASE(MultibyteLeadAtBufferEndCompactionWorks){
    // Content crafted so after consuming 'X','Y', the 'π' lead byte is last in the window.
    std::string s = std::string("XY") + u8"π" + "Z"; // 'π' = 2 bytes
    std::istringstream in(s);
    auto bis = LXMLFormatter::BufferedInputStream::Create(in, 4);

    REQUIRE_NE(bis, nullptr);
    if (!bis) {
        return;
    }

    REQUIRE_EQ(bis->getChar(), 'X');
    REQUIRE_EQ(bis->getChar(), 'Y');

    // Now the buffer likely ends on the lead byte of 'π'.
    REQUIRE_EQ(bis->peekChar(), 0x03C0);    // must succeed: compaction + one more read
    REQUIRE_EQ(bis->getChar(), 0x03C0);     // then consume it
    REQUIRE_EQ(bis->getChar(), 'Z');
    REQUIRE_EQ(bis->getChar(), -1);
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

TEST_CASE(MoveCtorPreservesState) {
    std::istringstream in("A\nB");
    auto p = LXMLFormatter::BufferedInputStream::Create(in, 4);
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
    auto p = LXMLFormatter::BufferedInputStream::Create(in, 4);
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