#pragma once
#include <cstdint>
#include <string>

struct LogicVal {
    uint64_t aval; // Data bitplane
    uint64_t bval; // Unknown/High-Z control bitplane (0=known, 1=X/Z)
    uint32_t width;

    explicit LogicVal(uint32_t w = 1, uint64_t a = 0, uint64_t b = 0)
        : aval(a), bval(b), width(w) {
        mask();
    }

    static LogicVal make_x(uint32_t w) {
        uint64_t m = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        return LogicVal(w, m, m); // 4-state X: a=1, b=1
    }

    static LogicVal make_z(uint32_t w) {
        uint64_t m = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        return LogicVal(w, 0, m); // 4-state Z: a=0, b=1
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