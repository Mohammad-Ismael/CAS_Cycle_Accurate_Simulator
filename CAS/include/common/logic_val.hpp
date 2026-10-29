#pragma once
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>

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

    std::string to_hex_string(bool unpadded = false) const {
        if (!is_unknown()) {
            std::stringstream ss;
            if (unpadded) {
                ss << std::hex << aval << std::dec;
            } else {
                int hex_width = (width + 3) / 4;
                ss << std::hex << std::setfill('0') << std::setw(hex_width) << aval << std::dec;
            }
            return ss.str();
        }
        int hex_width = (width + 3) / 4;
        std::string s;
        s.reserve(hex_width);
        for (int nib = hex_width - 1; nib >= 0; --nib) {
            int shift = nib * 4;
            uint64_t a_nib = (aval >> shift) & 0xF;
            uint64_t b_nib = (bval >> shift) & 0xF;
            if (b_nib == 0) {
                const char* hex_chars = "0123456789abcdef";
                s += hex_chars[a_nib];
            } else {
                bool has_x = false;
                for (int b = 0; b < 4; ++b) {
                    if ((b_nib >> b) & 1) {
                        if ((a_nib >> b) & 1) has_x = true;
                    }
                }
                s += (has_x ? 'x' : 'z');
            }
        }
        if (unpadded) {
            size_t first = s.find_first_not_of('0');
            if (first != std::string::npos) s = s.substr(first);
            else s = "0";
        }
        return s;
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