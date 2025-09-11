#include "XMLTokenizer.h"
#include "XMLTokenizerTypes.h"

namespace LXMLFormatter {

//a note on the anonymous namespace: this is used to define helper functions that are only visible within 
// this .cpp file. This helps to avoid name collisions and keeps the global namespace clean.
//so no symbol exported outside this file
namespace {
    // Upper-bound clamp for ByteLen (U32).
    inline ByteLen clampBL(ByteLen v, ByteLen cap) noexcept {
        return (v > cap) ? cap : v;
    }
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

    // Bind freelist block size to current per-tag capacity.
    freelistBlockSize_ = lims_.maxPerTagBytes;

    // stats_ is trivially zero-initialized (enabled build collects later).
}

bool XMLTokenizer::scanText(XMLToken& out) {
    int32_t cp = peekCp();
    if (cp == '<') { state_ = State::TagOpen; return false; }
    if (cp < 0) return false;
    
    textArena_.buf.clear();
    markTokenStart();
    
    while (true) {
        cp = peekCp();
        if (cp == '<' || cp < 0) break;
        
        getCp();  // Consume
        
        // Normalization ONLY in Content (not markup)
        if (opts_.normalizeLineEndings() && cp == '\r') {
            if (peekCp() == '\n') getCp();
            textArena_.buf.push_back('\n');
        } else {
            textArena_.buf.push_back(static_cast<char>(cp));
        }
        
        // Exact limit check (>= not >)
        if (textArena_.buf.size() >= lims_.maxTextRunBytes) {
            return emitError(out, TokenizerErrorCode::LimitExceeded,
                           ErrorSeverity::Fatal, "Text run too long", 17);
        }
    }
    
    makeTextToken(out, 0, textArena_.buf.size());
    return true;
}

} // namespace LXMLFormatter