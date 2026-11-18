#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <memory>
#include "logic_val.hpp"

struct DynamicArrayObject {
    std::string name;
    uint32_t elem_width = 32;
    bool is_signed = false;
    bool is_four_state = false;
    bool is_real = false;
    std::vector<LogicVal> elements;
    std::shared_ptr<std::vector<LogicVal>> storage = nullptr;

    DynamicArrayObject() = default;
    DynamicArrayObject(std::string n, uint32_t w = 32, bool s = false)
        : name(std::move(n)), elem_width(w), is_signed(s) {}

    static LogicVal cast_elem(const LogicVal& v, uint32_t w) {
        if (w >= 64) return v;
        uint64_t mask = (1ULL << w) - 1;
        return LogicVal(w, v.aval & mask, v.bval & mask);
    }

    uint32_t size() const {
        if (storage && storage->size() == elements.size()) return (uint32_t)storage->size();
        return (uint32_t)elements.size();
    }

    void allocate(size_t sz) {
        storage = nullptr;
        LogicVal def_v = is_real ? LogicVal::make_real(0.0) : (is_four_state ? LogicVal::make_x(elem_width) : LogicVal(elem_width, 0, 0));
        elements.assign(sz, def_v);
    }

    void resize_and_copy(size_t new_sz, const DynamicArrayObject& src) {
        storage = nullptr;
        const std::vector<LogicVal>& src_v = (src.storage && src.storage->size() == src.elements.size()) ? *src.storage : src.elements;
        LogicVal def_v = is_real ? LogicVal::make_real(0.0) : (is_four_state ? LogicVal::make_x(elem_width) : LogicVal(elem_width, 0, 0));
        std::vector<LogicVal> new_elems(new_sz, def_v);
        size_t copy_count = std::min(new_sz, src_v.size());
        for (size_t i = 0; i < copy_count; ++i) {
            new_elems[i] = src_v[i];
        }
        elements = std::move(new_elems);
    }

    void delete_all() {
        storage = nullptr;
        elements.clear();
    }

    LogicVal get_elem(int64_t idx) const {
        LogicVal ret;
        if (storage && storage->size() == elements.size()) {
            if (idx < 0 || (size_t)idx >= storage->size()) {
                return is_real ? LogicVal::make_real(0.0) : LogicVal::make_x(elem_width);
            }
            ret = (*storage)[(size_t)idx];
        } else {
            if (idx < 0 || (size_t)idx >= elements.size()) {
                return is_real ? LogicVal::make_real(0.0) : LogicVal::make_x(elem_width);
            }
            ret = elements[(size_t)idx];
        }
        if (!is_real && !ret.is_string) {
            ret.is_signed = is_signed;
        }
        return ret;
    }

    void set_elem(int64_t idx, const LogicVal& val) {
        if (storage && storage->size() == elements.size()) {
            if (idx < 0 || (size_t)idx >= storage->size()) return;
            LogicVal elem_to_write = (val.is_string || is_real || val.is_real) ? val : cast_elem(val, elem_width);
            (*storage)[(size_t)idx] = elem_to_write;
            elements[(size_t)idx] = elem_to_write;
            return;
        }
        if (idx < 0 || (size_t)idx >= elements.size()) {
            return; // IEEE 1800-2023 §7.5.4: Out-of-bounds write is ignored
        }
        if (val.is_string || is_real || val.is_real) {
            elements[(size_t)idx] = val;
        } else {
            elements[(size_t)idx] = cast_elem(val, elem_width);
        }
    }

    void share_from(DynamicArrayObject& src) {
        elem_width = src.elem_width;
        is_signed = src.is_signed;
        is_four_state = src.is_four_state;
        is_real = src.is_real;
        storage = nullptr;
        const std::vector<LogicVal>& src_v = (src.storage && src.storage->size() == src.elements.size()) ? *src.storage : src.elements;
        elements = src_v;
    }
};
