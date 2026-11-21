#include <stdint.h>
#include <stdio.h>
#include "svdpi.h"

// Scalar test: receives byte, shortint, int, longint and returns sum
int64_t c_sum_scalars(int8_t b, int16_t s, int32_t i, int64_t l) {
    return (int64_t)b + (int64_t)s + (int64_t)i + l;
}

// Inout/output scalar test: mutates inout and sets output
void c_transform_scalars(int32_t in_val, int32_t* inout_val, int32_t* out_val) {
    if (inout_val && out_val) {
        *out_val = in_val * 2;
        *inout_val = *inout_val + in_val;
    }
}

// Vector test: accepts 64-bit vector (svBitVecVal*), inverts chunks
void c_process_bitvec(const svBitVecVal* in_vec, svBitVecVal* out_vec) {
    if (in_vec && out_vec) {
        out_vec[0] = in_vec[0] ^ 0xAAAAAAAA;
        out_vec[1] = in_vec[1] ^ 0x55555555;
    }
}

// Logic vector test: checks 4-state logic vector using svPutBitselLogic
void c_process_logicvec(const svLogicVecVal* in_vec, svLogicVecVal* out_vec) {
    if (in_vec && out_vec) {
        out_vec[0].aval = in_vec[0].aval;
        out_vec[0].bval = in_vec[0].bval;
        svPutBitselLogic(out_vec, 0, sv_1);
    }
}
