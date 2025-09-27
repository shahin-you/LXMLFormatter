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

} // namespace LXMLFormatter