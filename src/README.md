Buffered Input Stream
===============================================
[TODO] fill this part

Streaming XML Tokenizer
===============================================

A high-performance, streaming XML tokenizer for **very large** documents. It emits a linear stream of tokens suitable for a SAX layer while enforcing strict lifetime guarantees and DoS-resistant limits.

Goals (Phase-1)
---------------

*   **Streaming** over std::istream via BufferedInputStream (UTF-8).
    
*   **Nested tags** with strict start/end matching.
    
*   **Tokens**: DocumentStart, StartTag, AttributeName, AttributeValue, EmptyTag, EndTag, Text, DocumentEnd, Error.
    
*   **Pointer stability** for **tag-scoped** data (element name + attributes) until the element closes.
    
*   **Coalesced text** with optional CR/CRLF→LF normalization.
    
*   **DoS guards** via TokenizerLimits (per-tag bytes, per-field bytes, **max open depth**).
    
*   **Fail-fast** on unsupported constructs in Phase-1 (comments, CDATA, PI, DOCTYPE).
    

### Non-Goals (Phase-1)

Entity expansion; comments/CDATA/PI/DOCTYPE; recovery/resync after structural errors; single-quote attribute values.

Core Concepts
-------------

### Token lifetimes

*   **Tag tokens** (StartTag, AttributeName, AttributeValue, EmptyTag, EndTag)token.data points into the **current element’s TagBuffer** and remains valid **until that element closes**.
    
*   **Text tokens** (Text)token.data points into the **TextArena** and remains valid **until the next nextToken() call**.
    
*   **Error tokens** (Error)msg is interned in an **ErrorArena** and remains valid until reset().
    

### Memory model

*   **TagFrame (per open element)**Fixed-capacity **TagBuffer** (limits.maxPerTagBytes) that never reallocates; all tokens for that element point into it. Frames live on a LIFO **stack**; popped on matching end-tag or />.
    
*   **TextArena (per text run)**std::vector used to accumulate one Text token at a time; reused between tokens.
    
*   **ErrorArena**std::vector storing stable error strings.
    
*   **Freelist**Pool of unique\_ptr blocks for TagBuffer reuse (all blocks are exactly limits.maxPerTagBytes); invalidated if the limit changes.
    

High-level component diagram
----------------------------
                           (Input)
+--------------------------------------------------+
|           BufferedInputStream (UTF-8)            |
|      decode + line/column/byte tracking          |
+-------------------------------+------------------+
                                |
                                v
+-------------------------------+---------------------+        emits one token per call
|                XMLTokenizer (state_, opts_, lims_)  |------------------------------>
+-----------+---------------+-----------+-------------+                               
            |               |           |                                            
            |               |           |                                            
            |               |           |                                            
            v               v           v                                            
   +----------------+  +-----------+  +----------------+   +---------------------+   +------------------------+
   | TagFrame stack |  | TextArena |  |  ErrorArena    |   |      Freelist       |   |        errors_         |
   | (TagBuffer +   |  | vector<>  |  |  vector<>      |   | unique_ptr<char[]>  |   |  TokenizerError records|
   |  TagContext)   |  |           |  |                |   |     blocks          |   |                        |
   +----------------+  +-----------+  +----------------+   +---------------------+   +------------------------+

                                   +------------------+
                                   |  XMLToken stream |
                                   +------------------+


DFA (Phase-1)
-------------

States used: Content, TagOpen, StartTagName, EndTagName, InTag, AttrName, AfterAttrName, BeforeAttrValue, AttrValueQuoted.

**Transitions (happy path)**

*   **Content**Read text → Text → ContentSee < → TagOpenEOF with no text → DocumentEnd
    
*   **TagOpen**/ → EndTagNameNameStart → StartTagName! or ? → Error (unsupported in Phase-1)
    
*   **StartTagName**Read name into TagBuffer → emit StartTag → InTag
    
*   **InTag**Whitespace → stayNameStart → AttrName> → Content/> → emit EmptyTag & pop TagFrame → Content
    
*   **AttrName** → emit AttributeName → AfterAttrName
    
*   **AfterAttrName** → require = → BeforeAttrValue
    
*   **BeforeAttrValue** → require " → AttrValueQuoted
    
*   **AttrValueQuoted** → read until next " → emit AttributeValue → InTag
    
*   **EndTagName**Stream-compare name against top TagFrame’s name; on match + > → emit EndTag & pop → Content; else Error.
    

Hot vs cold paths
-----------------

**Hot paths**

*   scanText() (coalesced text; optional CR/CRLF→LF normalization)
    
*   parseStartTag() + parseAttributesBasic() (ASCII fast path; batched TagBuffer appends)
    
*   parseEndTag() (bytewise compare against current TagFrame name)
    
*   appendToCurrentTagBuf() + ensureCurrentTagCapacity() (tight bounds checks; no reallocation)
    

**Cold paths**

*   emitDocumentStart() / emitDocumentEnd() (once each)
    
*   pushTagFrame() / popTagFrame() (per element)
    
*   Freelist invalidation on reset() when maxPerTagBytes changes
    
*   emitError() / internError() (malformed docs)
    

Error policy
------------

*   Structural or limit errors (InvalidCharAfterLT, ExpectedEqualsAfterAttrName, ExpectedQuoteForAttrValue, UnterminatedTag, LimitExceeded, …) emit a single Error token and push a TokenizerError into errors\_ with precise SourcePosition.
    
*   Phase-1 **fails fast** after emitting Error.
    

Options & limits
----------------

TokenizerOptions (defaults enabled):

*   CoalesceText, NormalizeLineEndings, Strict, ExpandInternalEntities (ignored in Phase-1), ReportXmlDecl, ReportIntertagWhitespace.
    

TokenizerLimits (DoS guards):

*   maxPerTagBytes (TagBuffer capacity)
    
*   maxOpenDepth (nesting depth)
    
*   Per-field caps: names, attribute values, text run, etc.
    

Exceeding a limit produces Error(LimitExceeded).

Token contracts
---------------

*   **Exactly one** token per nextToken() call.
    
*   XMLToken.data:
    
    *   Tag tokens → pointer into **top TagFrame**’s TagBuffer; **valid until that element closes**.
        
    *   Text tokens → pointer into **TextArena**; **valid until the next nextToken()**.
        
    *   Error token → message pointer into **ErrorArena**; valid until reset().
        

Performance notes
-----------------

*   UTF-8 decode fast path (ASCII-heavy) via UTF8Handler.
    
*   Batch appends into TagBuffer and TextArena to reduce bounds checks.
    
*   Per-element fixed TagBuffer avoids reallocations and pointer invalidation.
    
*   Freelist amortizes TagBuffer allocations to O(depth).    

*   Text tokens (not shown) are ephemeral: valid only until the next token is requested.
    
*   Tag tokens remain valid across the element’s entire lifetime (from its StartTag to its matching EndTag/EmptyTag).
    

Quick start
-----------

```C++
#include "BufferedInputStream.h"
#include "XMLTokenizer.h"
#include <sstream>

using namespace LXMLFormatter;

int main() {
    std::istringstream src("<a x=\"1\"><b>hi</b></a>");
    auto bis = BufferedInputStream::Create(src, 1<<20); // 1MB buffer
    XMLTokenizer tz(*bis);

    XMLToken tok;
    while (tz.nextToken(tok)) {
        // switch(tok.type) { ... }
    }
}
```