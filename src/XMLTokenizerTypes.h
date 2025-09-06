#ifndef XML_TOKENIZER_TYPES_H
#define XML_TOKENIZER_TYPES_H
/*
    * XML Tokenizer Types
    * Version: 0.0.1
    * Author: Shahin Youssefi
    * License: MIT
    * Date: 2025-08-30
    *
    * This header defines the fundamental types, enums, structs, and constants used by the
    * LXMLFormatter XML tokenizer. It includes canonical scalar aliases, token kinds, error codes,
    * source position tracking, tokenizer options and limits, DFA states, arena helpers, entity scanning,
    * tokenizer flags, optional statistics, and character class helpers.
    *
    * Key components:
    * - Canonical scalar type aliases for portability and clarity.
    * - XMLTokenType: Enumerates all possible token kinds recognized by the tokenizer.
    * - SourcePosition: Tracks byte, line, and column positions in the source stream.
    * - TokenizerErrorCode, ErrorSeverity: Enumerations for error handling and reporting.
    * - XMLToken: Represents a single token, optimized for performance and alignment.
    * - TokenizerError: Stores error details with stable message storage.
    * - TokenizerOptions & TokenizerLimits: Configurable parsing options and resource limits.
    * - DFA State enum: Lexical states for the tokenizer's finite automaton.
    * - Arena helpers: Structures for managing buffer segments and tag context.
    * - EntityScan: Describes the result of scanning an XML entity.
    * - TokenizerFlags: Bitmask flags for internal tokenizer state.
    * - TokenizerStats: Optional statistics collection (compile-time enabled).
    * - CharClass: Static helpers for XML character classification.
    *
    * All types and definitions are encapsulated within the LXMLFormatter namespace.
*/

#include <cstdint>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>
#include <memory>
#include <array>

namespace LXMLFormatter {

// -------------------------------
// Canonical scalar aliases
// -------------------------------
using U8  = std::uint8_t;
using U16 = std::uint16_t;
using U32 = std::uint32_t;
using U64 = std::uint64_t;

using ByteLen   = U32;   // lengths/counters bounded by caps (< 4 GiB)
using ByteOff   = U64;   // absolute byte offset in stream (can be > 4 GiB)
using CharCount = U64;   // optional (guarded by LXML_TRACK_CHAROFFSET)

static_assert(sizeof(void*) == 8, "64-bit build required for tokenizer");

// -------------------------------
// Token kinds
// -------------------------------
enum class XMLTokenType : U8 {
    StartTag, EndTag, EmptyTag,         // tags
    AttributeName, AttributeValue,      // attributes
    Text, Comment, PI, CDATA, DOCTYPE,  // content
    DocumentStart, DocumentEnd, Error   // document
};

// -------------------------------
// Source position
// -------------------------------
struct SourcePosition {
    ByteOff byteOffset = 0;  // absolute byte index from start
    U32     line = 1;        // 1-based
    U32     column = 1;      // 1-based (character columns)

#if defined(LXML_TRACK_CHAROFFSET)
    CharCount charOffset = 0; // optional scalar count
#endif

#if defined(LXML_ERROR_CONTEXT)
    ByteOff contextStart = 0; // optional byte range for error snippets
    ByteOff contextEnd   = 0;
#endif
};

// -------------------------------
// Error codes (will expand in future) 
// -------------------------------
enum class TokenizerErrorCode : U16 {
    None = 0,

    // EOF / stream (0x10–0x1F)
    UnexpectedEOF = 0x10,
    IoError,

    // Structural / syntax (0x20–0x3F)
    InvalidCharAfterLT    = 0x20,
    InvalidCharInName,
    UnterminatedTag,
    ExpectedEqualsAfterAttrName,
    ExpectedQuoteForAttrValue,

    // Entities / encoding (0x40–0x4F)
    InvalidUTF8     = 0x40,
    MalformedEntity,

    // Comment / CDATA / PI (0x50–0x5F)
    UnterminatedComment  = 0x50,
    BadCommentDoubleDash,
    UnterminatedCData,
    UnterminatedPI,

    // Limits (0x60–0x6F)
    LimitExceeded = 0x60,
};
enum class ErrorSeverity : U8 { Warning, Recoverable, Fatal };

enum class ArenaId : U8 { None=0, Text=1, Tag=2, Error=3 };

// -------------------------------
// Token (hot path, 32 bytes, 32B aligned)
// Cache-Optimal Layout. exactly half a cache line to allow streaming through L1
// -------------------------------
// Lifetime: data points into tokenizer-owned storage.
//  - Text/Comment/CDATA/PI/DOCTYPE/Error: valid until the next nextToken() call.
//  - StartTag/AttributeName/AttributeValue/EmptyTag: valid until the current tag closes.
struct alignas(32) XMLToken {
    const char*   data = nullptr;           // slice start
    ByteOff       byteOffset = 0;           // token absolute start
    ByteLen       length = 0;               // slice length in bytes
    U32           line = 1;                 // start line
    U32           column = 1;               // start column (characters)
    XMLTokenType  type = XMLTokenType::Text;// kind

#if defined(LXML_DEBUG_SLICES)
    ArenaId       arena = ArenaId::None;    // owning arena for debug
    U16           generation = 0;           // arena generation for UAF checks
#else
    U8            _pad[3] = {0,0,0};        // pad to 32 bytes in release
#endif
};
static_assert(alignof(XMLToken) == 32, "XMLToken must be 32B aligned");
static_assert(sizeof(XMLToken)  == 32, "XMLToken must remain 32 bytes");
static_assert(std::is_trivially_copyable<XMLToken>::value, "XMLToken must be trivially copyable");
static_assert(std::is_trivially_copyable<TokenizerErrorCode>::value, "TokenizerError must be trivially copyable");
static_assert(sizeof(XMLTokenType) == 1, "XMLTokenType must be uint8_t-sized");
static_assert(sizeof(ErrorSeverity) == 1, "ErrorSeverity must be uint8_t-sized");

// -------------------------------
// Tokenizer error record
// -------------------------------
struct TokenizerError {
    TokenizerErrorCode  code = TokenizerErrorCode::None;
    ErrorSeverity       sev  = ErrorSeverity::Recoverable;
    U8                  _rsv = 0;               // align
    SourcePosition      where{};                // where it happened
    const char*        msg = nullptr;           // stable C-string (error arena)
    ByteLen            msgLen = 0;              // bytes in msg
};

// -------------------------------
// Options & limits
// -------------------------------
struct TokenizerOptions {
    // Bit-mask flags (packed to avoid padding)
    U32 flags = 0;

    // Bit constants
    static constexpr U32 CoalesceText             = 1u << 0;
    static constexpr U32 Strict                   = 1u << 1;
    static constexpr U32 NormalizeLineEndings     = 1u << 2;
    static constexpr U32 ExpandInternalEntities   = 1u << 3;
    static constexpr U32 ReportXmlDecl            = 1u << 4;
    static constexpr U32 ReportIntertagWhitespace = 1u << 5;

    // Defaults: everything on
    TokenizerOptions() noexcept
        : flags(CoalesceText | Strict | NormalizeLineEndings |
                ExpandInternalEntities | ReportXmlDecl | ReportIntertagWhitespace) {}

    // Convenience predicates (inline, zero-cost)
    inline bool coalesceText() const noexcept             { return (flags & CoalesceText) != 0; }
    inline bool strict() const noexcept                   { return (flags & Strict) != 0; }
    inline bool normalizeLineEndings() const noexcept     { return (flags & NormalizeLineEndings) != 0; }
    inline bool expandInternalEntities() const noexcept   { return (flags & ExpandInternalEntities) != 0; }
    inline bool reportXmlDecl() const noexcept            { return (flags & ReportXmlDecl) != 0; }
    inline bool reportIntertagWhitespace() const noexcept { return (flags & ReportIntertagWhitespace) != 0; }
};

//-------------------------------
// Soft, runtime-configurable limits (must be <= absolute caps)
//-------------------------------
struct TokenizerLimits {
    ByteLen maxNameBytes        = 4u * 1024u;
    ByteLen maxAttrValueBytes   = 1u * 1024u * 1024u;
    ByteLen maxTextRunBytes     = 8u * 1024u * 1024u;
    ByteLen maxCommentBytes     = 1u * 1024u * 1024u;
    ByteLen maxCdataBytes       = 8u * 1024u * 1024u;
    ByteLen maxDoctypeBytes     = 128u * 1024u;
    // DoS guards tied to tag arena
    U16     maxAttrsPerElement  = 1024;
    ByteLen maxPerTagBytes      = 8u * 1024u * 1024u; // pre-reserved/fixed tag buffer
    U16     maxOpenDepth        = 1024;               // maximum nesting depth
};

//-------------------------------
// Absolute compile-time caps (never exceeded)
//-------------------------------
namespace Caps {
    inline constexpr ByteLen AbsMaxNameBytes      = 64u * 1024u;
    inline constexpr ByteLen AbsMaxAttrValueBytes = 64u * 1024u * 1024u;
    inline constexpr ByteLen AbsMaxTextRunBytes   = 64u * 1024u * 1024u;
    inline constexpr ByteLen AbsMaxCommentBytes   = 16u * 1024u * 1024u;
    inline constexpr ByteLen AbsMaxCdataBytes     = 64u * 1024u * 1024u;
    inline constexpr ByteLen AbsMaxDoctypeBytes   = 8u  * 1024u * 1024u;
    inline constexpr ByteLen AbsMaxPerTagBytes    = 16u * 1024u * 1024u;
}

// Tie caps to ByteLen (overflow safety)
static_assert(Caps::AbsMaxTextRunBytes   < std::numeric_limits<ByteLen>::max(),   "ByteLen too small for AbsMaxTextRunBytes");
static_assert(Caps::AbsMaxAttrValueBytes < std::numeric_limits<ByteLen>::max(),   "ByteLen too small for AbsMaxAttrValueBytes");
static_assert(Caps::AbsMaxPerTagBytes    < std::numeric_limits<ByteLen>::max(),   "ByteLen too small for AbsMaxPerTagBytes");

// -------------------------------
// DFA states (lexical only)
// -------------------------------
enum class State : U8 {
    Content,                                                                // Outside tags
    TagOpen,                                                                // looking for '<'
    StartTagName, EndTagName, InTag,                                        // Inside a start or end tag
    AttrName, AfterAttrName, BeforeAttrValue, AttrValueQuoted,              // Inside an attribute
    AfterBang, CommentStart1, CommentStart2, InComment, CommentEnd1, CommentEnd2,  // Inside a comment
    CDataStart, InCData, CDataEnd1, CDataEnd2,                              // Inside a CDATA section
    PITarget, PIContent,                                                    // Inside a processing instruction
    Resyncing                                                               // Inside a resyncing state
};
static_assert(sizeof(State) == 1, "State must be uint8_t-sized");

// -------------------------------
// Arena-related helpers
// Semantics tracker, separated from DFA
// -------------------------------
/* memory layout:
Document parsing:
├── TagBuffer (fixed, pointer-stable)
│   ├── Tag: "element" [0..7]
│   ├── Attr: "id" [8..10]  
│   ├── Value: "123" [11..14]
│   └── [All pointers remain valid until tag closes]
│
└── TextArena (growable)
    ├── "Some text" [emitted immediately]
    └── [Can clear/reuse after emission]

    * Holds all data for a single tag (name + all attributes).
    * Once allocated, the buffer address NEVER changes.
    * It matters because multiple tokens will point into this buffer
    * Note: All these tokens may be alive simultaneously in the parser, so the memory must remain stable
*/
struct TagBuffer {
    std::unique_ptr<char[]> mem{};   // capacity = limits.maxPerTagBytes
    ByteLen cap = 0;                 // bytes
    ByteLen used = 0;                // bytes used in current tag
#if defined(LXML_DEBUG_SLICES)
    U16 generation = 1;
#endif
};

// Text arena (ephemeral slices via std::vector<char>)
// Accumulates text content between tags.
// Can grow/reallocate because text is emitted immediately.
// Once a Text token is returned, the tokenizer won't modify buf until the caller calls nextToken() again
struct TextArena {
    std::vector<char> buf;
#if defined(LXML_DEBUG_SLICES)
    U16 generation = 1;
#endif
    // NOTE: token contracts ensure we don't mutate buf until the caller
    // asks for nextToken() again → slice pointer remains valid.
};

// Slice helpers
struct SegmentMark { U32 offset = 0; }; // offset within owning contiguous buffer
struct TagContext {
    SegmentMark nameMark{};            // start offset of tag name in tag buffer
    U32         nameLen = 0;           // bytes
    U16         attrCount = 0;         // number of attributes seen
    bool        sawSlashBeforeGT = false; // for EmptyTag detection
    // token start pos (for precise errors)
    U32         startLine = 1;
    U32         startColumn = 1;
    ByteOff     startByteOffset = 0;
    // current tail segment length (fast slice of attr values)
    U32         tailSegLen = 0;
};
static_assert(Caps::AbsMaxPerTagBytes < std::numeric_limits<U32>::max());

//-------------------------------
// Lookahead slot (for token buffering)
//-------------------------------
struct LookaheadSlot {
    bool     has = false;
    XMLToken tok{};
};

// -------------------------------
// Entity scanning
// -------------------------------
enum class EntityKind : U8 { Builtin, Numeric, Unknown };
struct EntityScan {
    EntityKind kind = EntityKind::Unknown;
    U32        value = 0;  // numeric value if Numeric/Builtin
    U16        nameLen = 0; // for Unknown (entity name length, bytes)
    U16        rawLen = 0;  // bytes consumed from '&'..';'
    bool       ok = false;
};

// -------------------------------
// Flags (no bitfields -> better codegen)
// -------------------------------
struct TokenizerFlags {
    U32 bits = 0;
    static constexpr U32 Started  = 1u << 0;
    static constexpr U32 Ended    = 1u << 1;
    static constexpr U32 InAttr   = 1u << 2;
    static constexpr U32 SawCR    = 1u << 3; // for CRLF normalization helpers
    // helpers
    inline bool test(U32 m) const noexcept { return (bits & m) != 0; }
    inline void set (U32 m)       noexcept { bits |=  m; }
    inline void clr (U32 m)       noexcept { bits &= ~m; }
};

// -------------------------------
// Stats [place holder for future use]
// -------------------------------
#if defined(LXML_ENABLE_STATS)
struct TokenizerStats {
    U64     bytesConsumed = 0;
    U64     tokensEmitted = 0;
    U64     errorsEmitted = 0;
    ByteLen maxTextArena  = 0;
    ByteLen maxTagArena   = 0;
};
#  define LXML_STAT_INC(field, val) ((field) += (val))
#else
struct TokenizerStats { };
#  define LXML_STAT_INC(field, val) ((void)0)
#endif

// -------------------------------
// Character classes (fast ASCII, stubs for Unicode)
// (We keep them inline here to avoid extra compilation units.)
// ---- character classes (ASCII LUT + TODO Unicode ranges) ----
// -------------------------------
struct CharClass {
    static constexpr std::array<bool,128> AsciiNameStart = []() {
        std::array<bool,128> t{}; // all false
        t[static_cast<std::size_t>(':')] = true;
        t[static_cast<std::size_t>('_')] = true;
        for (unsigned c = 'A'; c <= 'Z'; ++c) t[c] = true;
        for (unsigned c = 'a'; c <= 'z'; ++c) t[c] = true;
        return t;
    }();

    static constexpr std::array<bool,128> AsciiNameChar = []() {
        std::array<bool,128> t = AsciiNameStart; // start with Start-set
        t[static_cast<std::size_t>('-')] = true;
        t[static_cast<std::size_t>('.')] = true;
        for (unsigned c = '0'; c <= '9'; ++c) t[c] = true;
        return t;
    }();

    static inline bool isAscii(U32 cp) noexcept { return cp < 128u; }

    static inline bool isXMLWhitespace(U32 cp) noexcept {
        return cp == 0x20u || cp == 0x09u || cp == 0x0Au || cp == 0x0Du;
    }

    static inline bool isNameStart(U32 cp) noexcept {
        if (isAscii(cp)) return AsciiNameStart[cp];
        return true; // TODO: full XML 1.0 ranges
    }

    static inline bool isNameChar(U32 cp) noexcept {
        if (isAscii(cp)) return AsciiNameChar[cp];
        return true; // TODO: full XML 1.0 ranges
    }

    static inline bool isPubidChar(U32 cp) noexcept {
        if (!isAscii(cp)) return false;
        const bool az = (cp>='A'&&cp<='Z')||(cp>='a'&&cp<='z')||(cp>='0'&&cp<='9');
        switch (cp) {
            case 0x20: case 0x0D: case 0x0A: case '-': case '\'': case '(': case ')':
            case '+': case ',': case '.': case '/': case ':': case '=': case '?':
            case ';': case '!': case '*': case '#': case '@': case '$': case '_': case '%':
                return true;
            default: return az;
        }
    }
};

} // namespace LXMLFormatter

#endif // XML_TOKENIZER_TYPES_H