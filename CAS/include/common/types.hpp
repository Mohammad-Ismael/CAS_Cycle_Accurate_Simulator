#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <algorithm>

enum class EdgeType {
    POSEDGE,
    NEGEDGE,
    LEVEL
};

enum class GateType {
    AND,
    OR,
    XOR,
    NAND,
    NOR,
    XNOR,
    NOT,
    BUF
};

struct LogicVal {
    uint64_t aval; // Data word
    uint64_t bval; // Control word: 0=known, 1=unknown/Z
    uint32_t width;

    LogicVal(uint32_t w = 1, uint64_t a = 0, uint64_t b = 0)
        : aval(a), bval(b), width(w) {
        mask();
    }

    static LogicVal make_x(uint32_t w) {
        uint64_t m = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        return LogicVal(w, m, m);
    }

    static LogicVal make_known(uint32_t w, uint64_t val) {
        return LogicVal(w, val, 0);
    }

    void mask() {
        uint64_t m = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
        aval &= m;
        bval &= m;
    }

    bool is_unknown() const {
        return bval != 0;
    }

    bool to_bool() const {
        if (is_unknown()) return false;
        return aval != 0;
    }

    bool operator==(const LogicVal& o) const {
        return (aval == o.aval) && (bval == o.bval) && (width == o.width);
    }

    bool operator!=(const LogicVal& o) const {
        return !(*this == o);
    }

    // 4-state bitwise NOT (~)
    LogicVal bitwise_not() const {
        uint64_t new_a = (~aval) & (~bval);
        return LogicVal(width, new_a, bval);
    }

    // 4-state bitwise AND (&)
    static LogicVal bitwise_and(const LogicVal& l, const LogicVal& r) {
        uint32_t max_w = std::max(l.width, r.width);
        uint64_t new_a = l.aval & r.aval;
        uint64_t l_zero = (~l.aval) & (~l.bval);
        uint64_t r_zero = (~r.aval) & (~r.bval);
        uint64_t any_zero = l_zero | r_zero;
        uint64_t new_b = (l.bval | r.bval) & (~any_zero);
        return LogicVal(max_w, new_a, new_b);
    }

    // 4-state bitwise OR (|)
    static LogicVal bitwise_or(const LogicVal& l, const LogicVal& r) {
        uint32_t max_w = std::max(l.width, r.width);
        uint64_t new_a = l.aval | r.aval;
        uint64_t l_one = l.aval & (~l.bval);
        uint64_t r_one = r.aval & (~r.bval);
        uint64_t any_one = l_one | r_one;
        uint64_t new_b = (l.bval | r.bval) & (~any_one);
        return LogicVal(max_w, new_a, new_b);
    }

    // 4-state bitwise XOR (^)
    static LogicVal bitwise_xor(const LogicVal& l, const LogicVal& r) {
        uint32_t max_w = std::max(l.width, r.width);
        uint64_t new_b = l.bval | r.bval;
        uint64_t new_a = (l.aval ^ r.aval) & (~new_b);
        return LogicVal(max_w, new_a, new_b);
    }

    // Logical Left Shift (<<)
    static LogicVal shift_left(const LogicVal& l, const LogicVal& r) {
        if (r.is_unknown()) return make_x(l.width);
        uint64_t sh = r.aval;
        if (sh >= l.width) return LogicVal(l.width, 0, 0);
        return LogicVal(l.width, l.aval << sh, l.bval << sh);
    }

    // Logical Right Shift (>>)
    static LogicVal shift_right(const LogicVal& l, const LogicVal& r) {
        if (r.is_unknown()) return make_x(l.width);
        uint64_t sh = r.aval;
        if (sh >= l.width) return LogicVal(l.width, 0, 0);
        return LogicVal(l.width, l.aval >> sh, l.bval >> sh);
    }

    // 4-state Equality (==)
    static LogicVal logical_eq(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return make_x(1);
        return make_known(1, (l.aval == r.aval) ? 1 : 0);
    }

    // 4-state Inequality (!=)
    static LogicVal logical_neq(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return make_x(1);
        return make_known(1, (l.aval != r.aval) ? 1 : 0);
    }

    // Relational Operators (<, <=, >, >=)
    static LogicVal rel_lt(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return make_x(1);
        return make_known(1, (l.aval < r.aval) ? 1 : 0);
    }

    static LogicVal rel_le(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return make_x(1);
        return make_known(1, (l.aval <= r.aval) ? 1 : 0);
    }

    static LogicVal rel_gt(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return make_x(1);
        return make_known(1, (l.aval > r.aval) ? 1 : 0);
    }

    static LogicVal rel_ge(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return make_x(1);
        return make_known(1, (l.aval >= r.aval) ? 1 : 0);
    }

    std::string to_bin_string() const {
        std::string s;
        for (int i = (int)width - 1; i >= 0; --i) {
            bool a = (aval >> i) & 1;
            bool b = (bval >> i) & 1;
            if (!b) {
                s += (a ? '1' : '0');
            } else {
                s += (a ? 'x' : 'z');
            }
        }
        return s;
    }
};