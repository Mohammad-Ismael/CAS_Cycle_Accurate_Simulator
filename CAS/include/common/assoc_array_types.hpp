#pragma once
#include <string>
#include <map>
#include <cstdint>
#include <iostream>
#include "logic_val.hpp"

struct AssociativeArrayObject {
    std::string name;
    uint32_t val_width = 32;
    bool is_signed = false;
    std::string key_type = "logic";
    uint32_t key_width = 32;
    bool is_string_key = false;
    bool is_2state = false;

    std::map<uint64_t, LogicVal> elements;
    std::map<std::string, LogicVal> str_elements;

    AssociativeArrayObject() = default;
    AssociativeArrayObject(std::string n, uint32_t w = 32, bool s = false, std::string kt = "logic", uint32_t kw = 32, bool is_2st = false)
        : name(std::move(n)), val_width(w), is_signed(s), key_type(std::move(kt)), key_width(kw), is_2state(is_2st) {
        if (key_type == "string") {
            is_string_key = true;
        }
    }

    static LogicVal cast_elem(const LogicVal& v, uint32_t w) {
        if (w >= 64) return v;
        uint64_t mask = (1ULL << w) - 1;
        return LogicVal(w, v.aval & mask, v.bval & mask);
    }

    uint64_t mask_key(uint64_t k) const {
        if (key_width < 64 && key_width > 0) {
            return k & ((1ULL << key_width) - 1);
        }
        return k;
    }

    uint32_t size() const {
        return is_string_key ? (uint32_t)str_elements.size() : (uint32_t)elements.size();
    }
    uint32_t num() const {
        return size();
    }

    bool exists(uint64_t key) const {
        return elements.find(mask_key(key)) != elements.end();
    }
    bool exists(const std::string& key) const {
        return str_elements.find(key) != str_elements.end();
    }

    bool first(uint64_t& out_key) const {
        if (elements.empty()) return false;
        out_key = elements.begin()->first;
        return true;
    }
    bool first(std::string& out_key) const {
        if (str_elements.empty()) return false;
        out_key = str_elements.begin()->first;
        return true;
    }

    bool last(uint64_t& out_key) const {
        if (elements.empty()) return false;
        out_key = elements.rbegin()->first;
        return true;
    }
    bool last(std::string& out_key) const {
        if (str_elements.empty()) return false;
        out_key = str_elements.rbegin()->first;
        return true;
    }

    bool next(uint64_t& in_out_key) const {
        if (elements.empty()) return false;
        uint64_t k = mask_key(in_out_key);
        auto it = elements.upper_bound(k);
        if (it == elements.end()) return false;
        in_out_key = it->first;
        return true;
    }
    bool next(std::string& in_out_key) const {
        if (str_elements.empty()) return false;
        auto it = str_elements.upper_bound(in_out_key);
        if (it == str_elements.end()) return false;
        in_out_key = it->first;
        return true;
    }

    bool prev(uint64_t& in_out_key) const {
        if (elements.empty()) return false;
        uint64_t k = mask_key(in_out_key);
        auto it = elements.lower_bound(k);
        if (it == elements.begin()) return false;
        --it;
        in_out_key = it->first;
        return true;
    }
    bool prev(std::string& in_out_key) const {
        if (str_elements.empty()) return false;
        auto it = str_elements.lower_bound(in_out_key);
        if (it == str_elements.begin()) return false;
        --it;
        in_out_key = it->first;
        return true;
    }

    void delete_key(uint64_t key) {
        elements.erase(mask_key(key));
    }
    void delete_key(const std::string& key) {
        str_elements.erase(key);
    }

    void delete_all() {
        elements.clear();
        str_elements.clear();
    }

    LogicVal get(uint64_t key) const {
        auto it = elements.find(mask_key(key));
        if (it != elements.end()) {
            return it->second;
        }
        if (is_2state) {
            return LogicVal(val_width, 0, 0, is_signed);
        }
        return LogicVal::make_x(val_width);
    }
    LogicVal get(const std::string& key) const {
        auto it = str_elements.find(key);
        if (it != str_elements.end()) {
            return it->second;
        }
        if (is_2state) {
            return LogicVal(val_width, 0, 0, is_signed);
        }
        return LogicVal::make_x(val_width);
    }

    void set(uint64_t key, const LogicVal& val) {
        elements[mask_key(key)] = cast_elem(val, val_width);
    }
    void set(const std::string& key, const LogicVal& val) {
        str_elements[key] = cast_elem(val, val_width);
    }
};
