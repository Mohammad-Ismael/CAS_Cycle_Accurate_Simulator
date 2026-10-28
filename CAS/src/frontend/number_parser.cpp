#include "../../include/frontend/number_parser.hpp"
#include <stdexcept>

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
    }
    return res;
}

ParsedNumber NumberParser::parse_simple_decimal(const std::string& num_str) {
    ParsedNumber res;
    res.width = 32;
    res.text = num_str;
    res.aval = std::stoull(num_str);
    res.bval = 0;
    return res;
}

ParsedNumber NumberParser::parse_sized_literal(uint32_t width, char base, const std::string& digits) {
    ParsedNumber res;
    res.width = width;
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
                // aval = 0, bval = 0
            } else if (ch == 'x' || ch == 'X') {
                res.aval |= 1; // 4-state X: a=1, b=1
                res.bval |= 1;
            } else if (ch == 'z' || ch == 'Z') {
                // 4-state Z: a=0, b=1
                res.bval |= 1;
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
            } else if (ch == 'z' || ch == 'Z') {
                res.bval |= 0xF;
            }
        }
    } else if (base == 'd' || base == 'D') {
        bool has_unk = false;
        for (char ch : digits) {
            if (ch == 'x' || ch == 'X' || ch == 'z' || ch == 'Z') {
                has_unk = true;
                break;
            }
        }
        if (has_unk) {
            uint64_t m = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
            res.aval = m;
            res.bval = m;
        } else {
            res.aval = std::stoull(digits);
        }
    }

    uint64_t m = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
    res.aval &= m;
    res.bval &= m;
    return res;
}