#include "kernel/dpi_runtime.hpp"
#include "engine.hpp"
#include "common/dpic_types.hpp"
#include <map>
#include <cstdarg>
#include <cstring>
#include <iostream>

namespace DpiRuntime {
    void initialize() {}
    void cleanup() {}
}

extern "C" {

const char* svDpiVersion(void) {
    return "1800-2023";
}

svScope svGetScope(void) {
    return DpiRegistry::get_active_scope();
}

svScope svSetScope(const svScope scope) {
    return DpiRegistry::set_active_scope(scope);
}

const char* svGetNameFromScope(const svScope scope) {
    if (!scope) return "";
    const SvScopeContext* ctx = static_cast<const SvScopeContext*>(scope);
    return ctx->scope_name.c_str();
}

svScope svGetScopeFromName(const char* scopeName) {
    if (!scopeName) return nullptr;
    return static_cast<svScope>(DpiRegistry::get_or_create_scope(scopeName));
}

int svPutUserData(const svScope scope, void* userKey, void* userData) {
    if (!scope || !userKey || !userData) return -1;
    SvScopeContext* ctx = static_cast<SvScopeContext*>(scope);
    ctx->user_data_map[userKey] = userData;
    return 0;
}

void* svGetUserData(const svScope scope, void* userKey) {
    if (!scope || !userKey) return nullptr;
    SvScopeContext* ctx = static_cast<SvScopeContext*>(scope);
    auto it = ctx->user_data_map.find(userKey);
    if (it != ctx->user_data_map.end()) {
        return it->second;
    }
    return nullptr;
}

svBit svGetBitselBit(const svBitVecVal* s, int i) {
    if (!s || i < 0) return sv_0;
    int chunk = i >> 5;
    int bit = i & 31;
    return (s[chunk] & (1U << bit)) ? sv_1 : sv_0;
}

svLogic svGetBitselLogic(const svLogicVecVal* s, int i) {
    if (!s || i < 0) return sv_x;
    int chunk = i >> 5;
    int bit = i & 31;
    uint32_t a = (s[chunk].aval >> bit) & 1U;
    uint32_t b = (s[chunk].bval >> bit) & 1U;
    if (b == 0) return a ? sv_1 : sv_0;
    else return a ? sv_x : sv_z;
}

void svPutBitselBit(svBitVecVal* d, int i, svBit s) {
    if (!d || i < 0) return;
    int chunk = i >> 5;
    int bit = i & 31;
    if (s == sv_1) d[chunk] |= (1U << bit);
    else d[chunk] &= ~(1U << bit);
}

void svPutBitselLogic(svLogicVecVal* d, int i, svLogic s) {
    if (!d || i < 0) return;
    int chunk = i >> 5;
    int bit = i & 31;
    uint32_t a_bit = (s == sv_1 || s == sv_x) ? 1U : 0U;
    uint32_t b_bit = (s == sv_z || s == sv_x) ? 1U : 0U;
    if (a_bit) d[chunk].aval |= (1U << bit);
    else d[chunk].aval &= ~(1U << bit);
    if (b_bit) d[chunk].bval |= (1U << bit);
    else d[chunk].bval &= ~(1U << bit);
}

void svGetPartselBit(svBitVecVal* d, const svBitVecVal* s, int i, int w) {
    if (!d || !s || w <= 0) return;
    int words = (w + 31) / 32;
    for (int word = 0; word < words; ++word) {
        d[word] = 0;
    }
    for (int bit = 0; bit < w; ++bit) {
        svBit b = svGetBitselBit(s, i + bit);
        svPutBitselBit(d, bit, b);
    }
}

void svGetPartselLogic(svLogicVecVal* d, const svLogicVecVal* s, int i, int w) {
    if (!d || !s || w <= 0) return;
    int words = (w + 31) / 32;
    for (int word = 0; word < words; ++word) {
        d[word].aval = 0;
        d[word].bval = 0;
    }
    for (int bit = 0; bit < w; ++bit) {
        svLogic b = svGetBitselLogic(s, i + bit);
        svPutBitselLogic(d, bit, b);
    }
}

void svPutPartselBit(svBitVecVal* d, const svBitVecVal s, int i, int w) {
    if (!d || w <= 0) return;
    if (w > 32) w = 32;
    for (int bit = 0; bit < w; ++bit) {
        svBit b = (s & (1U << bit)) ? sv_1 : sv_0;
        svPutBitselBit(d, i + bit, b);
    }
}

void svPutPartselLogic(svLogicVecVal* d, const svLogicVecVal s, int i, int w) {
    if (!d || w <= 0) return;
    if (w > 32) w = 32;
    for (int bit = 0; bit < w; ++bit) {
        uint32_t a = (s.aval >> bit) & 1U;
        uint32_t b = (s.bval >> bit) & 1U;
        svLogic val = sv_0;
        if (b == 0) val = a ? sv_1 : sv_0;
        else val = a ? sv_x : sv_z;
        svPutBitselLogic(d, i + bit, val);
    }
}

int svDimensions(const svOpenArrayHandle h) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->dims.empty() ? 1 : static_cast<int>(arr->dims.size());
}

int svLeft(const svOpenArrayHandle h, int d) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    if (d < 1 || static_cast<size_t>(d) > arr->dims.size()) return 0;
    return arr->dims[d - 1].left;
}

int svRight(const svOpenArrayHandle h, int d) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    if (d < 1 || static_cast<size_t>(d) > arr->dims.size()) return 0;
    return arr->dims[d - 1].right;
}

int svLow(const svOpenArrayHandle h, int d) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    if (d < 1 || static_cast<size_t>(d) > arr->dims.size()) return 0;
    return arr->dims[d - 1].low;
}

int svHigh(const svOpenArrayHandle h, int d) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    if (d < 1 || static_cast<size_t>(d) > arr->dims.size()) return 0;
    return arr->dims[d - 1].high;
}

int svIncrement(const svOpenArrayHandle h, int d) {
    if (!h) return 1;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    if (d < 1 || static_cast<size_t>(d) > arr->dims.size()) return 1;
    return arr->dims[d - 1].increment;
}

int svLength(const svOpenArrayHandle h, int d) {
    return svSize(h, d);
}

int svSize(const svOpenArrayHandle h, int d) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    if (d < 1 || static_cast<size_t>(d) > arr->dims.size()) return 0;
    return arr->dims[d - 1].size;
}

int svSizeOfArray(const svOpenArrayHandle h) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return static_cast<int>(arr->buffer.size());
}

void* svGetArrElemPtr1(const svOpenArrayHandle h, int indx) {
    if (!h) return nullptr;
    SvOpenArray* arr = const_cast<SvOpenArray*>(reinterpret_cast<const SvOpenArray*>(h));
    return arr->get_elem_ptr(indx);
}

void* svGetArrElemPtr2(const svOpenArrayHandle h, int indx1, int indx2) {
    if (!h) return nullptr;
    SvOpenArray* arr = const_cast<SvOpenArray*>(reinterpret_cast<const SvOpenArray*>(h));
    return arr->get_elem_ptr2(indx1, indx2);
}

void* svGetArrElemPtr3(const svOpenArrayHandle h, int indx1, int indx2, int indx3) {
    if (!h) return nullptr;
    SvOpenArray* arr = const_cast<SvOpenArray*>(reinterpret_cast<const SvOpenArray*>(h));
    return arr->get_elem_ptr3(indx1, indx2, indx3);
}

static void* svGetArrElemPtr_v(const svOpenArrayHandle h, int indx1, va_list args) {
    if (!h) return nullptr;
    SvOpenArray* arr = const_cast<SvOpenArray*>(reinterpret_cast<const SvOpenArray*>(h));
    if (arr->dims.empty()) return nullptr;
    if (arr->dims.size() == 1) return arr->get_elem_ptr(indx1);
    int cur_idx = indx1;
    size_t linear = 0;
    for (size_t d = 0; d < arr->dims.size(); ++d) {
        if (d > 0) cur_idx = va_arg(args, int);
        const auto& dim = arr->dims[d];
        int off = (dim.left >= dim.right) ? (dim.left - cur_idx) : (cur_idx - dim.left);
        if (off < 0 || off >= dim.size) {
            return nullptr;
        }
        linear = linear * dim.size + off;
    }
    size_t byte_off = linear * arr->elem_stride;
    if (byte_off + arr->elem_stride > arr->buffer.size()) return nullptr;
    return arr->buffer.data() + byte_off;
}

void* svGetArrElemPtr(const svOpenArrayHandle h, int indx1, ...) {
    va_list args;
    va_start(args, indx1);
    void* ptr = svGetArrElemPtr_v(h, indx1, args);
    va_end(args);
    return ptr;
}

void* svGetArrayPtr(const svOpenArrayHandle h) {
    if (!h) return nullptr;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return const_cast<uint8_t*>(arr->buffer.data());
}

void svPutBitArrElem1VecVal(const svOpenArrayHandle d, const svBitVecVal* s, int indx1) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr1(d, indx1);
    if (ptr) {
        std::memcpy(ptr, s, reinterpret_cast<const SvOpenArray*>(d)->elem_stride);
    }
}

void svGetBitArrElem1VecVal(svBitVecVal* d, const svOpenArrayHandle s, int indx1) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr1(s, indx1);
    if (ptr) {
        std::memcpy(d, ptr, reinterpret_cast<const SvOpenArray*>(s)->elem_stride);
    }
}

void svPutBitArrElem2VecVal(const svOpenArrayHandle d, const svBitVecVal* s, int indx1, int indx2) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr2(d, indx1, indx2);
    if (ptr) {
        std::memcpy(ptr, s, reinterpret_cast<const SvOpenArray*>(d)->elem_stride);
    }
}

void svGetBitArrElem2VecVal(svBitVecVal* d, const svOpenArrayHandle s, int indx1, int indx2) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr2(s, indx1, indx2);
    if (ptr) {
        std::memcpy(d, ptr, reinterpret_cast<const SvOpenArray*>(s)->elem_stride);
    }
}

void svPutBitArrElem3VecVal(const svOpenArrayHandle d, const svBitVecVal* s, int indx1, int indx2, int indx3) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr3(d, indx1, indx2, indx3);
    if (ptr) {
        std::memcpy(ptr, s, reinterpret_cast<const SvOpenArray*>(d)->elem_stride);
    }
}

void svGetBitArrElem3VecVal(svBitVecVal* d, const svOpenArrayHandle s, int indx1, int indx2, int indx3) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr3(s, indx1, indx2, indx3);
    if (ptr) {
        std::memcpy(d, ptr, reinterpret_cast<const SvOpenArray*>(s)->elem_stride);
    }
}

void svPutBitArrElemVecVal(const svOpenArrayHandle d, const svBitVecVal* s, int indx1, ...) {
    if (!d || !s) return;
    va_list args;
    va_start(args, indx1);
    void* ptr = svGetArrElemPtr_v(d, indx1, args);
    va_end(args);
    if (ptr) {
        std::memcpy(ptr, s, reinterpret_cast<const SvOpenArray*>(d)->elem_stride);
    }
}

void svGetBitArrElemVecVal(svBitVecVal* d, const svOpenArrayHandle s, int indx1, ...) {
    if (!d || !s) return;
    va_list args;
    va_start(args, indx1);
    void* ptr = svGetArrElemPtr_v(s, indx1, args);
    va_end(args);
    if (ptr) {
        std::memcpy(d, ptr, reinterpret_cast<const SvOpenArray*>(s)->elem_stride);
    }
}

void svPutLogicArrElem1VecVal(const svOpenArrayHandle d, const svLogicVecVal* s, int indx1) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr1(d, indx1);
    if (ptr) {
        std::memcpy(ptr, s, reinterpret_cast<const SvOpenArray*>(d)->elem_stride);
    }
}

void svGetLogicArrElem1VecVal(svLogicVecVal* d, const svOpenArrayHandle s, int indx1) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr1(s, indx1);
    if (ptr) {
        std::memcpy(d, ptr, reinterpret_cast<const SvOpenArray*>(s)->elem_stride);
    }
}

void svPutLogicArrElem2VecVal(const svOpenArrayHandle d, const svLogicVecVal* s, int indx1, int indx2) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr2(d, indx1, indx2);
    if (ptr) {
        std::memcpy(ptr, s, reinterpret_cast<const SvOpenArray*>(d)->elem_stride);
    }
}

void svGetLogicArrElem2VecVal(svLogicVecVal* d, const svOpenArrayHandle s, int indx1, int indx2) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr2(s, indx1, indx2);
    if (ptr) {
        std::memcpy(d, ptr, reinterpret_cast<const SvOpenArray*>(s)->elem_stride);
    }
}

void svPutLogicArrElem3VecVal(const svOpenArrayHandle d, const svLogicVecVal* s, int indx1, int indx2, int indx3) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr3(d, indx1, indx2, indx3);
    if (ptr) {
        std::memcpy(ptr, s, reinterpret_cast<const SvOpenArray*>(d)->elem_stride);
    }
}

void svGetLogicArrElem3VecVal(svLogicVecVal* d, const svOpenArrayHandle s, int indx1, int indx2, int indx3) {
    if (!d || !s) return;
    void* ptr = svGetArrElemPtr3(s, indx1, indx2, indx3);
    if (ptr) {
        std::memcpy(d, ptr, reinterpret_cast<const SvOpenArray*>(s)->elem_stride);
    }
}

void svPutLogicArrElemVecVal(const svOpenArrayHandle d, const svLogicVecVal* s, int indx1, ...) {
    if (!d || !s) return;
    va_list args;
    va_start(args, indx1);
    void* ptr = svGetArrElemPtr_v(d, indx1, args);
    va_end(args);
    if (ptr) {
        std::memcpy(ptr, s, reinterpret_cast<const SvOpenArray*>(d)->elem_stride);
    }
}

void svGetLogicArrElemVecVal(svLogicVecVal* d, const svOpenArrayHandle s, int indx1, ...) {
    if (!d || !s) return;
    va_list args;
    va_start(args, indx1);
    void* ptr = svGetArrElemPtr_v(s, indx1, args);
    va_end(args);
    if (ptr) {
        std::memcpy(d, ptr, reinterpret_cast<const SvOpenArray*>(s)->elem_stride);
    }
}

svBit svGetBitArrElem1(const svOpenArrayHandle s, int indx1) {
    if (!s) return sv_0;
    void* ptr = svGetArrElemPtr1(s, indx1);
    if (!ptr) return sv_0;
    const uint32_t* u = reinterpret_cast<const uint32_t*>(ptr);
    return (*u & 1) ? sv_1 : sv_0;
}

void svPutBitArrElem1(const svOpenArrayHandle d, svBit value, int indx1) {
    if (!d) return;
    void* ptr = svGetArrElemPtr1(d, indx1);
    if (!ptr) return;
    uint32_t* u = reinterpret_cast<uint32_t*>(ptr);
    if (value == sv_1) *u |= 1U;
    else *u &= ~1U;
}

svLogic svGetLogicArrElem1(const svOpenArrayHandle s, int indx1) {
    if (!s) return sv_x;
    void* ptr = svGetArrElemPtr1(s, indx1);
    if (!ptr) return sv_x;
    const svLogicVecVal* lv = reinterpret_cast<const svLogicVecVal*>(ptr);
    uint32_t a = lv->aval & 1U;
    uint32_t b = lv->bval & 1U;
    if (b == 0) return a ? sv_1 : sv_0;
    else return a ? sv_x : sv_z;
}

void svPutLogicArrElem1(const svOpenArrayHandle d, svLogic value, int indx1) {
    if (!d) return;
    void* ptr = svGetArrElemPtr1(d, indx1);
    if (!ptr) return;
    svLogicVecVal* lv = reinterpret_cast<svLogicVecVal*>(ptr);
    uint32_t a = (value == sv_1 || value == sv_x) ? 1U : 0U;
    uint32_t b = (value == sv_z || value == sv_x) ? 1U : 0U;
    if (a) lv->aval |= 1U; else lv->aval &= ~1U;
    if (b) lv->bval |= 1U; else lv->bval &= ~1U;
}

svBit svGetBitArrElem(const svOpenArrayHandle s, int indx1, ...) {
    if (!s) return sv_0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(s);
    if (arr->dims.empty()) return sv_0;
    va_list args;
    va_start(args, indx1);
    int64_t linear = 0;
    int cur_idx = indx1;
    for (size_t d = 0; d < arr->dims.size(); ++d) {
        if (d > 0) cur_idx = va_arg(args, int);
        const auto& dim = arr->dims[d];
        int off = (dim.left >= dim.right) ? (dim.left - cur_idx) : (cur_idx - dim.left);
        if (off < 0 || off >= dim.size) {
            va_end(args);
            return sv_0;
        }
        linear = linear * dim.size + off;
    }
    va_end(args);
    size_t byte_off = linear * arr->elem_stride;
    if (byte_off + arr->elem_stride > arr->buffer.size()) return sv_0;
    const uint32_t* u = reinterpret_cast<const uint32_t*>(arr->buffer.data() + byte_off);
    return (*u & 1) ? sv_1 : sv_0;
}

svBit svGetBitArrElem2(const svOpenArrayHandle s, int indx1, int indx2) {
    return svGetBitArrElem(s, indx1, indx2);
}

svBit svGetBitArrElem3(const svOpenArrayHandle s, int indx1, int indx2, int indx3) {
    return svGetBitArrElem(s, indx1, indx2, indx3);
}

void svPutBitArrElem(const svOpenArrayHandle d, svBit value, int indx1, ...) {
    if (!d) return;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(d);
    if (arr->dims.empty()) return;
    va_list args;
    va_start(args, indx1);
    int64_t linear = 0;
    int cur_idx = indx1;
    for (size_t d_idx = 0; d_idx < arr->dims.size(); ++d_idx) {
        if (d_idx > 0) cur_idx = va_arg(args, int);
        const auto& dim = arr->dims[d_idx];
        int off = (dim.left >= dim.right) ? (dim.left - cur_idx) : (cur_idx - dim.left);
        if (off < 0 || off >= dim.size) {
            va_end(args);
            return;
        }
        linear = linear * dim.size + off;
    }
    va_end(args);
    size_t byte_off = linear * arr->elem_stride;
    if (byte_off + arr->elem_stride > arr->buffer.size()) return;
    uint32_t* u = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(arr->buffer.data()) + byte_off);
    if (value == sv_1) *u |= 1U;
    else *u &= ~1U;
}

void svPutBitArrElem2(const svOpenArrayHandle d, svBit value, int indx1, int indx2) {
    svPutBitArrElem(d, value, indx1, indx2);
}

void svPutBitArrElem3(const svOpenArrayHandle d, svBit value, int indx1, int indx2, int indx3) {
    svPutBitArrElem(d, value, indx1, indx2, indx3);
}

svLogic svGetLogicArrElem(const svOpenArrayHandle s, int indx1, ...) {
    if (!s) return sv_x;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(s);
    if (arr->dims.empty()) return sv_x;
    va_list args;
    va_start(args, indx1);
    int64_t linear = 0;
    int cur_idx = indx1;
    for (size_t d = 0; d < arr->dims.size(); ++d) {
        if (d > 0) cur_idx = va_arg(args, int);
        const auto& dim = arr->dims[d];
        int off = (dim.left >= dim.right) ? (dim.left - cur_idx) : (cur_idx - dim.left);
        if (off < 0 || off >= dim.size) {
            va_end(args);
            return sv_x;
        }
        linear = linear * dim.size + off;
    }
    va_end(args);
    size_t byte_off = linear * arr->elem_stride;
    if (byte_off + arr->elem_stride > arr->buffer.size()) return sv_x;
    const svLogicVecVal* lv = reinterpret_cast<const svLogicVecVal*>(arr->buffer.data() + byte_off);
    uint32_t a = lv->aval & 1U;
    uint32_t b = lv->bval & 1U;
    if (b == 0) return a ? sv_1 : sv_0;
    else return a ? sv_x : sv_z;
}

svLogic svGetLogicArrElem2(const svOpenArrayHandle s, int indx1, int indx2) {
    return svGetLogicArrElem(s, indx1, indx2);
}

svLogic svGetLogicArrElem3(const svOpenArrayHandle s, int indx1, int indx2, int indx3) {
    return svGetLogicArrElem(s, indx1, indx2, indx3);
}

void svPutLogicArrElem(const svOpenArrayHandle d, svLogic value, int indx1, ...) {
    if (!d) return;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(d);
    if (arr->dims.empty()) return;
    va_list args;
    va_start(args, indx1);
    int64_t linear = 0;
    int cur_idx = indx1;
    for (size_t d_idx = 0; d_idx < arr->dims.size(); ++d_idx) {
        if (d_idx > 0) cur_idx = va_arg(args, int);
        const auto& dim = arr->dims[d_idx];
        int off = (dim.left >= dim.right) ? (dim.left - cur_idx) : (cur_idx - dim.left);
        if (off < 0 || off >= dim.size) {
            va_end(args);
            return;
        }
        linear = linear * dim.size + off;
    }
    va_end(args);
    size_t byte_off = linear * arr->elem_stride;
    if (byte_off + arr->elem_stride > arr->buffer.size()) return;
    svLogicVecVal* lv = reinterpret_cast<svLogicVecVal*>(const_cast<uint8_t*>(arr->buffer.data()) + byte_off);
    uint32_t a = (value == sv_1 || value == sv_x) ? 1U : 0U;
    uint32_t b = (value == sv_z || value == sv_x) ? 1U : 0U;
    if (a) lv->aval |= 1U; else lv->aval &= ~1U;
    if (b) lv->bval |= 1U; else lv->bval &= ~1U;
}

void svPutLogicArrElem2(const svOpenArrayHandle d, svLogic value, int indx1, int indx2) {
    svPutLogicArrElem(d, value, indx1, indx2);
}

void svPutLogicArrElem3(const svOpenArrayHandle d, svLogic value, int indx1, int indx2, int indx3) {
    svPutLogicArrElem(d, value, indx1, indx2, indx3);
}

int svIsDisabledState(void) {
    return 0;
}

void svAckDisabledState(void) {}

int svGetTime(const svScope /*scope*/, svTimeVal* time) {
    if (!time) return -1;
    Engine* eng = static_cast<Engine*>(DpiRegistry::get_active_engine());
    uint64_t t = eng ? eng->get_sim_time() : 0;
    time->type = 2; // vpiSimTime
    time->high = static_cast<uint32_t>(t >> 32);
    time->low  = static_cast<uint32_t>(t & 0xFFFFFFFFULL);
    time->real = static_cast<double>(t);
    return 0;
}

int svGetTimeUnit(const svScope /*scope*/, int32_t* time_unit) {
    if (!time_unit) return -1;
    *time_unit = -9; // 1ns default
    return 0;
}

int svGetTimePrecision(const svScope /*scope*/, int32_t* time_precision) {
    if (!time_precision) return -1;
    *time_precision = -12; // 1ps default
    return 0;
}

int svGetCallerInfo(const char** fileName, int* lineNumber) {
    if (!fileName || !lineNumber) return 0;
    *fileName = "unknown.sv";
    *lineNumber = 0;
    return 1;
}

uint64_t cas_dispatch_dpi_export(const char* name, int argc, const uint64_t* args) {
    Engine* eng = static_cast<Engine*>(DpiRegistry::get_active_engine());
    if (!eng) {
        throw std::runtime_error(std::string("[CAS-DPI-ERR] cas_dispatch_dpi_export called without active simulation engine context for '") + (name ? name : "NULL") + "'");
    }
    std::vector<LogicVal> l_args;
    for (int i = 0; i < argc; ++i) {
        l_args.push_back(LogicVal::make_known(32, static_cast<uint32_t>(args[i])));
    }
    LogicVal res = eng->dispatch_dpi_export(name ? name : "", l_args);
    if (res.is_real) {
        double d = res.to_real();
        uint64_t ret_bits = 0;
        std::memcpy(&ret_bits, &d, sizeof(double));
        return ret_bits;
    }
    return res.aval;
}

void sv_write_reg(int addr, int data) {
    Engine* eng = static_cast<Engine*>(DpiRegistry::get_active_engine());
    if (!eng) {
        throw std::runtime_error("[CAS-DPI-ERR] sv_write_reg called without active simulation engine context");
    }
    eng->dispatch_dpi_export("sv_write_reg", {LogicVal(32, static_cast<uint32_t>(addr)), LogicVal(32, static_cast<uint32_t>(data))});
}

int sv_read_reg(int addr) {
    Engine* eng = static_cast<Engine*>(DpiRegistry::get_active_engine());
    if (!eng) {
        throw std::runtime_error("[CAS-DPI-ERR] sv_read_reg called without active simulation engine context");
    }
    LogicVal res = eng->dispatch_dpi_export("sv_read_reg", {LogicVal(32, static_cast<uint32_t>(addr))});
    return static_cast<int>(static_cast<int32_t>(res.aval));
}

int sv_compute_checksum(int a, int b) {
    uint64_t args[2] = { static_cast<uint64_t>(static_cast<uint32_t>(a)), static_cast<uint64_t>(static_cast<uint32_t>(b)) };
    return static_cast<int>(cas_dispatch_dpi_export("sv_compute_checksum", 2, args));
}

void sv_callback(int code) {
    uint64_t args[1] = { static_cast<uint64_t>(static_cast<uint32_t>(code)) };
    cas_dispatch_dpi_export("sv_callback", 1, args);
}

int sv_get_status(void) {
    return static_cast<int>(cas_dispatch_dpi_export("sv_get_status", 0, nullptr));
}

void sv_set_status(int status) {
    uint64_t args[1] = { static_cast<uint64_t>(static_cast<uint32_t>(status)) };
    cas_dispatch_dpi_export("sv_set_status", 1, args);
}

char read_symbol(const char* symbol_name, unsigned long long* address) {
    if (!address || !symbol_name) return 0;
#ifndef _WIN32
#ifdef RTLD_DEFAULT
    void* sym_map_ptr = dlsym(RTLD_DEFAULT, "_Z7symbolsB5cxx11");
    if (sym_map_ptr) {
        auto* sym_map = reinterpret_cast<std::map<std::string, uint64_t>*>(sym_map_ptr);
        auto it = sym_map->find(symbol_name);
        if (it != sym_map->end()) {
            *address = it->second;
            return 1;
        }
    }
#endif
#endif
    if (std::strcmp(symbol_name, "tohost") == 0) {
        *address = 0x80001000ULL;
        return 1;
    }
    return 0;
}

char cas_read_symbol(const char* symbol_name, unsigned long long* address) {
    return read_symbol(symbol_name, address);
}

} // extern "C"
