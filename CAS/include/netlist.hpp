#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct LogicVal {
    uint64_t aval; // Data word
    uint64_t bval; // 4-state control word: 0=known, 1=unknown/Z
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

struct Net {
    std::string name;
    uint32_t width;
    LogicVal value;
    LogicVal prev_value;
    bool modified;
    std::string vcd_id;

    Net(std::string n, uint32_t w)
        : name(std::move(n)), width(w),
          value(LogicVal::make_x(w)),
          prev_value(LogicVal::make_x(w)),
          modified(false), vcd_id("") {}

    void set(const LogicVal& val) {
        LogicVal clamped(width, val.aval, val.bval);
        if (value != clamped) {
            prev_value = value;
            value = clamped;
            modified = true;
        }
    }

    bool is_posedge() const {
        bool curr_1 = (value.bval == 0 && (value.aval & 1) == 1);
        bool prev_not_1 = (prev_value.bval != 0) || ((prev_value.aval & 1) == 0);
        return prev_not_1 && curr_1;
    }

    bool is_negedge() const {
        bool curr_0 = (value.bval == 0 && (value.aval & 1) == 0);
        bool prev_not_0 = (prev_value.bval != 0) || ((prev_value.aval & 1) == 1);
        return prev_not_0 && curr_0;
    }
};