#pragma once
#include <string>
#include <cstdint>
#include <vector>

struct Net {
    std::string name;
    uint32_t width;
    uint64_t value;
    uint64_t prev_value;
    bool modified;
    std::string vcd_id; // ASCII character identifier for VCD dumping

    Net(std::string n, uint32_t w)
        : name(std::move(n)), width(w), value(0), prev_value(0), modified(false), vcd_id("") {}

    void set(uint64_t val) {
        uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
        uint64_t clamped = val & mask;
        if (value != clamped) {
            prev_value = value;
            value = clamped;
            modified = true;
        }
    }

    bool is_posedge() const {
        return (prev_value == 0 && value != 0);
    }

    bool is_negedge() const {
        return (prev_value != 0 && value == 0);
    }

    std::string to_bin_string() const {
        std::string s;
        for (int i = (int)width - 1; i >= 0; --i) {
            s += ((value >> i) & 1) ? '1' : '0';
        }
        return s;
    }
};