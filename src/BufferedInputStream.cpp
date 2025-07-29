#include "BufferedInputStream.h"

namespace LXMLFormatter {

BufferedInputStream::BufferedInputStream(std::istream& stream, size_t bufferSize)
    : stream_(stream)
    , bufferSize_(bufferSize)
    , buffer_(std::make_unique<uint8_t[]>(bufferSize))
    , bufferPos_(0)
    , bufferEnd_(0)
    , currentLine_(1)
    , currentColumn_(1)
    , totalBytesRead_(0)
    , encoding_(Encoding::UTF8_NO_BOM)
    , bomSize_(0)
    , hasPendingCR_(false) 
{
    fillBuffer();
    detectEncoding();
}

BufferedInputStream::BufferedInputStream(BufferedInputStream&& other) noexcept
    : stream_(other.stream_)
    , bufferSize_(other.bufferSize_)
    , buffer_(std::move(other.buffer_))
    , bufferPos_(other.bufferPos_)
    , bufferEnd_(other.bufferEnd_)
    , currentLine_(other.currentLine_)
    , currentColumn_(other.currentColumn_)
    , totalBytesRead_(other.totalBytesRead_)
    , encoding_(other.encoding_)
    , bomSize_(other.bomSize_)
    , hasPendingCR_(other.hasPendingCR_) 
{
    // Reset moved-from object
    other.bufferPos_ = 0;
    other.bufferEnd_ = 0;
    other.hasPendingCR_ = false;
}

int32_t BufferedInputStream::getChar() {
    if (encoding_ != Encoding::UTF8 && encoding_ != Encoding::UTF8_NO_BOM) {
        // For now, we only support UTF-8
        // TODO: Add UTF-16/32 support
        return -2;  // Encoding error
    }

    if (!ensureData(1)) {
        return -1;  // EOF
    }

    uint8_t first = buffer_[bufferPos_];
    
    // ASCII fast path
    if (first < 0x80) {
        advance(1);
        // XML forbids NUL character
        return (first == 0) ? -2 : first;
    }

    // Multi-byte UTF-8
    int bytes = 0;
    int32_t codepoint = 0;

    if ((first & 0xE0) == 0xC0) {
        bytes = 2;
        codepoint = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        bytes = 3;
        codepoint = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        bytes = 4;
        codepoint = first & 0x07;
    } else {
        // Invalid UTF-8 start byte
        advance(1);
        return -2;  // Encoding error
    }

    if (!ensureData(bytes)) {
        return -2;  // Incomplete sequence at EOF
    }

    // Validate and decode continuation bytes
    constexpr uint8_t CONTINUATION_MASK = 0xC0;
    constexpr uint8_t CONTINUATION_BITS = 0x80;
    
    for (int i = 1; i < bytes; i++) {
        uint8_t byte = buffer_[bufferPos_ + i];
        if ((byte & CONTINUATION_MASK) != CONTINUATION_BITS) {
            // Invalid continuation byte
            advance(1);
            return -2;  // Encoding error
        }
        codepoint = (codepoint << 6) | (byte & 0x3F);
    }

    // Validate codepoint range and check for overlong sequences
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        advance(1);
        return -2;  // Invalid codepoint
    }
    
    // Check for overlong sequences (security issue)
    if ((bytes == 2 && codepoint < 0x80) ||
        (bytes == 3 && codepoint < 0x800) ||
        (bytes == 4 && codepoint < 0x10000)) {
        advance(1);
        return -2;  // Overlong sequence
    }

    advance(bytes);
    return codepoint;
}

int32_t BufferedInputStream::peekChar() {
    size_t savedPos = bufferPos_;
    size_t savedLine = currentLine_;
    size_t savedColumn = currentColumn_;
    size_t savedBytes = totalBytesRead_;
    
    int32_t ch = getChar();
    
    // Restore position
    bufferPos_ = savedPos;
    currentLine_ = savedLine;
    currentColumn_ = savedColumn;
    totalBytesRead_ = savedBytes;
    
    return ch;
}

bool BufferedInputStream::readUntil(std::string& out, int32_t delimiter) {
    return readWhile(out, [delimiter](int32_t ch) { return ch != delimiter; });
}

void BufferedInputStream::skipWhitespace() {
    while (true) {
        int32_t ch = peekChar();
        if (ch < 0) break;  // EOF or error
        
        // XML whitespace: space, tab, CR, LF
        if (ch != SPACE && ch != TAB && ch != CR && ch != LF) {
            break;
        }
        getChar();
    }
}

bool BufferedInputStream::eof() const {
    return bufferPos_ >= bufferEnd_ && stream_.eof();
}

bool BufferedInputStream::ensureData(size_t bytes) {
    // Invariant: cannot request more bytes than buffer size
    if (bytes > bufferSize_) {
        return false;  // Prevent infinite loop
    }
    
    if (bufferPos_ + bytes <= bufferEnd_) {
        return true;
    }
    
    // Need to refill buffer
    // Optimization: only move data if we've consumed more than 1/4 of buffer
    // This reduces average copy size from bufferSize_/2 to bufferSize_/8
    // For a 10MB buffer, this saves ~4MB of copying on average per refill
    if (bufferPos_ > bufferSize_ / 4 && bufferEnd_ - bufferPos_ > 0) {
        // Move remaining bytes to start
        size_t remaining = bufferEnd_ - bufferPos_;
        std::memmove(buffer_.get(), buffer_.get() + bufferPos_, remaining);
        bufferEnd_ = remaining;
        bufferPos_ = 0;
    } else if (bufferPos_ >= bufferEnd_) {
        // Buffer fully consumed
        bufferPos_ = 0;
        bufferEnd_ = 0;
    }
    // else: keep data where it is and read into remaining space
    
    // Fill available buffer space
    size_t readPos = bufferEnd_;
    size_t readSize = bufferSize_ - bufferEnd_;
    
    stream_.read(reinterpret_cast<char*>(buffer_.get() + readPos), readSize);
    size_t bytesRead = stream_.gcount();
    bufferEnd_ += bytesRead;
    
    return bufferPos_ + bytes <= bufferEnd_;
}

void BufferedInputStream::advance(size_t bytes) {
    size_t consumed = bytes;  // Track total bytes to consume
    
    for (size_t i = 0; i < consumed; i++) {
        if (bufferPos_ < bufferEnd_) {
            uint8_t c = buffer_[bufferPos_];
            bufferPos_++;
            totalBytesRead_++;
            
            // Handle pending CR from previous buffer
            if (hasPendingCR_ && c == LF) {
                // This LF completes a CR-LF pair; don't increment line again
                hasPendingCR_ = false;
                currentColumn_ = 1;
                continue;
            }
            hasPendingCR_ = false;
            
            // Update line/column tracking
            if (c == LF) {
                currentLine_++;
                currentColumn_ = 1;
            } else if (c == CR) {
                currentLine_++;
                currentColumn_ = 1;
                
                // Check if next char is LF
                if (bufferPos_ < bufferEnd_ && buffer_[bufferPos_] == LF) {
                    // Consume the LF as part of CR-LF
                    bufferPos_++;
                    totalBytesRead_++;
                    consumed++;  // Adjust total to account for extra byte
                } else if (bufferPos_ >= bufferEnd_) {
                    // CR at buffer boundary - need to check after refill
                    hasPendingCR_ = true;
                }
            } else {
                // For UTF-8, only count the start of a character
                constexpr uint8_t UTF8_CONTINUATION_MASK = 0xC0;
                constexpr uint8_t UTF8_CONTINUATION_BITS = 0x80;
                if ((c & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_BITS) {
                    currentColumn_++;
                }
            }
        }
    }
}

bool BufferedInputStream::fillBuffer() {
    if (!stream_.good()) {
        return false;
    }

    stream_.read(reinterpret_cast<char*>(buffer_.get()), bufferSize_);
    size_t bytesRead = stream_.gcount();
    
    if (bytesRead == 0) {
        return false;
    }

    bufferPos_ = 0;
    bufferEnd_ = bytesRead;
    return true;
}

void BufferedInputStream::detectEncoding() {
    if (bufferEnd_ < 4) {
        encoding_ = Encoding::UTF8_NO_BOM;  // Default
        return;
    }

    // BOM detection constants
    constexpr uint8_t UTF8_BOM_1 = 0xEF;
    constexpr uint8_t UTF8_BOM_2 = 0xBB;
    constexpr uint8_t UTF8_BOM_3 = 0xBF;
    constexpr uint8_t UTF16_BE_BOM_1 = 0xFE;
    constexpr uint8_t UTF16_BE_BOM_2 = 0xFF;
    constexpr uint8_t UTF16_LE_BOM_1 = 0xFF;
    constexpr uint8_t UTF16_LE_BOM_2 = 0xFE;

    // Check for BOM
    if (buffer_[0] == UTF8_BOM_1 && buffer_[1] == UTF8_BOM_2 && buffer_[2] == UTF8_BOM_3) {
        encoding_ = Encoding::UTF8;
        bomSize_ = 3;
        bufferPos_ = 3;
        totalBytesRead_ = 3;
    } else if (buffer_[0] == UTF16_BE_BOM_1 && buffer_[1] == UTF16_BE_BOM_2) {
        encoding_ = Encoding::UTF16_BE;
        bomSize_ = 2;
        bufferPos_ = 2;
        totalBytesRead_ = 2;
    } else if (buffer_[0] == UTF16_LE_BOM_1 && buffer_[1] == UTF16_LE_BOM_2) {
        if (bufferEnd_ >= 4 && buffer_[2] == 0x00 && buffer_[3] == 0x00) {
            encoding_ = Encoding::UTF32_LE;
            bomSize_ = 4;
            bufferPos_ = 4;
            totalBytesRead_ = 4;
        } else {
            encoding_ = Encoding::UTF16_LE;
            bomSize_ = 2;
            bufferPos_ = 2;
            totalBytesRead_ = 2;
        }
    } else {
        // No BOM - default to UTF-8
        encoding_ = Encoding::UTF8_NO_BOM;
        bomSize_ = 0;
    }
}

} // namespace LXMLFormatter