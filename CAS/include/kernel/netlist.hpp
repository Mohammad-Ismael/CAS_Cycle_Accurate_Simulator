#pragma once
#include "../common/types.hpp"
#include <string>
#include <cstdint>
#include <vector>

struct ExpressionNode;

struct Net {
    std::string name;
    uint32_t width;
    bool is_signed;
    NetKind kind = NetKind::WIRE;
    LogicVal value;
    LogicVal prev_value;
    bool modified;
    uint64_t change_counter = 0;
    std::string vcd_id;
    bool has_procedural_assign = false;
    ExpressionNode* procedural_assign_rhs = nullptr;
    std::string procedural_assign_scope = "";
    std::string type_name = "";
    bool is_class_handle = false;
    bool is_covergroup_handle = false;
    std::string covergroup_type_name = "";
    bool is_chandle = false;
    bool is_semaphore = false;
    bool is_mailbox = false;

    Net(std::string n, uint32_t w, bool s = false, NetKind k = NetKind::WIRE)
        : name(std::move(n)), width(w), is_signed(s), kind(k),
          value((k == NetKind::VAR) ? LogicVal::make_x(w) : LogicVal::make_z(w)),
          prev_value((k == NetKind::VAR) ? LogicVal::make_x(w) : LogicVal::make_z(w)),
          modified(false), vcd_id("") {}

    void set(const LogicVal& val) {
        LogicVal clamped(width, val.aval, val.bval);
        if (val.is_real) {
            clamped.is_real = true;
            clamped.real_val = val.real_val;
        }
        if (value != clamped) {
            prev_value = value;
            value = clamped;
            modified = true;
            change_counter++;
        }
    }

    bool is_posedge() const {
        if (!modified) return false;
        bool curr_1 = (value.bval == 0 && (value.aval & 1) == 1);
        bool prev_not_1 = (prev_value.bval != 0) || ((prev_value.aval & 1) == 0);
        return prev_not_1 && curr_1;
    }

    bool is_negedge() const {
        if (!modified) return false;
        bool curr_0 = (value.bval == 0 && (value.aval & 1) == 0);
        bool prev_not_0 = (prev_value.bval != 0) || ((prev_value.aval & 1) == 1);
        return prev_not_0 && curr_0;
    }

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

struct NamedEvent {
    std::string name;
    uint64_t last_triggered_time = (uint64_t)-1;
    std::vector<size_t> waiting_pids;
};