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
    , havePeek_(false)
    , cachedCp_(-1)
    , cachedWidth_(0) 
{
    fillInitialBuffer();
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
    , havePeek_(other.havePeek_)
    , cachedCp_(other.cachedCp_)
    , cachedWidth_(other.cachedWidth_) 
{
    // Reset moved-from object
        other.buffer_.reset();
        other.bufferSize_     = 0;
        other.bufferPos_      = 0;
        other.bufferEnd_      = 0;
        other.currentLine_    = 1;
        other.currentColumn_  = 1;
        other.totalBytesRead_ = 0;
        other.bomSize_        = 0;
        other.hasPendingCR_   = false;
        other.havePeek_       = false;
        other.cachedCp_       = -1;
        other.cachedWidth_    = 0;
}

int32_t BufferedInputStream::getChar() {
    if (!isValid()) return -1;

    if (havePeek_) {
        havePeek_ = false;
        const int32_t cp = cachedCp_;
        advance(cachedWidth_);
        return cp;
    }

    if (!ensureAtLeast(1)) return -1; // true EOF

    auto r = UTF8Handler::decode(buffer_.get() + bufferPos_, available());
    if (r.status == UTF8Handler::DecodeStatus::NeedMore) {
        if (!ensureAtLeast(r.width)) 
            return -1; // premature EOF
        r = UTF8Handler::decode(buffer_.get() + bufferPos_, available());
    }
    if (r.status != UTF8Handler::DecodeStatus::Ok) {
        // Policy: treat invalid sequences as EOF for now.
        return -1;
    }

    advance(r.width);
    return r.cp;
}

int32_t BufferedInputStream::peekChar() {
    if (!isValid()) return -1;
    if (havePeek_)  return cachedCp_;

    if (!ensureAtLeast(1)) return -1; // true EOF

    auto r = UTF8Handler::decode(buffer_.get() + bufferPos_, available());
    if (r.status == UTF8Handler::DecodeStatus::NeedMore) {
        if (!ensureAtLeast(r.width)) return -1; // premature EOF
        r = UTF8Handler::decode(buffer_.get() + bufferPos_, available());
    }
    if (r.status != UTF8Handler::DecodeStatus::Ok) {
        // Policy: treat invalid sequences as EOF for now.
        return -1;
    }

    cachedCp_    = r.cp;
    cachedWidth_ = r.width;
    havePeek_    = true;
    return cachedCp_;
}

void BufferedInputStream::readUntil(std::string& out, char delimiter) {
    out.clear();
    const int32_t d = static_cast<int32_t>(delimiter);
    readWhile(out, [d](int32_t ch) { return ch != d; });
    // delimiter (if present) remains unconsumed; caller can bis.getChar() to consume it.
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
    return available()==0 && stream_.eof();
}

void BufferedInputStream::advance(std::size_t width) noexcept {
    // Consumes 'width' bytes while maintaining CR/LF and line/column.
    for (std::size_t i = 0; i < width; ++i) {
        if (bufferPos_ >= bufferEnd_) break;

        const uint8_t ch = buffer_[bufferPos_++];
        ++totalBytesRead_;

        if (ch == '\r') {
            ++currentLine_;
            currentColumn_ = 1;
            hasPendingCR_  = true;
        } else if (ch == '\n') {
            if (hasPendingCR_) {
                // CRLF sequence: line already advanced on CR. keep column at 1.
                hasPendingCR_ = false;
            } else {
                ++currentLine_;
                currentColumn_ = 1;
            }
        } else {
            ++currentColumn_;
            hasPendingCR_ = false;
        }
    }
}

void BufferedInputStream::fillInitialBuffer() {
    bufferPos_ = bufferEnd_ = 0;
    if (!stream_) return;

    stream_.read(reinterpret_cast<char*>(buffer_.get()),
                 static_cast<std::streamsize>(bufferSize_));
    std::streamsize n = stream_.gcount();
    if (n > 0) {
        bufferPos_ = 0;
        bufferEnd_ = static_cast<std::size_t>(n);
    }
}


void BufferedInputStream::detectEncoding() {
    // Only UTF-8/UTF-8 BOM supported; if BOM present, skip it.
    if (bufferEnd_ - bufferPos_ >= 3) {
        const uint8_t* p = buffer_.get() + bufferPos_;
        if (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
            encoding_ = Encoding::UTF8;
            bomSize_  = 3;

            // Skip BOM WITHOUT affecting line/column or CR state.
            bufferPos_      += 3;
            totalBytesRead_ += 3;

            // Any previously cached peek is now stale.
            havePeek_    = false;
            cachedCp_    = -1;
            cachedWidth_ = 0;
            return;
        }   
    } 
}

bool BufferedInputStream::isValid() const noexcept 
{
    // no need to check stream_.good() since it is not related to object validity, it is data state. 
    return  buffer_ != nullptr && bufferSize_ > 0;
}

std::unique_ptr<BufferedInputStream> BufferedInputStream::Create(std::istream& stream,
                            size_t bufferSize,
                            StateError* err)
{
    if (bufferSize == 0) {
        if (err) *err = StateError::ZeroBufferSize;
        return nullptr;
    }

    if (bufferSize < 4) { // Buffer must be at least 4 bytes so we can read a full UTF-8 character
        if (err) *err = StateError::BufferTooSmall;
        return nullptr;
    }

    //we can't trust try/catch to catch huge allocations since ASan would catch them before any new happens
    if (bufferSize > kMaxBufferSize) {
        if (err) *err = StateError::OutOfMemory;
        return nullptr;
    }

    try {
        auto p = std::unique_ptr<BufferedInputStream>(
            new BufferedInputStream(stream, bufferSize));
        if (err) *err = StateError::None;
        return p;
    } catch (const std::bad_alloc&) {
        if (err) *err = StateError::OutOfMemory;
        return nullptr;
    }
}

bool BufferedInputStream::ensureAtLeast(std::size_t n)
{
    if (!isValid()) return false;
    if (available() >= n) return true;

    /* Compact existing unread bytes to the front if needed/possible.
       Most OS reads want a single contiguous destination. If we keep the unread 
       data in the middle of the buffer, there might be too little space at the 
       tail to pull more bytes especially with small buffers so we’d be forced to
       allocate/grow or to implement a ring buffer which we don't. ring buffer
       makes every pointer math a two-step process (modulo buffer size).
    */
    if (bufferPos_ > 0 && bufferPos_ < bufferEnd_) {
        const std::size_t unread = bufferEnd_ - bufferPos_;
        std::memmove(buffer_.get(), buffer_.get() + bufferPos_, unread);
        bufferPos_ = 0;
        bufferEnd_ = unread;
    } else if (bufferPos_ == bufferEnd_) {
        bufferPos_ = bufferEnd_ = 0;
    }

    // Read until either enough bytes are available or no more data arrives.
    while (available() < n) {
        if (!stream_) break;
        const std::size_t room = bufferSize_ - bufferEnd_;
        if (room == 0) break; // no space left; caller asked for > bufferSize_
        stream_.read(reinterpret_cast<char*>(buffer_.get() + bufferEnd_),
                     static_cast<std::streamsize>(room));
        const std::streamsize got = stream_.gcount();
        if (got <= 0) break;
        bufferEnd_ += static_cast<std::size_t>(got);
    }

    return available() >= n;
}


} // namespace LXMLFormatter