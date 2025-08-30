#ifndef BUFFERED_INPUT_STREAM_H
#define BUFFERED_INPUT_STREAM_H

#include <iostream>
#include <memory>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include "UTF8Handler.h"


namespace LXMLFormatter {

class BufferedInputStream {
public:
    enum class Encoding {
        UTF8,         // UTF-8 with BOM
        UTF8_NO_BOM,  // UTF-8 without BOM (most common)
        UTF16_LE,     // UTF-16 Little Endian
        UTF16_BE,     // UTF-16 Big Endian
        UTF32_LE,     // UTF-32 Little Endian
        UTF32_BE      // UTF-32 Big Endian
        // Note: Currently only UTF-8 variants are implemented
    };

    enum class StateError {
        None,
        ZeroBufferSize,
        BufferTooSmall,
        OutOfMemory
    };

    // Character constants to avoid signed/unsigned comparison issues
    static constexpr uint8_t LF = 0x0A;      // \n
    static constexpr uint8_t CR = 0x0D;      // \r
    static constexpr uint8_t SPACE = 0x20;   // space
    static constexpr uint8_t TAB = 0x09;     // \t
    static constexpr std::size_t kMaxBufferSize = (1ull << 28); //256MB buffer cap

    static std::unique_ptr<BufferedInputStream> Create(std::istream& stream, size_t bufferSize, StateError* err = nullptr);
    // Delete copy operations
    BufferedInputStream(const BufferedInputStream&) = delete;
    BufferedInputStream& operator=(const BufferedInputStream&) = delete;
    
    // Enable move constructor only (stream_ is a reference, can't be reseated)
    BufferedInputStream(BufferedInputStream&& other) noexcept;
    
    // Delete move assignment (can't reassign const members or references)
    BufferedInputStream& operator=(BufferedInputStream&&) = delete;

    // Reads the next Unicode scalar value as int32_t.
    // Returns: Unicode codepoint (>= 0)
    //          treats any invalid sequence as EOF (-1)
    // Note: UTF8 and UTF8_NO_BOM are handled identically (BOM already consumed)
    int32_t getChar();

    // Peeks the next code point without consuming it.
    // Returns -1 on EOF.
    int32_t peekChar();

    // Read UTF-8 string until condition is met
    template<typename Predicate>
    void readWhile(std::string& out, Predicate pred);

    // Read until delimiter (delimiter not included)
    void readUntil(std::string& out, char delimiter);

    // Skip whitespace (Unicode-aware)
    void skipWhitespace();

    bool eof() const;

    size_t getCurrentLine() const { return currentLine_; }
    size_t getCurrentColumn() const { return currentColumn_; }
    size_t getTotalBytesRead() const { return totalBytesRead_ - bomSize_; }
    Encoding getEncoding() const { return encoding_; }

    // Returns true if the instance has a live buffer and positive capacity.
    bool isValid() const noexcept;
    
private:
    BufferedInputStream(std::istream& stream, size_t bufferSize = 10 * 1024 * 1024);

    // Number of bytes available from bufferPos_ to bufferEnd_.
    inline std::size_t available() const noexcept { return bufferEnd_ - bufferPos_; }

    // Refills/compacts so that at least n bytes are available at bufferPos_.
    // Does not advance bufferPos_. Returns false on true EOF (no new bytes).
    bool ensureAtLeast(std::size_t n);

    // Advances bufferPos_ by 'width' bytes; updates line/column and CR/LF.
    void advance(std::size_t width) noexcept;

    // Initial read and BOM detection. Assumes buffer is empty on entry.
    void fillInitialBuffer();
    void detectEncoding();

    std::istream& stream_;
    size_t bufferSize_;
    std::unique_ptr<uint8_t[]> buffer_;
    size_t bufferPos_;
    size_t bufferEnd_;
    size_t currentLine_;
    size_t currentColumn_;
    size_t totalBytesRead_;
    Encoding encoding_;
    size_t bomSize_;
    bool hasPendingCR_;    // Track CR at buffer boundary for correct line counting
    bool    havePeek_;
    int32_t cachedCp_;     // next code point
    size_t  cachedWidth_;  // number of bytes it spans in the buffer
};


template<typename Predicate>
void BufferedInputStream::readWhile(std::string& out, Predicate pred) {
    if (!isValid()) return;

    for (;;) {
        if (bufferPos_ == bufferEnd_) {
            if (!ensureAtLeast(1)) break; // true EOF
        }

        // Remember start of contiguous window in the current buffer snapshot.
        const std::size_t start = bufferPos_;

        // Consume as long as predicate accepts the decoded code points,
        // but do not cross a refill boundary inside this inner loop.
        while (bufferPos_ < bufferEnd_) {
            auto r = UTF8Handler::decode(buffer_.get() + bufferPos_, available());
            if (r.status == UTF8Handler::DecodeStatus::NeedMore) break;         // refill outside
            if (r.status != UTF8Handler::DecodeStatus::Ok) break;               // stop on invalid
            if (!pred(r.cp)) break;                                // stop when predicate fails
            advance(r.width);                                      // consume bytes and update position
        }

        // Append what was consumed in this window.
        out.append(reinterpret_cast<const char*>(buffer_.get() + start),
                    static_cast<std::size_t>(bufferPos_ - start));

        // If we stopped because predicate failed or an invalid sequence was seen, exit.
        if (bufferPos_ < bufferEnd_) {
            auto r2 = UTF8Handler::decode(buffer_.get() + bufferPos_, available());
            if (r2.status == UTF8Handler::DecodeStatus::Ok && !pred(r2.cp)) break;
            if (r2.status == UTF8Handler::DecodeStatus::Invalid) break;
            // Otherwise r2 == NeedMore -> outer loop will refill.
        } else {
            // Buffer exhausted; loop will attempt to refill.
            continue;
        }
    }
}

} // namespace LXMLFormatter

#endif // BUFFERED_INPUT_STREAM_H