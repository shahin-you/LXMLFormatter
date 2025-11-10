#include "XMLTokenizer.h"
#include "XMLTokenizerTypes.h"

namespace LXMLFormatter {

static constexpr char defaultErrorMessage[] = "Tokenizer error";

//a note on the anonymous namespace: this is used to define helper functions that are only visible within 
// this .cpp file. This helps to avoid name collisions and keeps the global namespace clean.
//so no symbol exported outside this file
namespace {
    // Upper-bound clamp for ByteLen (U32).
    inline ByteLen clampBL(ByteLen v, ByteLen cap) noexcept {
        return (v > cap) ? cap : v;
    }
}

// Temporary: map severity to a short label for stderr,
static inline const char* sevLabel(ErrorSeverity s) noexcept {
    switch (s) {
        case ErrorSeverity::Warning:     return "warning";
        case ErrorSeverity::Recoverable: return "recoverable";
        case ErrorSeverity::Fatal:       return "fatal";
    }
    return "unknown";
}

/*
clamp limits to caps.
don’t allocate in the noexcept constructor to avoid std::terminate on OOM.
initialize state/flags so the first nextToken() call emits DocumentStart.
tie the freelist block size to the (sanitized) maxPerTagBytes for later reuse.
*/
XMLTokenizer::XMLTokenizer(BufferedInputStream& in,
                           TokenizerOptions opts,
                           TokenizerLimits lims) noexcept
    : in_(in), opts_(opts), lims_(lims) 
{
    // Clamp soft limits to absolute caps (defensive, prevents misconfig/DoS).
    lims_.maxNameBytes      = clampBL(lims_.maxNameBytes,      Caps::AbsMaxNameBytes);
    lims_.maxAttrValueBytes = clampBL(lims_.maxAttrValueBytes, Caps::AbsMaxAttrValueBytes);
    lims_.maxTextRunBytes   = clampBL(lims_.maxTextRunBytes,   Caps::AbsMaxTextRunBytes);
    lims_.maxCommentBytes   = clampBL(lims_.maxCommentBytes,   Caps::AbsMaxCommentBytes);
    lims_.maxCdataBytes     = clampBL(lims_.maxCdataBytes,     Caps::AbsMaxCdataBytes);
    lims_.maxDoctypeBytes   = clampBL(lims_.maxDoctypeBytes,   Caps::AbsMaxDoctypeBytes);
    lims_.maxPerTagBytes    = clampBL(lims_.maxPerTagBytes,    Caps::AbsMaxPerTagBytes);

    // Core state/flags (Started/Ended bits are clear).
    state_       = State::Content;
    flags_.bits  = 0;

    // No allocations in a noexcept ctor; start with empty containers.
    tagStack_.clear();
    tagBufFreelist_.clear();
    errors_.clear();
    errorArena_.clear();
    textArena_.buf.clear();
#if defined(LXML_DEBUG_SLICES)
    textArena_.generation = 1;
#endif
    la_.has       = false;
    pendingStart_ = SourcePosition{};
    pendingStartValid_ = false;
    
    // Bind freelist block size to current per-tag capacity.
    freelistBlockSize_ = lims_.maxPerTagBytes;

    // stats_ is trivially zero-initialized (enabled build collects later).
}


// Intern error message into errorArena_ as a null-terminated C string.
// Returns a stable pointer (Phase-1 fail-fast makes this safe).
const char* XMLTokenizer::internError(const char* msg, U32 len) noexcept {
    const bool   useDefault = (msg == nullptr);
    const char*  src        = useDefault ? defaultErrorMessage : msg;
    const U32    used       = useDefault ? static_cast<U32>(sizeof(defaultErrorMessage) - 1) : len;

    const size_t need = errorArena_.size() + static_cast<size_t>(used) + 1; // +NULL
    if (errorArena_.capacity() < need) {
        size_t newCap = errorArena_.capacity() ? errorArena_.capacity() * 2 : 256;
        if (newCap < need) newCap = need;
        errorArena_.reserve(newCap);
    }

    const size_t base = errorArena_.size();
    errorArena_.resize(base + used + 1);
    std::memcpy(&errorArena_[base], src, used);
    errorArena_[base + used] = '\0';
    return errorArena_.data() + base;
}


bool XMLTokenizer::emitError(XMLToken& out,
                             TokenizerErrorCode code,
                             ErrorSeverity sev,
                             const char* msg,
                             U32 msgLen) noexcept
{
    // Prefer token-start position if set; otherwise, current cursor.
    const SourcePosition where = pendingStartValid_ ? pendingStart_ : currentPosition();
    pendingStartValid_ = false; // avoid accidental reuse

    const bool useDefault = (msg == nullptr);
    const U32  effLen     = useDefault ? static_cast<U32>(sizeof(defaultErrorMessage) - 1) : msgLen;

    // Intern message and fill the immediate Error token.
    const char* stableMsg = internError(msg, effLen);

    out.type       = XMLTokenType::Error;
    out.data       = stableMsg;
    out.length     = effLen;
    out.byteOffset = where.byteOffset;
    out.line       = where.line;
    out.column     = where.column;
#if defined(LXML_DEBUG_SLICES)
    out.arena      = ArenaId::Error;
    out.generation = 0;
#endif

    // Record into errors_ with std::string_view (no copies/allocs).
    TokenizerError rec;
    rec.code  = code;
    rec.sev   = sev;
    rec.where = where;
    rec.msg   = std::string_view(stableMsg, effLen);
    errors_.push_back(rec);

    // Temporary stderr logging (will be replaced by callbacks).
    std::fprintf(stderr,
                 "[LXMLTokenizer %s] code=0x%04X at byte=%llu line=%u col=%u: %.*s\n",
                 sevLabel(sev),
                 static_cast<unsigned>(code),
                 static_cast<unsigned long long>(where.byteOffset),
                 static_cast<unsigned>(where.line),
                 static_cast<unsigned>(where.column),
                 static_cast<int>(effLen),
                 stableMsg);

    // Phase-1: end stream on fatal errors (next nextToken() returns false).
    if (sev == ErrorSeverity::Fatal) {
        flags_.set(TokenizerFlags::Ended);
    }
    return true; // One token (Error) emitted
}


// Text tokens: lifetime = until next nextToken()
bool XMLTokenizer::makeTextToken(XMLToken& out, U32 off, U32 len) noexcept {
    // Use the marked start if available; otherwise snapshot current cursor.
    const SourcePosition where = pendingStartValid_ ? pendingStart_ : currentPosition();
    pendingStartValid_ = false;

    out.type       = XMLTokenType::Text;
    out.data       = (len != 0) ? (const char*)(textArena_.buf.data() + off) : nullptr;
    out.length     = len;
    out.byteOffset = where.byteOffset;
    out.line       = where.line;
    out.column     = where.column;

#if defined(LXML_DEBUG_SLICES)
    out.arena      = ArenaId::Text;
    out.generation = textArena_.generation;
#endif
    return true; // one token emitted
}

bool XMLTokenizer::scanText(XMLToken& out) {
    int32_t cp = peekCp();
    
    // If we see '<' immediately, transition to TagOpen without emitting
    if (cp == '<') {
        state_ = State::TagOpen;
        return false;  // No token, state changed
    }
    
    // If EOF, let trampoline handle it
    if (cp < 0) {
        return false;
    }
    
    // Start accumulating text
    textArena_.buf.clear();
    markTokenStart();  // Capture position before consuming first byte
    
    while (true) {
        cp = peekCp();
        if (cp == '<' || cp < 0) {
            break;  // Stop at '<' or EOF, don't consume
        }
        
        getCp();  // Consume the code point
        
        // Handle CRLF normalization (only in Content, not markup)
        if (opts_.normalizeLineEndings() && cp == '\r') {
            // Check for CRLF sequence
            if (peekCp() == '\n') {
                getCp();  // Consume the LF too
            }
            // Emit single LF for both CR and CRLF
            textArena_.buf.push_back('\n');
        } else {
            // Encode Unicode code point back to UTF-8 bytes
            uint8_t utf8Bytes[4];  // Max UTF-8 sequence length
            auto encResult = UTF8Handler::encode(static_cast<uint32_t>(cp), utf8Bytes, 4);
            
            if (encResult.status != UTF8Handler::EncodeStatus::Ok) {
                // This should never happen for valid code points from BufferedInputStream
                return emitError(out, TokenizerErrorCode::InvalidUTF8,
                               ErrorSeverity::Fatal, "Failed to encode code point", 25);
            }
            
            // Append the UTF-8 bytes
            for (uint8_t i = 0; i < encResult.width; ++i) {
                textArena_.buf.push_back(static_cast<char>(utf8Bytes[i]));
            }
        }
        
        // Check limit using proper types (both as size_t)
        if (textArena_.buf.size() >= static_cast<size_t>(lims_.maxTextRunBytes)) {
            flags_.set(TokenizerFlags::Ended);
            return emitError(out, TokenizerErrorCode::LimitExceeded,
                           ErrorSeverity::Fatal, "Text run exceeds limit", 23);
        }
    }
    
    // Emit text token (points into textArena_.buf)
    makeTextToken(out, 0, static_cast<U32>(textArena_.buf.size()));
    // state_ remains Content
    return true;  // Token emitted
}

void XMLTokenizer::skipXMLSpace() {
    while (true) {
        int32_t ch = peekCp();
        if (ch < 0 /*EOF*/|| !CharClass::isXMLWhitespace(static_cast<uint32_t>(ch)))
            break;
        getCp(); // consume
    }
}

// Guarantees at least 'need' free bytes in current TagBuffer.
// Grows buffer geometrically up to per-tag cap; reuses freelist if size matches.
bool XMLTokenizer::ensureCurrentTagCapacity(ByteLen need) {
    if (tagStack_.empty()) 
        return false; // no current tag buffer

    TagFrame& frame = tagStack_.back();
    if (need <= (frame.buf.cap - frame.buf.used)) {
        return true; // already enough space
    }

    // Need to grow. Check against limits first.
    if (need > lims_.maxPerTagBytes) {
        flags_.set(TokenizerFlags::Ended);
        return false; // would exceed per-tag limit
    }

    // Compute new capacity: double current or just enough, capped.
    ByteLen newCap = frame.buf.cap ? frame.buf.cap * 2 : 256;
    if (newCap < frame.buf.used + need) {
        newCap = frame.buf.used + need;
    }
    if (newCap > lims_.maxPerTagBytes) {
        newCap = lims_.maxPerTagBytes;
    }

    // Try to reuse a freelist buffer of the right size.
    if (!tagBufFreelist_.empty() && freelistBlockSize_ == newCap) {
        frame.buf.mem = std::move(tagBufFreelist_.back());
        tagBufFreelist_.pop_back();
        frame.buf.cap = newCap;
        return true;
    }

    // Allocate a new buffer.
    try {
        std::unique_ptr<char[]> newMem(new char[newCap]);
        if (frame.buf.mem) {
            // Copy existing data to new buffer.
            std::memcpy(newMem.get(), frame.buf.mem.get(), frame.buf.used);
        }
        frame.buf.mem = std::move(newMem);
        frame.buf.cap = newCap;
        return true;
    } catch (const std::bad_alloc&) {
        flags_.set(TokenizerFlags::Ended);
        return false; // allocation failed
    }
}
// Writes 'len' bytes to current TagBuffer and returns starting offset on success.
// Returns kBadOff on failure
U32 XMLTokenizer::appendToCurrentTagBuf(const char* data, U32 len) {
    if (tagStack_.empty()) 
        return kBadOff; // no current tag buffer

    TagFrame& frame = tagStack_.back();
    if (!ensureCurrentTagCapacity(len)) return kBadOff;

    const U32 off = frame.buf.used;
    std::memcpy(frame.buf.mem.get() + off, data, len);
    frame.buf.used += len;
    return off;
}

// Emits the synthetic DocumentStart token (only once per stream).
// Emits a fatal error if called after Started flag already set.
bool XMLTokenizer::emitDocumentStart(XMLToken& out) noexcept {
    if (flags_.test(TokenizerFlags::Started)) {
        const char* msg = "DocumentStart already emitted";
        return emitError(out, TokenizerErrorCode::DuplicateDocumentBoundary, 
            ErrorSeverity::Fatal, msg,
            static_cast<U32>(std::strlen(msg)));
    }

    flags_.set(TokenizerFlags::Started);
    
    const SourcePosition pos = currentPosition();
    
    out.type       = XMLTokenType::DocumentStart;
    out.data       = nullptr;
    out.length     = 0;
    out.byteOffset = pos.byteOffset;
    out.line       = pos.line;
    out.column     = pos.column;

#if defined(LXML_DEBUG_SLICES)
    out.arena      = ArenaId::None;
    out.generation = 0;
#endif
    pendingStartValid_ = false;
    return true;
}

// Emits DocumentEnd or a fatal error if unclosed tags remain.
// Sets Ended flag to prevent further token emission attempts.
bool XMLTokenizer::emitDocumentEnd(XMLToken& out) noexcept {
    if (flags_.test(TokenizerFlags::Ended)) {
        return false;  // Already ended
    }
    
    // Fatal error: unclosed tags
    if (!tagStack_.empty()) {
        flags_.set(TokenizerFlags::Ended);
        const char* msg = "Unclosed tag at end of document";
        return emitError(out,
            TokenizerErrorCode::UnexpectedEOF,
            ErrorSeverity::Fatal,
            msg,
            static_cast<U32>(std::strlen(msg)));
    }

    flags_.set(TokenizerFlags::Ended);
    
    const SourcePosition pos = currentPosition();
    
    out.type       = XMLTokenType::DocumentEnd;
    out.data       = nullptr;
    out.length     = 0;
    out.byteOffset = pos.byteOffset;
    out.line       = pos.line;
    out.column     = pos.column;

#if defined(LXML_DEBUG_SLICES)
    out.arena      = ArenaId::None;
    out.generation = 0;
#endif
    pendingStartValid_ = false;
    return true;
}

// Allocate or reuse a TagBuffer for the current frame.
// Always allocate full-size per-tag buffer, no growth is happening here.
bool XMLTokenizer::ensureCurrentTagBuffer() noexcept {
    if (tagStack_.empty())
        return false;
    
    // Guard against misconfiguration
    if (lims_.maxPerTagBytes == 0) {
        flags_.set(TokenizerFlags::Ended);
        return false;
    }
    
    TagFrame& frame = tagStack_.back();
    
    // Already has buffer
    if (frame.buf.mem) {
        noteTagArena(frame.buf.cap);
        return true;
    }
    
    // Try freelist first
    if (!tagBufFreelist_.empty()) {
        frame.buf.mem = std::move(tagBufFreelist_.back());
        tagBufFreelist_.pop_back();
        frame.buf.cap = lims_.maxPerTagBytes;
        frame.buf.used = 0;
        noteTagArena(frame.buf.cap);
        return true;
    }
    
    // Allocate new buffer at full size
    try {
        frame.buf.mem.reset(new char[lims_.maxPerTagBytes]);
        frame.buf.cap = lims_.maxPerTagBytes;
        frame.buf.used = 0;
        noteTagArena(frame.buf.cap);
        return true;
    } catch (const std::bad_alloc&) {
        flags_.set(TokenizerFlags::Ended);
        return false;
    }
}

// Push a new TagFrame onto the stack.
bool XMLTokenizer::pushTagFrame() {
    if (tagStack_.size() >= lims_.maxOpenDepth) {
        flags_.set(TokenizerFlags::Ended);
        // Optional: emit fatal depth error token
        XMLToken dummy;
        const char* msg = "Maximum tag nesting depth exceeded";
        emitError(dummy, TokenizerErrorCode::LimitExceeded,
                  ErrorSeverity::Fatal,
                  msg,
                  static_cast<U32>(std::strlen(msg)));
        return false;
    }

    const SourcePosition pos = currentPosition();

    tagStack_.emplace_back();
    TagFrame& frame = tagStack_.back();
    frame.startPos = pos;
    frame.ctx.startLine       = pos.line;
    frame.ctx.startColumn     = pos.column;
    frame.ctx.startByteOffset = pos.byteOffset;

#if defined(LXML_ENABLE_STATS)
    stats_.maxOpenDepth = std::max(stats_.maxOpenDepth, static_cast<U32>(tagStack_.size()));
#endif

    // Buffer allocation deferred to ensureCurrentTagBuffer()
    return true;
}

// Pop the current TagFrame, recycling its buffer if possible.
void XMLTokenizer::popTagFrame() noexcept {
    if (tagStack_.empty())
        return;
    
    // Move frame out first, then pop
    TagFrame frame = std::move(tagStack_.back());
    tagStack_.pop_back();
    
    // Now work with local copy
    constexpr ByteLen kFreelistMemoryBudget = 64u * 1024u * 1024u;
    const size_t maxFreelistSize = std::max(
        size_t{4},
        static_cast<size_t>(kFreelistMemoryBudget / std::max<ByteLen>(1, lims_.maxPerTagBytes))
    );
    
    if (frame.buf.mem &&
        frame.buf.cap == lims_.maxPerTagBytes &&
        tagBufFreelist_.size() < maxFreelistSize) {
        tagBufFreelist_.push_back(std::move(frame.buf.mem));
    }
    // frame destructor runs here (handles any non-cached buffer cleanup)
}

bool XMLTokenizer::makeTagToken(XMLToken& out, XMLTokenType type, U32 offset, U32 length) noexcept {
    // Caller MUST ensure offset and length are valid:
    //   - offset < frame.buf.used
    //   - offset + length <= frame.buf.used
    // Violating this contract results in undefined behavior (out-of-bounds read).
    
    const SourcePosition where = pendingStartValid_ ? pendingStart_ : currentPosition();
    pendingStartValid_ = false;
    
    const char* data = nullptr;
    if (length != 0 && !tagStack_.empty() && tagStack_.back().buf.mem) {
        data = tagStack_.back().buf.mem.get() + offset;
    }
    
    out.type       = type;
    out.data       = data;
    out.length     = length;
    out.byteOffset = where.byteOffset;
    out.line       = where.line;
    out.column     = where.column;

#if defined(LXML_DEBUG_SLICES)
    out.arena      = ArenaId::Tag;
    out.generation = tagStack_.empty() ? 0 : tagStack_.back().buf.generation;
#endif

    return true;
}

bool XMLTokenizer::validateEndTagMatch(const char* namePtr, U32 nameLen) noexcept {
    // Caller MUST check namePtr to be valid memory
    // Must have an open tag to close
    if (tagStack_.empty())
        return false;
    
    const TagFrame& frame = tagStack_.back();
    const TagContext& ctx = frame.ctx;
    
    // Length mismatch → different names
    if (nameLen != ctx.nameLen)
        return false;
    
    // Empty names are considered matching (edge case)
    if (nameLen == 0)
        return true;
    
    // Buffer must exist to retrieve open tag name
    if (!frame.buf.mem)
        return false;
    
    const char* startTagName = frame.buf.mem.get() + ctx.nameMark.offset;
    return std::memcmp(namePtr, startTagName, nameLen) == 0;
}

} // namespace LXMLFormatter