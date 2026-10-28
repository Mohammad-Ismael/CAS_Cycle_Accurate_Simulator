#pragma once
#include <cstdint>
#include <string>

struct ParsedNumber {
    uint64_t aval = 0;
    uint64_t bval = 0;
    uint32_t width = 32;
    std::string text;
};

class NumberParser {
public:
    static ParsedNumber parse_sized_literal(uint32_t width, char base, const std::string& digits);
    static ParsedNumber parse_unbased_literal(char lit);
    static ParsedNumber parse_simple_decimal(const std::string& num_str);
};