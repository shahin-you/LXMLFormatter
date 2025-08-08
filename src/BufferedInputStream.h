#ifndef BUFFERED_INPUT_STREAM_H
#define BUFFERED_INPUT_STREAM_H

#include <iostream>
#include <memory>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdint>

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
        OutOfMemory,
        IoError,
    };

    // Character constants to avoid signed/unsigned comparison issues
    static constexpr uint8_t LF = 0x0A;      // \n
    static constexpr uint8_t CR = 0x0D;      // \r
    static constexpr uint8_t SPACE = 0x20;   // space
    static constexpr uint8_t TAB = 0x09;     // \t

    static std::unique_ptr<BufferedInputStream> Create(std::istream& stream, size_t bufferSize, StateError* err = nullptr);
    // Delete copy operations
    BufferedInputStream(const BufferedInputStream&) = delete;
    BufferedInputStream& operator=(const BufferedInputStream&) = delete;
    
    // Enable move constructor only (stream_ is a reference, can't be reseated)
    BufferedInputStream(BufferedInputStream&& other) noexcept;
    
    // Delete move assignment (can't reassign const members or references)
    BufferedInputStream& operator=(BufferedInputStream&&) = delete;

    // Get next UTF-8 character 
    // Returns: Unicode codepoint (>= 0), -1 for EOF, -2 for encoding error
    // Note: XML forbids NUL (U+0000), so we return -2 if encountered
    // Note: UTF8 and UTF8_NO_BOM are handled identically (BOM already consumed)
    int32_t getChar();

    // Peek at next character without consuming
    int32_t peekChar();

    // Read UTF-8 string until condition is met
    template<typename Predicate>
    bool readWhile(std::string& out, Predicate pred);

    // Read until delimiter (delimiter not included)
    bool readUntil(std::string& out, int32_t delimiter);

    // Skip whitespace (Unicode-aware)
    void skipWhitespace();

    bool eof() const;

    size_t getCurrentLine() const { return currentLine_; }
    size_t getCurrentColumn() const { return currentColumn_; }
    size_t getTotalBytesRead() const { return totalBytesRead_ - bomSize_; }
    Encoding getEncoding() const { return encoding_; }

    bool isValid() const noexcept;

private:
    bool ensureData(size_t bytes);
    void advance(size_t bytes);
    bool fillBuffer();
    void detectEncoding();
    BufferedInputStream(std::istream& stream, size_t bufferSize = 10 * 1024 * 1024);

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
    bool hasPendingCR_;  // Track CR at buffer boundary for correct line counting
};


template<typename Predicate>
bool BufferedInputStream::readWhile(std::string& out, Predicate pred) {
    out.clear();
    
    while (true) {
        int32_t ch = peekChar();
        if (ch < 0 || !pred(ch)) {  // EOF or error stops reading
            break;
        }
        
        // Actually consume the character and append its UTF-8 bytes
        size_t startPos = bufferPos_;
        getChar();
        size_t endPos = bufferPos_;
        
        for (size_t i = startPos; i < endPos; i++) {
            out.push_back(static_cast<char>(buffer_[i]));
        }
    }
    
    return !out.empty();
}

} // namespace LXMLFormatter

#endif // BUFFERED_INPUT_STREAM_H