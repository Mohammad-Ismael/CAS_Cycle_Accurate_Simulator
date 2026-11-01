#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

struct ArrayDimInfo {
    int64_t left = 0;
    int64_t right = 0;
    bool is_unpacked = true;
    bool is_dynamic = false;
    bool is_queue = false;

    int64_t low() const { return std::min(left, right); }
    int64_t high() const { return std::max(left, right); }
    int64_t size() const { return high() - low() + 1; }
    int64_t increment() const {
        if (is_dynamic || is_queue) return -1;
        return (left >= right) ? 1 : -1;
    }
};

struct VariableDimMetadata {
    std::string name;
    std::vector<ArrayDimInfo> dimensions;
    uint32_t unpacked_dims = 0;
    bool is_vector_or_array = false;
    bool is_dynamic = false;
    bool is_queue = false;
};
