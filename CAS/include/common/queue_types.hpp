#pragma once
#include <string>
#include <deque>
#include <cstdint>
#include <iostream>
#include "types.hpp"

struct QueueObject {
    std::string name;
    uint32_t elem_width = 32;
    bool is_signed = false;
    int64_t max_bound = -1; // -1 for unbounded [$], >= 0 for bounded [$:bound_val]
    bool is_real = false;
    bool is_string = false;
    bool is_2state = false;
    std::deque<LogicVal> elements;

    QueueObject() = default;
    QueueObject(std::string n, uint32_t w = 32, bool s = false, int64_t bound = -1, bool is_r = false, bool is_str = false, bool is_2st = false)
        : name(std::move(n)), elem_width(w), is_signed(s), max_bound(bound), is_real(is_r), is_string(is_str), is_2state(is_2st) {}

    LogicVal get_default_val() const {
        if (is_real) return LogicVal::make_real(0.0);
        if (is_string) return LogicVal::make_string("");
        if (is_2state) return LogicVal(elem_width, 0, 0, is_signed);
        return LogicVal::make_x(elem_width);
    }

    LogicVal cast_elem(const LogicVal& v) const {
        if (is_string || v.is_string) {
            return v.is_string ? v : LogicVal::make_string(v.to_ascii_string());
        }
        if (is_real || v.is_real) {
            if (v.is_real) return v;
            return LogicVal::make_real(v.is_signed ? (double)v.to_signed() : (double)v.aval);
        }
        if (elem_width >= 64) {
            LogicVal res = v;
            res.width = elem_width;
            if (is_2state) res.coerce_to_2state();
            return res;
        }
        uint64_t mask = (1ULL << elem_width) - 1;
        LogicVal res(elem_width, v.aval & mask, v.bval & mask, is_signed);
        if (is_2state) res.coerce_to_2state();
        return res;
    }

    static LogicVal cast_elem(const LogicVal& v, uint32_t w) {
        if (v.is_string || v.is_real) return v;
        if (w >= 64) return v;
        uint64_t mask = (1ULL << w) - 1;
        return LogicVal(w, v.aval & mask, v.bval & mask);
    }

    uint32_t size() const {
        return (uint32_t)elements.size();
    }

    void push_back(const LogicVal& val) {
        LogicVal casted = cast_elem(val);
        if (max_bound >= 0 && (int64_t)elements.size() > max_bound) {
            return;
        }
        elements.push_back(casted);
        if (max_bound >= 0 && (int64_t)elements.size() > max_bound + 1) {
            elements.resize(max_bound + 1);
        }
    }

    void push_front(const LogicVal& val) {
        LogicVal casted = cast_elem(val);
        elements.push_front(casted);
        if (max_bound >= 0 && (int64_t)elements.size() > max_bound + 1) {
            elements.pop_back();
        }
    }

    LogicVal pop_front() {
        if (elements.empty()) {
            return get_default_val();
        }
        LogicVal val = elements.front();
        elements.pop_front();
        return val;
    }

    LogicVal pop_back() {
        if (elements.empty()) {
            return get_default_val();
        }
        LogicVal val = elements.back();
        elements.pop_back();
        return val;
    }

    void delete_all() {
        elements.clear();
    }

    void delete_at(int64_t idx) {
        if (idx >= 0 && (size_t)idx < elements.size()) {
            elements.erase(elements.begin() + idx);
        }
    }

    void insert(int64_t idx, const LogicVal& val) {
        if (idx < 0 || (size_t)idx > elements.size()) {
            return; // IEEE 1800-2023 §7.10.2.2: Illegal index warning/ignored
        }
        LogicVal casted = cast_elem(val);
        if (max_bound >= 0 && (int64_t)elements.size() > max_bound) {
            if ((size_t)idx == elements.size()) return;
            elements.insert(elements.begin() + idx, casted);
            elements.pop_back();
            return;
        }
        elements.insert(elements.begin() + idx, casted);
        if (max_bound >= 0 && (int64_t)elements.size() > max_bound + 1) {
            elements.pop_back();
        }
    }

    LogicVal get_elem(int64_t idx) const {
        if (idx < 0 || (size_t)idx >= elements.size()) {
            return get_default_val();
        }
        return elements[(size_t)idx];
    }

    void set_elem(int64_t idx, const LogicVal& val) {
        if (idx < 0) return;
        if ((size_t)idx == elements.size()) {
            if (max_bound >= 0 && (int64_t)elements.size() > max_bound) return;
            elements.push_back(cast_elem(val));
            return;
        }
        if ((size_t)idx > elements.size()) {
            return; // IEEE 1800-2023 §7.10.3: Out-of-bounds write is ignored
        }
        elements[(size_t)idx] = cast_elem(val);
    }
};
