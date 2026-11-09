#include "../../include/frontend/number_parser.hpp"
#include <stdexcept>
#include <cctype>

ParsedNumber NumberParser::parse_unbased_literal(char lit) {
    ParsedNumber res;
    res.width = 64;
    res.text = std::string("'") + lit;

    if (lit == '0') {
        res.aval = 0;
        res.bval = 0;
    } else if (lit == '1') {
        res.aval = ~0ULL;
        res.bval = 0;
    } else if (lit == 'x' || lit == 'X') {
        res.aval = ~0ULL;
        res.bval = ~0ULL;
    } else if (lit == 'z' || lit == 'Z') {
        res.aval = 0;
        res.bval = ~0ULL;
    } else {
        throw std::runtime_error("Invalid unbased unsized literal: '" + std::string(1, lit) + "'. IEEE Std 1800-2023 §5.7.1 restricts unbased literals to '0, '1, 'z/'Z, and 'x/'X.");
    }
    return res;
}

ParsedNumber NumberParser::parse_simple_decimal(const std::string& num_str) {
    ParsedNumber res;
    res.width = 32;
    res.text = num_str;
    std::string clean;
    clean.reserve(num_str.size());
    for (char c : num_str) {
        if (c != '_') clean.push_back(c);
    }
    res.aval = clean.empty() ? 0 : std::stoull(clean);
    res.bval = 0;
    res.is_signed = true;
    return res;
}

ParsedNumber NumberParser::parse_sized_literal(uint32_t width, bool is_signed, char base, const std::string& digits) {
    ParsedNumber res;
    res.width = width;
    res.is_signed = is_signed;
    res.text = digits;
    res.aval = 0;
    res.bval = 0;

    if (base == 'b' || base == 'B') {
        for (char ch : digits) {
            res.aval <<= 1;
            res.bval <<= 1;
            if (ch == '1') {
                res.aval |= 1;
            } else if (ch == '0') {
                // 0, 0
            } else if (ch == 'x' || ch == 'X') {
                res.aval |= 1;
                res.bval |= 1;
            } else if (ch == 'z' || ch == 'Z' || ch == '?') {
                res.bval |= 1;
            }
        }
        uint32_t parsed_bits = static_cast<uint32_t>(digits.size());
        if (parsed_bits < width && !digits.empty()) {
            char msb_char = digits[0];
            if (msb_char == 'x' || msb_char == 'X') {
                for (uint32_t b = parsed_bits; b < width && b < 64; ++b) {
                    res.aval |= (1ULL << b);
                    res.bval |= (1ULL << b);
                }
            } else if (msb_char == 'z' || msb_char == 'Z' || msb_char == '?') {
                for (uint32_t b = parsed_bits; b < width && b < 64; ++b) {
                    res.bval |= (1ULL << b);
                }
            }
        }
    } else if (base == 'o' || base == 'O') {
        for (char ch : digits) {
            res.aval <<= 3;
            res.bval <<= 3;
            if (ch >= '0' && ch <= '7') {
                res.aval |= (ch - '0');
            } else if (ch == 'x' || ch == 'X') {
                res.aval |= 0x7;
                res.bval |= 0x7;
            } else if (ch == 'z' || ch == 'Z' || ch == '?') {
                res.bval |= 0x7;
            }
        }
        uint32_t parsed_bits = static_cast<uint32_t>(digits.size() * 3);
        if (parsed_bits < width && !digits.empty()) {
            char msb_char = digits[0];
            if (msb_char == 'x' || msb_char == 'X') {
                for (uint32_t b = parsed_bits; b < width && b < 64; ++b) {
                    res.aval |= (1ULL << b);
                    res.bval |= (1ULL << b);
                }
            } else if (msb_char == 'z' || msb_char == 'Z' || msb_char == '?') {
                for (uint32_t b = parsed_bits; b < width && b < 64; ++b) {
                    res.bval |= (1ULL << b);
                }
            }
        }
    } else if (base == 'h' || base == 'H') {
        for (char ch : digits) {
            res.aval <<= 4;
            res.bval <<= 4;
            if (ch >= '0' && ch <= '9') {
                res.aval |= (ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                res.aval |= (ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                res.aval |= (ch - 'A' + 10);
            } else if (ch == 'x' || ch == 'X') {
                res.aval |= 0xF;
                res.bval |= 0xF;
            } else if (ch == 'z' || ch == 'Z' || ch == '?') {
                res.bval |= 0xF;
            }
        }
        uint32_t parsed_bits = static_cast<uint32_t>(digits.size() * 4);
        if (parsed_bits < width && !digits.empty()) {
            char msb_char = digits[0];
            if (msb_char == 'x' || msb_char == 'X') {
                for (uint32_t b = parsed_bits; b < width && b < 64; ++b) {
                    res.aval |= (1ULL << b);
                    res.bval |= (1ULL << b);
                }
            } else if (msb_char == 'z' || msb_char == 'Z' || msb_char == '?') {
                for (uint32_t b = parsed_bits; b < width && b < 64; ++b) {
                    res.bval |= (1ULL << b);
                }
            }
        }
    } else if (base == 'd' || base == 'D') {
        bool has_unk = false;
        for (char ch : digits) {
            if (ch == 'x' || ch == 'X' || ch == 'z' || ch == 'Z' || ch == '?') {
                has_unk = true;
                break;
            }
        }
        if (has_unk) {
            uint64_t m = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
            res.aval = m;
            res.bval = m;
        } else {
            std::string clean;
            clean.reserve(digits.size());
            for (char c : digits) {
                if (c != '_') clean.push_back(c);
            }
            res.aval = clean.empty() ? 0 : std::stoull(clean);
        }
    }

    if (width > 64) {
        size_t nwords = (width + 63) / 64;
        res.wide_aval.assign(nwords, 0ULL);
        res.wide_bval.assign(nwords, 0ULL);
        if (base == 'h' || base == 'H') {
            size_t len = digits.size();
            for (size_t i = 0; i < len; ++i) {
                char ch = digits[len - 1 - i];
                uint64_t a_nib = 0, b_nib = 0;
                if (ch >= '0' && ch <= '9') { a_nib = ch - '0'; }
                else if (ch >= 'a' && ch <= 'f') { a_nib = ch - 'a' + 10; }
                else if (ch >= 'A' && ch <= 'F') { a_nib = ch - 'A' + 10; }
                else if (ch == 'x' || ch == 'X') { a_nib = 0xF; b_nib = 0xF; }
                else if (ch == 'z' || ch == 'Z' || ch == '?') { b_nib = 0xF; }

                size_t bit_pos = i * 4;
                size_t w_idx = bit_pos / 64;
                size_t shift = bit_pos % 64;
                if (w_idx < nwords) {
                    res.wide_aval[w_idx] |= (a_nib << shift);
                    res.wide_bval[w_idx] |= (b_nib << shift);
                    if (shift > 60 && w_idx + 1 < nwords) {
                        res.wide_aval[w_idx + 1] |= (a_nib >> (64 - shift));
                        res.wide_bval[w_idx + 1] |= (b_nib >> (64 - shift));
                    }
                }
            }
            uint32_t parsed_bits = static_cast<uint32_t>(digits.size() * 4);
            if (parsed_bits < width && !digits.empty()) {
                char msb_char = digits[0];
                if (msb_char == 'x' || msb_char == 'X' || msb_char == 'z' || msb_char == 'Z' || msb_char == '?') {
                    for (uint32_t b = parsed_bits; b < width; ++b) {
                        size_t w_idx = b / 64;
                        size_t bit = b % 64;
                        if (w_idx < nwords) {
                            if (msb_char == 'x' || msb_char == 'X') res.wide_aval[w_idx] |= (1ULL << bit);
                            res.wide_bval[w_idx] |= (1ULL << bit);
                        }
                    }
                }
            }
        } else if (base == 'b' || base == 'B') {
            size_t len = digits.size();
            for (size_t i = 0; i < len; ++i) {
                char ch = digits[len - 1 - i];
                size_t w_idx = i / 64;
                size_t bit = i % 64;
                if (w_idx < nwords) {
                    if (ch == '1') res.wide_aval[w_idx] |= (1ULL << bit);
                    else if (ch == 'x' || ch == 'X') { res.wide_aval[w_idx] |= (1ULL << bit); res.wide_bval[w_idx] |= (1ULL << bit); }
                    else if (ch == 'z' || ch == 'Z' || ch == '?') { res.wide_bval[w_idx] |= (1ULL << bit); }
                }
            }
        }
        size_t rem = width % 64;
        if (rem != 0) {
            uint64_t last_m = (1ULL << rem) - 1;
            res.wide_aval.back() &= last_m;
            res.wide_bval.back() &= last_m;
        }
        res.aval = res.wide_aval[0];
        res.bval = res.wide_bval[0];
    }

    uint64_t m = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
    res.aval &= m;
    res.bval &= m;
    return res;
}