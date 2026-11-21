#pragma once
#include "../common/types.hpp"
#include <string>
#include <cstdint>
#include <vector>
#include <cmath>

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
    inline static uint64_t global_net_change_count = 0;
    bool changed_in_current_sim_time = true;
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
    bool is_virtual_interface = false;
    std::string interface_name = "";
    std::string modport_name = "";
    bool is_vectored = false;
    bool is_port = false;
    bool is_2state = false;

    Net(std::string n, uint32_t w, bool s = false, NetKind k = NetKind::WIRE)
        : name(std::move(n)), width(w), is_signed(s), kind(k),
          value((k == NetKind::VAR) ? LogicVal::make_x(w) :
                (k == NetKind::TRI1) ? LogicVal(w, (w >= 64 ? ~0ULL : ((1ULL << w) - 1)), 0) :
                (k == NetKind::TRI0) ? LogicVal(w, 0, 0) :
                LogicVal::make_z(w)),
          prev_value(value),
          modified(false), vcd_id("") {}

    void set(const LogicVal& val) {
        LogicVal clamped;
        if (val.is_string || type_name == "string") {
            clamped = val;
            clamped.is_string = true;
        } else if (type_name == "real" || type_name == "shortreal") {
            clamped.width = width ? width : 64;
            clamped.is_real = true;
            clamped.real_val = val.to_real();
        } else if (val.is_real) {
            clamped.is_real = false;
            clamped.is_signed = is_signed;
            clamped.width = width;
            int64_t ival = 0;
            if (!std::isnan(val.real_val) && !std::isinf(val.real_val)) {
                ival = std::llround(val.real_val);
            }
            if (width <= 64) {
                clamped.aval = static_cast<uint64_t>(ival);
                clamped.bval = 0;
            } else {
                clamped.wide_aval.assign((width + 63) / 64, 0);
                clamped.wide_bval.assign((width + 63) / 64, 0);
                clamped.wide_aval[0] = static_cast<uint64_t>(ival);
                if (ival < 0 && is_signed) {
                    for (size_t wi = 1; wi < clamped.wide_aval.size(); ++wi) {
                        clamped.wide_aval[wi] = ~0ULL;
                    }
                }
            }
            clamped.mask();
        } else {
            clamped = val;
            clamped.width = width;
            clamped.is_signed = is_signed;
            clamped.mask();
            if ((is_2state || type_name == "bit" || type_name == "byte" || type_name == "shortint" || type_name == "longint" || type_name == "int") && !val.is_queue_empty_pop) {
                clamped.coerce_to_2state();
            }
        }
        if (value.is_real != clamped.is_real || value.is_string != clamped.is_string || value != clamped) {
            prev_value = value;
            value = clamped;
            modified = true;
            change_counter++;
            global_net_change_count++;
            changed_in_current_sim_time = true;
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

struct MemoryArray;

struct MemoryBinding {
    std::string src;
    std::string dst;
    mutable MemoryArray* cached_src = nullptr;
    mutable MemoryArray* cached_dst = nullptr;
    mutable bool resolved = false;
    mutable uint64_t last_synced_version = 0;
};

struct MemoryContinuousAssign {
    std::string scope;
    std::string mem_name;
    std::unique_ptr<ExpressionNode> index_expr;
    std::unique_ptr<ExpressionNode> rhs;
    std::string member_name;
    uint32_t bit_offset = 0;
    uint32_t bit_width = 0;
    mutable MemoryArray* cached_mem = nullptr;
    mutable bool mem_resolved = false;
};

struct NamedEvent {
    std::string name;
    uint64_t last_triggered_time = (uint64_t)-1;
    std::vector<size_t> waiting_pids;
};

struct ModulePathDelay {
    size_t src_net_id = (size_t)-1;
    size_t dst_net_id = (size_t)-1;
    ExpressionNode* cond_expr = nullptr;
    std::string scope = "";
    uint64_t delay_ticks = 0;
};

struct RuntimeTimingCheck {
    TimingCheckType type = TimingCheckType::SETUP;
    std::string scope = "";
    std::string file = "";
    int line = 0;

    size_t ref_net_id = (size_t)-1;
    std::string ref_name = "";
    TimingCheckEdge ref_edge = TimingCheckEdge::ANY;

    size_t data_net_id = (size_t)-1;
    std::string data_name = "";
    TimingCheckEdge data_edge = TimingCheckEdge::ANY;

    uint64_t limit_ticks = 0;
    uint64_t threshold_ticks = 0;

    size_t notifier_net_id = (size_t)-1;
    std::string notifier_name = "";

    uint64_t t_last_ref = (uint64_t)-1;
    uint64_t t_last_data = (uint64_t)-1;
    uint64_t last_ref_change_counter = (uint64_t)-1;
    uint64_t last_data_change_counter = (uint64_t)-1;
};

struct FanoutGraph {
    // net_id -> vector of indices into engine.resolved_continuous_assigns
    std::vector<std::vector<uint32_t>> net_to_rca;

    // net_id -> vector of process IDs (for combinational processes sensitive to this net)
    std::vector<std::vector<uint32_t>> net_to_comb_proc;

    // list of RCA indices that read from any memory array
    std::vector<uint32_t> mem_reading_rcas;

    // list of combinational process IDs that read from any memory array
    std::vector<uint32_t> mem_reading_comb_procs;

    // list of MCA indices that read from any memory array
    std::vector<uint32_t> mem_reading_mcas;

    // net_id -> vector of MCA indices
    std::vector<std::vector<uint32_t>> net_to_mca;

    void resize(size_t net_count) {
        net_to_rca.resize(net_count);
        net_to_comb_proc.resize(net_count);
        net_to_mca.resize(net_count);
    }
};
