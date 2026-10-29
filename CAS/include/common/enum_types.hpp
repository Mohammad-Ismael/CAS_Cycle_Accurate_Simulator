#pragma once
#include "logic_val.hpp"
#include <string>
#include <vector>
#include <unordered_map>

struct EnumMember {
    std::string name;
    LogicVal val;
};

struct EnumType {
    std::string name;
    uint32_t width = 32;
    bool is_signed = false;
    std::vector<EnumMember> members;

    std::string get_name(const LogicVal& val) const {
        for (const auto& m : members) {
            if (m.val.aval == val.aval && m.val.bval == val.bval) {
                return m.name;
            }
        }
        return "";
    }

    LogicVal first() const {
        if (members.empty()) return LogicVal(width, 0, 0);
        return members.front().val;
    }

    LogicVal last() const {
        if (members.empty()) return LogicVal(width, 0, 0);
        return members.back().val;
    }

    LogicVal next(const LogicVal& current_val, int64_t n = 1) const {
        if (members.empty()) return LogicVal(width, 0, 0);
        size_t cur_idx = (size_t)-1;
        for (size_t i = 0; i < members.size(); ++i) {
            if (members[i].val.aval == current_val.aval && members[i].val.bval == current_val.bval) {
                cur_idx = i;
                break;
            }
        }
        if (cur_idx == (size_t)-1) {
            return members.front().val;
        }
        int64_t count = (int64_t)members.size();
        int64_t target_idx = ((int64_t)cur_idx + n) % count;
        if (target_idx < 0) target_idx += count;
        return members[(size_t)target_idx].val;
    }

    LogicVal prev(const LogicVal& current_val, int64_t n = 1) const {
        if (members.empty()) return LogicVal(width, 0, 0);
        size_t cur_idx = (size_t)-1;
        for (size_t i = 0; i < members.size(); ++i) {
            if (members[i].val.aval == current_val.aval && members[i].val.bval == current_val.bval) {
                cur_idx = i;
                break;
            }
        }
        if (cur_idx == (size_t)-1) {
            return members.back().val;
        }
        int64_t count = (int64_t)members.size();
        int64_t target_idx = ((int64_t)cur_idx - n) % count;
        if (target_idx < 0) target_idx += count;
        return members[(size_t)target_idx].val;
    }
};
