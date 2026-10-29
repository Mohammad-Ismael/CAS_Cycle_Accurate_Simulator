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
    std::deque<LogicVal> elements;

    QueueObject() = default;
    QueueObject(std::string n, uint32_t w = 32, bool s = false, int64_t bound = -1)
        : name(std::move(n)), elem_width(w), is_signed(s), max_bound(bound) {}

    static LogicVal cast_elem(const LogicVal& v, uint32_t w) {
        if (w >= 64) return v;
        uint64_t mask = (1ULL << w) - 1;
        return LogicVal(w, v.aval & mask, v.bval & mask);
    }

    uint32_t size() const {
        return (uint32_t)elements.size();
    }

    void push_back(const LogicVal& val) {
        LogicVal casted = cast_elem(val, elem_width);
        if (max_bound >= 0 && (int64_t)elements.size() > max_bound) {
            return;
        }
        elements.push_back(casted);
        if (max_bound >= 0 && (int64_t)elements.size() > max_bound + 1) {
            elements.resize(max_bound + 1);
        }
    }

    void push_front(const LogicVal& val) {
        LogicVal casted = cast_elem(val, elem_width);
        elements.push_front(casted);
        if (max_bound >= 0 && (int64_t)elements.size() > max_bound + 1) {
            elements.resize(max_bound + 1);
        }
    }

    LogicVal pop_front() {
        if (elements.empty()) {
            std::cerr << "[CAS WARNING] pop_front() called on empty queue '" << name << "'\n";
            return LogicVal::make_x(elem_width);
        }
        LogicVal val = elements.front();
        elements.pop_front();
        return val;
    }

    LogicVal pop_back() {
        if (elements.empty()) {
            std::cerr << "[CAS WARNING] pop_back() called on empty queue '" << name << "'\n";
            return LogicVal::make_x(elem_width);
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

    LogicVal get_elem(int64_t idx) const {
        if (idx < 0 || (size_t)idx >= elements.size()) {
            return LogicVal::make_x(elem_width);
        }
        return elements[(size_t)idx];
    }

    void set_elem(int64_t idx, const LogicVal& val) {
        if (idx < 0 || (size_t)idx >= elements.size()) {
            return; // IEEE 1800-2023 §7.10.3: Out-of-bounds write is ignored
        }
        elements[(size_t)idx] = cast_elem(val, elem_width);
    }
};
