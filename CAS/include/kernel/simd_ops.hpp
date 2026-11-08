#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace cas {
namespace simd {

// Returns true if any of the 4 uint64_t nets differ. If diff_mask is non-null, stores XOR differences.
inline bool simd_diff_check_256(const uint64_t* a, const uint64_t* b, uint64_t* diff_mask = nullptr) {
#if defined(__AVX2__)
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b));
    __m256i cmp = _mm256_cmpeq_epi64(va, vb);
    int mask = _mm256_movemask_epi8(cmp);
    // If mask is -1 (0xFFFFFFFF), all 4 64-bit values are identical
    if (mask != -1) {
        if (diff_mask) {
            __m256i diff = _mm256_xor_si256(va, vb);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(diff_mask), diff);
        }
        return true;
    }
    return false;
#else
    bool diff = false;
    for (int i = 0; i < 4; ++i) {
        uint64_t d = a[i] ^ b[i];
        if (diff_mask) diff_mask[i] = d;
        if (d != 0) diff = true;
    }
    return diff;
#endif
}

// 4 x 64-bit Bitwise AND
inline void simd_and_256(const uint64_t* a, const uint64_t* b, uint64_t* out) {
#if defined(__AVX2__)
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm256_and_si256(va, vb));
#else
    for (int i = 0; i < 4; ++i) out[i] = a[i] & b[i];
#endif
}

// 4 x 64-bit Bitwise OR
inline void simd_or_256(const uint64_t* a, const uint64_t* b, uint64_t* out) {
#if defined(__AVX2__)
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm256_or_si256(va, vb));
#else
    for (int i = 0; i < 4; ++i) out[i] = a[i] | b[i];
#endif
}

// 4 x 64-bit Bitwise XOR
inline void simd_xor_256(const uint64_t* a, const uint64_t* b, uint64_t* out) {
#if defined(__AVX2__)
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm256_xor_si256(va, vb));
#else
    for (int i = 0; i < 4; ++i) out[i] = a[i] ^ b[i];
#endif
}

// 4 x 64-bit Bitwise NOT
inline void simd_not_256(const uint64_t* a, uint64_t* out) {
#if defined(__AVX2__)
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
    __m256i ones = _mm256_set1_epi64x(-1LL);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm256_xor_si256(va, ones));
#else
    for (int i = 0; i < 4; ++i) out[i] = ~a[i];
#endif
}

// Scans an 8-bit modified_mask array and appends dirty net indices into dirty_nets
void simd_extract_dirty_indices(
    const uint8_t* modified_mask,
    size_t count,
    std::vector<uint32_t>& dirty_nets
);

// Bulk compares cur vs next over count elements, populates modified_mask and dirty_nets
void simd_bulk_diff_sync(
    const uint64_t* cur,
    const uint64_t* next,
    uint8_t* modified_mask,
    std::vector<uint32_t>& dirty_nets,
    size_t count
);

} // namespace simd
} // namespace cas
