#pragma once
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

struct LogicVal {
    uint64_t aval; // Data bitplane
    uint64_t bval; // Unknown/High-Z control bitplane (0=known, 1=X/Z)
    uint32_t width;
    bool is_string = false;
    std::string str_val = "";
    bool is_real = false;
    double real_val = 0.0;
    bool is_signed = false;
    bool is_unpacked = false;
    std::vector<LogicVal> unpacked_elements;
    std::vector<uint64_t> wide_aval;
    std::vector<uint64_t> wide_bval;

    explicit LogicVal(uint32_t w = 1, uint64_t a = 0, uint64_t b = 0, bool s = false)
        : aval(a), bval(b), width(w), is_string(false), str_val(""), is_real(false), real_val(0.0), is_signed(s) {
        if (width > 64) {
            size_t nwords = (width + 63) / 64;
            wide_aval.assign(nwords, 0);
            wide_bval.assign(nwords, 0);
            wide_aval[0] = a;
            wide_bval[0] = b;
        }
        mask();
    }

    static LogicVal make_x(uint32_t w) {
        LogicVal v(w, ~0ULL, ~0ULL);
        if (w > 64) {
            size_t nwords = (w + 63) / 64;
            v.wide_aval.assign(nwords, ~0ULL);
            v.wide_bval.assign(nwords, ~0ULL);
            v.mask();
        }
        return v;
    }

    static LogicVal make_z(uint32_t w) {
        LogicVal v(w, 0ULL, ~0ULL);
        if (w > 64) {
            size_t nwords = (w + 63) / 64;
            v.wide_aval.assign(nwords, 0ULL);
            v.wide_bval.assign(nwords, ~0ULL);
            v.mask();
        }
        return v;
    }

    static LogicVal make_known(uint32_t w, uint64_t val, bool s = false) {
        return LogicVal(w, val, 0, s);
    }

    static LogicVal from_int(int32_t val) {
        return LogicVal(32, static_cast<uint64_t>(static_cast<uint32_t>(val)), 0, true);
    }

    static LogicVal make_string(const std::string& str) {
        LogicVal v(32, 0, 0);
        v.is_string = true;
        v.str_val = str;
        return v;
    }

    static LogicVal make_real(double val) {
        LogicVal v(64, static_cast<uint64_t>(val), 0);
        v.is_real = true;
        v.real_val = val;
        return v;
    }

    void mask() {
        if (is_real) return;
        if (width <= 64) {
            uint64_t m = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
            aval &= m;
            bval &= m;
            if (!wide_aval.empty()) {
                wide_aval.clear();
                wide_bval.clear();
            }
        } else {
            size_t nwords = (width + 63) / 64;
            if (wide_aval.size() != nwords) {
                wide_aval.resize(nwords, 0);
                wide_bval.resize(nwords, 0);
                if (nwords > 0) {
                    wide_aval[0] = aval;
                    wide_bval[0] = bval;
                }
            }
            uint32_t rem = width % 64;
            if (rem != 0) {
                uint64_t last_m = (1ULL << rem) - 1;
                wide_aval.back() &= last_m;
                wide_bval.back() &= last_m;
            }
            aval = wide_aval[0];
            bval = wide_bval[0];
        }
    }

    bool get_bit_a(uint32_t bit) const {
        if (bit >= width) return false;
        if (width <= 64) return (aval >> bit) & 1ULL;
        size_t idx = bit / 64;
        uint32_t rem = bit % 64;
        if (idx < wide_aval.size()) return (wide_aval[idx] >> rem) & 1ULL;
        return false;
    }

    bool get_bit_b(uint32_t bit) const {
        if (bit >= width) return false;
        if (width <= 64) return (bval >> bit) & 1ULL;
        size_t idx = bit / 64;
        uint32_t rem = bit % 64;
        if (idx < wide_bval.size()) return (wide_bval[idx] >> rem) & 1ULL;
        return false;
    }

    void set_bit(uint32_t bit, bool a, bool b) {
        if (bit >= width) return;
        if (width <= 64) {
            if (a) aval |= (1ULL << bit);
            else aval &= ~(1ULL << bit);
            if (b) bval |= (1ULL << bit);
            else bval &= ~(1ULL << bit);
            return;
        }
        size_t idx = bit / 64;
        uint32_t rem = bit % 64;
        if (idx >= wide_aval.size()) {
            wide_aval.resize(idx + 1, 0);
            wide_bval.resize(idx + 1, 0);
        }
        if (a) wide_aval[idx] |= (1ULL << rem);
        else wide_aval[idx] &= ~(1ULL << rem);
        if (b) wide_bval[idx] |= (1ULL << rem);
        else wide_bval[idx] &= ~(1ULL << rem);
        if (idx == 0) {
            aval = wide_aval[0];
            bval = wide_bval[0];
        }
    }

    LogicVal extract_slice(uint32_t offset, uint32_t slice_w) const {
        if (slice_w == 0) return LogicVal(0, 0, 0);
        if (width <= 64 && offset < 64 && (offset + slice_w <= 64)) {
            uint64_t m = (slice_w == 64) ? ~0ULL : ((1ULL << slice_w) - 1);
            return LogicVal(slice_w, (aval >> offset) & m, (bval >> offset) & m);
        }
        if (offset % 64 == 0 && slice_w <= 64) {
            size_t idx = offset / 64;
            if (idx < wide_aval.size()) {
                uint64_t m = (slice_w == 64) ? ~0ULL : ((1ULL << slice_w) - 1);
                return LogicVal(slice_w, wide_aval[idx] & m, wide_bval[idx] & m);
            }
        }
        LogicVal res(slice_w, 0, 0);
        for (uint32_t i = 0; i < slice_w; ++i) {
            uint32_t src_bit = offset + i;
            if (src_bit < width) {
                res.set_bit(i, get_bit_a(src_bit), get_bit_b(src_bit));
            } else {
                res.set_bit(i, 1, 1);
            }
        }
        res.mask();
        return res;
    }

    void deposit_slice(uint32_t offset, uint32_t slice_w, const LogicVal& val) {
        if (slice_w == 0) return;
        if (width <= 64 && offset < 64 && (offset + slice_w <= 64)) {
            uint64_t m = (slice_w == 64) ? ~0ULL : ((1ULL << slice_w) - 1);
            uint64_t clear_m = ~(m << offset);
            aval = (aval & clear_m) | ((val.aval & m) << offset);
            bval = (bval & clear_m) | ((val.bval & m) << offset);
            mask();
            return;
        }
        if (offset % 64 == 0 && slice_w <= 64) {
            size_t idx = offset / 64;
            if (idx < wide_aval.size()) {
                uint64_t m = (slice_w == 64) ? ~0ULL : ((1ULL << slice_w) - 1);
                uint64_t clear_m = ~m;
                wide_aval[idx] = (wide_aval[idx] & clear_m) | (val.aval & m);
                wide_bval[idx] = (wide_bval[idx] & clear_m) | (val.bval & m);
                if (idx == 0) {
                    aval = wide_aval[0];
                    bval = wide_bval[0];
                }
                mask();
                return;
            }
        }
        for (uint32_t i = 0; i < slice_w; ++i) {
            uint32_t dst_bit = offset + i;
            if (dst_bit < width) {
                bool a_bit = (i < val.width) ? val.get_bit_a(i) : 0;
                bool b_bit = (i < val.width) ? val.get_bit_b(i) : 0;
                set_bit(dst_bit, a_bit, b_bit);
            }
        }
        mask();
    }

    int64_t to_signed() const {
        if (width >= 64) return static_cast<int64_t>(aval);
        uint64_t sign_bit = 1ULL << (width - 1);
        if (aval & sign_bit) {
            return static_cast<int64_t>(aval | (~0ULL << width));
        }
        return static_cast<int64_t>(aval);
    }

    bool is_unknown() const {
        if (is_real) return false;
        if (width <= 64) return bval != 0;
        for (uint64_t b : wide_bval) {
            if (b != 0) return true;
        }
        return false;
    }

    bool to_bool() const {
        if (is_real) return real_val != 0.0;
        if (is_string) return !str_val.empty();
        if (is_unknown()) return false;
        if (width <= 64) return aval != 0;
        for (uint64_t a : wide_aval) {
            if (a != 0) return true;
        }
        return false;
    }

    bool is_true() const {
        return to_bool();
    }

    bool operator==(const LogicVal& o) const {
        if (is_real || o.is_real) {
            double r1 = is_real ? real_val : static_cast<double>(aval);
            double r2 = o.is_real ? o.real_val : static_cast<double>(o.aval);
            return r1 == r2;
        }
        if (is_string || o.is_string) {
            return str_val == o.str_val;
        }
        if (width != o.width) return false;
        if (width <= 64) {
            return (aval == o.aval) && (bval == o.bval);
        }
        if (wide_aval.size() != o.wide_aval.size()) return false;
        for (size_t i = 0; i < wide_aval.size(); ++i) {
            if (wide_aval[i] != o.wide_aval[i] || wide_bval[i] != o.wide_bval[i]) return false;
        }
        return true;
    }

    bool operator!=(const LogicVal& o) const {
        return !(*this == o);
    }

    std::string to_hex_string(bool unpadded = false) const {
        if (width <= 64 && !is_unknown()) {
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
            int bit_start = nib * 4;
            uint64_t a_nib = 0;
            uint64_t b_nib = 0;
            for (int b = 0; b < 4; ++b) {
                if (bit_start + b < (int)width) {
                    if (get_bit_a(bit_start + b)) a_nib |= (1ULL << b);
                    if (get_bit_b(bit_start + b)) b_nib |= (1ULL << b);
                }
            }
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
        s.reserve(width);
        for (int i = (int)width - 1; i >= 0; --i) {
            bool a = get_bit_a(i);
            bool b = get_bit_b(i);
            if (!b) {
                s += (a ? '1' : '0');
            } else {
                s += (a ? 'x' : 'z');
            }
        }
        return s;
    }
};

inline uint64_t compute_clog2(uint64_t val) {
    if (val <= 1) return 0;
    uint64_t v = val - 1;
    uint64_t r = 0;
    while (v > 0) {
        v >>= 1;
        r++;
    }
    return r;
}
