#include "kernel/simd_ops.hpp"

namespace cas {
namespace simd {

void simd_extract_dirty_indices(
    const uint8_t* modified_mask,
    size_t count,
    std::vector<uint32_t>& dirty_nets
) {
    size_t i = 0;
#if defined(__AVX2__)
    __m256i zero = _mm256_setzero_si256();
    for (; i + 32 <= count; i += 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(modified_mask + i));
        __m256i cmp = _mm256_cmpeq_epi8(v, zero);
        int mask = _mm256_movemask_epi8(cmp);
        // If mask is -1 (0xFFFFFFFF), all 32 bytes are 0 -> skip entire block!
        if (mask != -1) {
            for (size_t k = 0; k < 32; ++k) {
                if (modified_mask[i + k]) {
                    dirty_nets.push_back(static_cast<uint32_t>(i + k));
                }
            }
        }
    }
#endif
    for (; i < count; ++i) {
        if (modified_mask[i]) {
            dirty_nets.push_back(static_cast<uint32_t>(i));
        }
    }
}

void simd_bulk_diff_sync(
    const uint64_t* cur,
    const uint64_t* next,
    uint8_t* modified_mask,
    std::vector<uint32_t>& dirty_nets,
    size_t count
) {
    size_t i = 0;
#if defined(__AVX2__)
    for (; i + 4 <= count; i += 4) {
        __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur + i));
        __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(next + i));
        __m256i cmp = _mm256_cmpeq_epi64(va, vb);
        int mask = _mm256_movemask_epi8(cmp);
        // If mask is -1 (0xFFFFFFFF), all 4 64-bit values are identical -> skip!
        if (mask != -1) {
            for (size_t k = 0; k < 4; ++k) {
                if (cur[i + k] != next[i + k]) {
                    modified_mask[i + k] = 1;
                    dirty_nets.push_back(static_cast<uint32_t>(i + k));
                }
            }
        }
    }
#endif
    for (; i < count; ++i) {
        if (cur[i] != next[i]) {
            modified_mask[i] = 1;
            dirty_nets.push_back(static_cast<uint32_t>(i));
        }
    }
}

} // namespace simd
} // namespace cas
