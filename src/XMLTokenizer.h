#ifndef LXMLFORMATTER_XMLTOKENIZER_H
#define LXMLFORMATTER_XMLTOKENIZER_H

#include "BufferedInputStream.h"
#include "XMLTokenizerTypes.h"
#include <vector>
#include <string>
#include <memory>
#include <utility>

namespace LXMLFormatter {

class XMLTokenizer {
public:
    // Construct with input stream, options, and limits.
    explicit XMLTokenizer(BufferedInputStream& in,
                          TokenizerOptions opts = {},
                          TokenizerLimits lims = {}) noexcept;

    // Not copyable/movable (references & internal buffers).
    XMLTokenizer(const XMLTokenizer&)            = delete;
    XMLTokenizer& operator=(const XMLTokenizer&) = delete;
    XMLTokenizer(XMLTokenizer&&)                 = delete;
    XMLTokenizer& operator=(XMLTokenizer&&)      = delete;

    // Produce the next token; returns false after DocumentEnd or fatal error.
    bool nextToken(XMLToken& out);

    // Accessors.
    const TokenizerOptions& options() const noexcept { return opts_; }
    const TokenizerLimits&  limits()  const noexcept { return lims_; }
    State                   state()   const noexcept { return state_; }

    // Simple error API (Phase 1: queue all errors; caller may clear).
    const std::vector<TokenizerError>& errors() const noexcept { return errors_; }
    void clearErrors() noexcept { errors_.clear(); }

    // Current source position (byte/line/column) as seen at last read.
    SourcePosition currentPosition() const noexcept;

    // Current nesting depth (number of open elements).
    size_t nestingDepth() const noexcept { return tagStack_.size(); }

    // Reset tokenizer to initial state (keeps same stream, options, and limits).
    void reset() noexcept;

    // Error helper
    bool emitError(XMLToken& out,
                   TokenizerErrorCode code,
                   ErrorSeverity sev,
                   const char* msg,
                   U32 msgLen) noexcept;

private:
    // --- Core wiring ---
    BufferedInputStream& in_;
    TokenizerOptions     opts_;
    TokenizerLimits      lims_;
    TokenizerStats       stats_;    // disabled unless LXML_ENABLE_STATS
    TokenizerFlags       flags_;
    State                state_ = State::Content;

    // --- TagFrame: One stable buffer per open element ---
    struct TagFrame {
        TagBuffer buf;                    // Stable storage for this element's data
        TagContext ctx;                   // Name offsets, attr count, position info
        SourcePosition startPos;          // Where this element started (for errors)
        
        TagFrame() {
            buf.mem = nullptr;
            buf.cap = 0;
            buf.used = 0;
#if defined(LXML_DEBUG_SLICES)
            buf.generation = 1;
#endif
        }
    };

    // LIFO stack: tagStack_.back() = currently parsing element
    std::vector<TagFrame> tagStack_;
    
    // Separate arena for text content (ephemeral between tokens)
    TextArena textArena_;
    
    // Single-slot lookahead buffer (future feature)
    LookaheadSlot la_;

    // Errors accumulated during parsing
    std::vector<TokenizerError> errors_;
    
    // Error message arena (stable storage for TokenizerError.msg)
    std::vector<char> errorArena_;

    // freelist for TagBuffer reuse (minimize allocations)
    // Invariant: all entries are exactly freelistBlockSize_ bytes
    std::vector<std::unique_ptr<char[]>> tagBufFreelist_;
    ByteLen freelistBlockSize_ = 0;  // Track freelist buffer size for invalidation
    
    // Token start position tracking
    SourcePosition pendingStart_{};  // Captured at start of each token
    bool pendingStartValid_ = false; // true iff markTokenStart() captured a start
    
    // Constants
    static constexpr U32 kBadOff = 0xFFFFFFFFu;  // Sentinel for failed append operations

    // --- Phase-1 helpers (happy-path minimal XML) ---
    bool emitDocumentStart(XMLToken& out) noexcept;
    bool emitDocumentEnd(XMLToken& out) noexcept;

    // Content: gather text until '<' or EOF; coalesce based on options.
    bool scanText(XMLToken& out);

    // Decide start vs end tag after '<' (no comments/PI/doctype in Phase 1).
    bool scanTagOrError(XMLToken& out);

    // Parse a start tag: <Name (Attr="Value")* [/]?>
    bool parseStartTag(XMLToken& out);

    // Parse an end tag: </n>
    bool parseEndTag(XMLToken& out);

    // Basic attributes: name="value" (double-quoted only in Phase 1).
    // Stateful: each call emits one AttributeName or AttributeValue token.
    bool parseAttributesBasic(XMLToken& out);

    // --- TagFrame stack management ---
    // Push new frame when starting an element; allocates TagBuffer if needed.
    // Returns false if tagStack_.size() >= lims_.maxOpenDepth (DoS protection).
    bool pushTagFrame();
    
    // Pop frame when element closes; may add buffer to freelist.
    void popTagFrame() noexcept;
    
    // Get current (top) tag frame; nullptr if stack empty.
    TagFrame* currentTagFrame() noexcept {
        return tagStack_.empty() ? nullptr : &tagStack_.back();
    }
    
    // Allocate or reuse a TagBuffer for the current frame.
    // Always allocate full-size per-tag buffer, no growth is happening here.
    inline void noteTagArena(ByteLen cap) {
        #if defined(LXML_ENABLE_STATS)
            stats_.maxTagArena = std::max(stats_.maxTagArena, cap);
        #endif
    }
    bool ensureCurrentTagBuffer() noexcept;

    // --- Low-level building blocks ---
    // Capture token start position (call before consuming first char).
    inline void markTokenStart() noexcept { 
        pendingStart_ = currentPosition(); 
        pendingStartValid_ = true;
    }

    // Read XML Name into current TagBuffer; returns (off,len) or 0-len on failure.
    std::pair<U32,U32> readNameToCurrentTagBuffer();

    // Append raw bytes to current TagBuffer; returns starting offset or kBadOff on failure.
    U32 appendToCurrentTagBuf(const char* data, U32 len);

    // Append single byte to current TagBuffer.
    U32 appendToCurrentTagBuf(char ch) { return appendToCurrentTagBuf(&ch, 1); }

    // Ensure current TagBuffer has at least 'need' free bytes.
    bool ensureCurrentTagCapacity(ByteLen need);

    // Emit token pointing into TextArena (valid until next nextToken()).
    // Uses pendingStart_ for position info.
    bool makeTextToken(XMLToken& out, U32 off, U32 len) noexcept;

    // Emit token pointing into current TagBuffer (valid until element close).
    // Uses pendingStart_ for position info.
    bool makeTagToken(XMLToken& out, XMLTokenType t, U32 off, U32 len) noexcept;

    // Validate end tag name matches current open element.
    bool validateEndTagMatch(const char* namePtr, U32 nameLen) noexcept;
    
    // Intern error message in stable storage; returns pointer into errorArena_.
    const char* internError(const char* s, U32 len) noexcept;

    // Small utilities.
    static inline bool isNameStart(U32 cp) noexcept { return CharClass::isNameStart(cp); }
    static inline bool isNameChar (U32 cp) noexcept { return CharClass::isNameChar(cp);  }

    // Consume optional whitespace in content/inside tags.
    void skipXMLSpace();

    // Read one code point (via BufferedInputStream).
    int32_t getCp()  { return in_.getChar();  }
    int32_t peekCp() { return in_.peekChar(); }
};

// ===== inline small bits =====
inline void XMLTokenizer::reset() noexcept {
    state_ = State::Content;
    flags_.bits = 0;
    errors_.clear();
    errorArena_.clear();
    
    // Clear tag stack and move buffers to freelist for reuse
    for (auto& frame : tagStack_) {
        if (frame.buf.mem && frame.buf.cap == lims_.maxPerTagBytes) {
            tagBufFreelist_.push_back(std::move(frame.buf.mem));
        }
        // Otherwise buffer is discarded (size mismatch with current limits)
    }
    tagStack_.clear();
    
    // Only purge freelist if block size changed (performance optimization)
    if (freelistBlockSize_ != lims_.maxPerTagBytes) {
        tagBufFreelist_.clear();
        freelistBlockSize_ = lims_.maxPerTagBytes;
    }
    
    // Clear text arena
    textArena_.buf.clear();
#if defined(LXML_DEBUG_SLICES)
    textArena_.generation = textArena_.generation ? U16(textArena_.generation + 1) : 1;
#endif
    
    // Clear lookahead
    la_.has = false;
    
    // Reset pending position
    pendingStart_ = SourcePosition{};
    pendingStartValid_ = false;
}

// Returns the position of the *next unread* input byte (start-of-token if called before consumption).
inline SourcePosition XMLTokenizer::currentPosition() const noexcept {
    SourcePosition sp{};
    sp.byteOffset = in_.getTotalBytesRead();
    sp.line       = static_cast<U32>(in_.getCurrentLine());
    sp.column     = static_cast<U32>(in_.getCurrentColumn());
#if defined(LXML_TRACK_CHAROFFSET)
    // If character offset tracking is enabled, could be implemented here
    // For now, leave as default (0)
    sp.charOffset = 0;
#endif
#if defined(LXML_ERROR_CONTEXT)
    // If error context tracking is enabled, could be implemented here
    // For now, leave as defaults (0)
    sp.contextStart = 0;
    sp.contextEnd = 0;
#endif
    return sp;
}

} // namespace LXMLFormatter

#endif // LXMLFORMATTER_XMLTOKENIZER_H