#pragma once
#include "../common/types.hpp"
#include <string>
#include <cstdint>

struct Net {
    std::string name;
    uint32_t width;
    LogicVal value;
    LogicVal prev_value;
    bool modified;
    std::string vcd_id;

    Net(std::string n, uint32_t w)
        : name(std::move(n)), width(w),
          value(LogicVal::make_x(w)),
          prev_value(LogicVal::make_x(w)),
          modified(false), vcd_id("") {}

    void set(const LogicVal& val) {
        LogicVal clamped(width, val.aval, val.bval);
        if (value != clamped) {
            prev_value = value;
            value = clamped;
            modified = true;
        }
    }

    // A posedge occurs ONLY if the net was modified and transitioned to 1 from not-1
    bool is_posedge() const {
        if (!modified) return false;
        bool curr_1 = (value.bval == 0 && (value.aval & 1) == 1);
        bool prev_not_1 = (prev_value.bval != 0) || ((prev_value.aval & 1) == 0);
        return prev_not_1 && curr_1;
    }

    // A negedge occurs ONLY if the net was modified and transitioned to 0 from not-0
    bool is_negedge() const {
        if (!modified) return false;
        bool curr_0 = (value.bval == 0 && (value.aval & 1) == 0);
        bool prev_not_0 = (prev_value.bval != 0) || ((prev_value.aval & 1) == 1);
        return prev_not_0 && curr_0;
    }

    // Synchronize history at the end of a time slot
    void commit_time_slot() {
        prev_value = value;
        modified = false;
    }
};

struct NetSliceRef {
    size_t net_id;
    uint32_t offset;
    uint32_t width;
};

struct SliceBinding {
    NetSliceRef src;
    NetSliceRef dst;
};