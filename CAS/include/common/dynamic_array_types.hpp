#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "logic_val.hpp"

struct DynamicArrayObject {
    std::string name;
    uint32_t elem_width = 32;
    bool is_signed = false;
    std::vector<LogicVal> elements;

    DynamicArrayObject() = default;
    DynamicArrayObject(std::string n, uint32_t w = 32, bool s = false)
        : name(std::move(n)), elem_width(w), is_signed(s) {}

    static LogicVal cast_elem(const LogicVal& v, uint32_t w) {
        if (w >= 64) return v;
        uint64_t mask = (1ULL << w) - 1;
        return LogicVal(w, v.aval & mask, v.bval & mask);
    }

    uint32_t size() const {
        return (uint32_t)elements.size();
    }

    void allocate(size_t sz) {
        elements.assign(sz, LogicVal(elem_width, 0, 0));
    }

    void resize_and_copy(size_t new_sz, const DynamicArrayObject& src) {
        std::vector<LogicVal> new_elems(new_sz, LogicVal(elem_width, 0, 0));
        size_t copy_count = std::min(new_sz, src.elements.size());
        for (size_t i = 0; i < copy_count; ++i) {
            new_elems[i] = src.elements[i];
        }
        elements = std::move(new_elems);
    }

    void delete_all() {
        elements.clear();
    }

    LogicVal get_elem(int64_t idx) const {
        if (idx < 0 || (size_t)idx >= elements.size()) {
            return LogicVal::make_x(elem_width);
        }
        return elements[(size_t)idx];
    }

    void set_elem(int64_t idx, const LogicVal& val) {
        if (idx < 0 || (size_t)idx >= elements.size()) {
            return; // IEEE 1800-2023 §7.5.4: Out-of-bounds write is ignored
        }
        elements[(size_t)idx] = cast_elem(val, elem_width);
    }
};
