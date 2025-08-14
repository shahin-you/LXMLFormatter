
/*
    * UTF8 helper
    * Version: 0.0.1
    * Author: Shahin Youssefi
    * License: MIT
    * Date: 2025-08-20
    * 
    * This file provides a UTF-8 decoder using a lookup table for efficient decoding.
    * It supports decoding of valid UTF-8 sequences and identifies invalid sequences.
    * NOTE:The decoder is designed to be used in performance-critical applications and
    * the code is written not for readablity but for speed and efficiency.
*/

#ifndef LXMLFORMATTER_UTF8HANDLER_H
#define LXMLFORMATTER_UTF8HANDLER_H

#include <cstdint>
#include <cstddef>
#include <array>
#include <utility>

/// \namespace ShUTF8Detail
/// \brief Implementation details for UTF-8 table generation. Not public API.
///
/// This namespace exists solely so the UTF8Handler’s lookup table can be
/// constant-initialized in C++17 without tripping incomplete-class rules:
/// the table’s in-class initializer calls free `constexpr` generators defined
/// *before* the class. That’s the only reason this namespace exists.
///
/// **don’t rely on this:**
/// - Not part of the library’s API or ABI. Names, types, and functions here
///   may change or disappear without notice.
/// - Intended for internal use by `UTF8Handler` only. If you reach into it,
///   you own the breakage.
/// - Header-only and ODR-safe: everything here is `constexpr` (thus `inline`)
///   or otherwise suitable for inclusion in multiple TUs.
/// - No runtime state; all work happens at compile time; zero per-call overhead.
/// - Requires C++17 (`std::array`, `std::index_sequence`). No libc dependencies.
///
/// Public consumers should use `UTF8Handler` and ignore `ShUTF8Detail`.
/// If you think you need something from here, you don’t.
namespace ShUTF8Detail {
// Lookup table for first byte - 256 bytes, fits in L1 cache
    struct Utf8Info {
        uint8_t bytes;
        uint8_t mask;
        uint8_t shift;
        uint8_t pad;        //reserved for alignment/ABI; intentionally unused
        uint32_t min_cp;
    };

    constexpr Utf8Info make_utf8_info(std::size_t byte) noexcept {
        // ASCII: 0xxxxxxx (0x00-0x7F)
        if (byte <= 0x7F) {
            return {1, 0x7F, 0, 0, 0x00};   // bytes: single byte, mask: all 7 bits, shift: no shift needed, no pad, cp >= 0
        }
        // Invalid: 10xxxxxx (0x80-0xBF) - continuation bytes can't be first
        else if (byte >= 0x80 && byte <= 0xBF) {
            return {0, 0, 0, 0, 0};         // all zeros = invalid
        }
        // Invalid: 11000000-11000001 (0xC0-0xC1) - overlong 2-byte sequences
        else if (byte >= 0xC0 && byte <= 0xC1) {
            return {0, 0, 0, 0, 0};
        }
        // 2-byte: 110xxxxx (0xC2-0xDF) 
        else if (byte >= 0xC2 && byte <= 0xDF) {
            return {2, 0x1F, 6, 0, 0x80};   //2-byte sequence, mask: lower 5 bits, shift first byte contributes 5 bits, shifted left by 6, pad, min_cp
        }
        // 3-byte: 1110xxxx (0xE0-0xEF)
        else if (byte >= 0xE0 && byte <= 0xEF) {
            return {3, 0x0F, 12, 0, 0x800}; //3-byte sequence, mask: lower 4 bits, shift first byte contributes 4 bits, shifted left by 12, pad, min_cp
        }
        // 4-byte: 11110xxx (0xF0-0xF4)
        else if (byte >= 0xF0 && byte <= 0xF4) {
            return {4, 0x07, 18, 0, 0x10000};//4-byte sequence, mask: lower 3 bits, shift first byte contributes 3 bits, shifted left by 18, pad, min_cp
        }
        // Invalid: 11110101-11111111 (0xF5-0xFF) - would encode > U+10FFFF
        else {
            return {0, 0, 0, 0, 0};
        }
    }

    template<std::size_t... Is>
    constexpr std::array<Utf8Info, 256>
    generate_table_impl(std::index_sequence<Is...>) noexcept { //metaprogramming from hell :)
        return {{ make_utf8_info(Is)... }};
    }

    constexpr std::array<Utf8Info, 256> generate_table() noexcept {
        return generate_table_impl(std::make_index_sequence<256>{});
    }
} // namespace ShUTF8Detail

class UTF8Handler {
public:
    enum class DecodeStatus {
        Ok,
        NeedMore,
        Invalid
    };
    
    struct DecodeResult {
        uint32_t cp = 0;           // codepoint
        uint8_t width = 0;          // bytes consumed (or needed if NeedMore)
        DecodeStatus status = DecodeStatus::Invalid;
    };
    
    // Static decoder - no state needed
    [[nodiscard]] static DecodeResult decode(const uint8_t* p, std::size_t avail) noexcept;
    
    // Convenience overload for buffer + offset
    [[nodiscard]] static DecodeResult decode(const uint8_t* buffer, std::size_t size, std::size_t offset) noexcept {
        if (offset >= size) return DecodeResult{0, 1, DecodeStatus::NeedMore};
        return decode(buffer + offset, size - offset);
    }
    
private:        
    alignas(64) static inline const std::array<ShUTF8Detail::Utf8Info, 256> utf8_table = ShUTF8Detail::generate_table();

    // Branch-free surrogate check: returns true if NOT a surrogate
    [[nodiscard]] static constexpr bool check_surrogates(uint32_t cp) noexcept {
        return (cp & 0xFFFFF800) != 0xD800;
    }

    // Make constructor private - this is a utility class
    UTF8Handler() = delete;
};

/// this library shall be compiled with GCC or Clang
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define IS_CONTINUATION(b) (((b) & 0xC0) == 0x80)
#define CONTINUATION_VALUE(b) ((b) & 0x3F)
#define FORCE_INLINE __attribute__((always_inline)) inline

inline UTF8Handler::DecodeResult UTF8Handler::decode(const uint8_t* p, std::size_t avail) noexcept {
    DecodeResult result;
    
    if (UNLIKELY(avail == 0)) {
        result.status = DecodeStatus::NeedMore;
        result.width = 1;
        return result;
    }
    
    const uint8_t first = p[0];
    const ShUTF8Detail::Utf8Info& info = utf8_table[first];  // Direct index into our table
    
    // Fast path for ASCII
    if (LIKELY(info.bytes == 1)) {
        result.cp = first;
        result.width = 1;
        result.status = DecodeStatus::Ok;
        return result;
    }
    
    // Invalid first byte (info.bytes == 0)
    if (UNLIKELY(info.bytes == 0)) {
        result.status = DecodeStatus::Invalid;
        result.width = 1; //callers can skip one byte and resync
        return result;
    }
    
    // Need more bytes?
    if (UNLIKELY(avail < info.bytes)) {
        result.status = DecodeStatus::NeedMore;
        result.width = info.bytes;
        return result;
    }
    
    uint32_t codepoint;
    
    switch (info.bytes) {
    case 2: {
        const uint8_t b1 = p[1];
        
        if (UNLIKELY(!IS_CONTINUATION(b1))) {
            result.status = DecodeStatus::Invalid;
            result.width = 1;
            return result;
        }
        
        // Combine: (first_byte & mask) << 6 | (second_byte & 0x3F)
        codepoint = ((first & info.mask) << info.shift) | 
                    CONTINUATION_VALUE(b1);

        if (UNLIKELY(codepoint < info.min_cp)) { 
            result.status = DecodeStatus::Invalid;
            result.width = 1;
            return result; 
        }
        break;
    }
    
    case 3: {
        const uint8_t b1 = p[1];
        const uint8_t b2 = p[2];
        
        if (UNLIKELY(!IS_CONTINUATION(b1) || !IS_CONTINUATION(b2))) {
            result.status = DecodeStatus::Invalid;
            result.width = 1;
            return result;
        }
        
        // Combine: (first & mask) << 12 | (b1 & 0x3F) << 6 | (b2 & 0x3F)
        codepoint = ((first & info.mask) << info.shift) | 
                    (CONTINUATION_VALUE(b1) << 6) |
                    CONTINUATION_VALUE(b2);
        
        // Check for overlong encoding
        if (UNLIKELY(codepoint < info.min_cp)) {
            result.status = DecodeStatus::Invalid;
            result.width = 1;
            return result;
        }
        
        // Check for surrogates (0xD800-0xDFFF are invalid)
        if (UNLIKELY(!check_surrogates(codepoint))) {
            result.status = DecodeStatus::Invalid;
            result.width = 1;
            return result;
        }
        break;
    }
    
    case 4: {
        const uint8_t b1 = p[1];
        const uint8_t b2 = p[2];
        const uint8_t b3 = p[3];
        
        if (UNLIKELY(!IS_CONTINUATION(b1) || 
                     !IS_CONTINUATION(b2) || 
                     !IS_CONTINUATION(b3))) {
            result.status = DecodeStatus::Invalid;
            result.width = 1;
            return result;
        }
        
        // Combine: (first & mask) << 18 | (b1 & 0x3F) << 12 | (b2 & 0x3F) << 6 | (b3 & 0x3F)
        codepoint = ((first & info.mask) << info.shift) | 
                    (CONTINUATION_VALUE(b1) << 12) |
                    (CONTINUATION_VALUE(b2) << 6) |
                    CONTINUATION_VALUE(b3);
        
        // Check valid range: >= 0x10000 and <= 0x10FFFF
        if (UNLIKELY(codepoint < info.min_cp || codepoint > 0x10FFFF)) {
            result.status = DecodeStatus::Invalid;
            result.width = 1;
            return result;
        }
        break;
    }
    
    default:
        __builtin_unreachable();
    }
    
    result.cp = codepoint;
    result.width = info.bytes;
    result.status = DecodeStatus::Ok;
    return result;
}

#undef LIKELY
#undef UNLIKELY  
#undef IS_CONTINUATION
#undef CONTINUATION_VALUE
#undef FORCE_INLINE

#endif 