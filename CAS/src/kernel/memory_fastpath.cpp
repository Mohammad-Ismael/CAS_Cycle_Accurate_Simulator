#include "../../include/kernel/memory_fastpath.hpp"
#include "../../include/kernel/scheduler.hpp"
#include "../../include/engine.hpp"
#include <iostream>

namespace cas {

uint32_t MemoryFastPath::find_canon_net(const Engine& engine, const std::string& suffix) const {
    for (size_t i = 0; i < engine.netlist.size(); ++i) {
        const auto& name = engine.netlist[i].name;
        if (name == suffix ||
            (name.size() > suffix.size() &&
             name[name.size() - suffix.size() - 1] == '.' &&
             name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            return (i < engine.canonical_net_map.size()) ? (uint32_t)engine.canonical_net_map[i] : (uint32_t)i;
        }
    }
    return UINT32_MAX;
}

bool MemoryFastPath::init(Engine& engine) {
    bus.instr_req_id = find_canon_net(engine, "instr_req_o");
    if (bus.instr_req_id == UINT32_MAX) bus.instr_req_id = find_canon_net(engine, "instr_req");

    bus.instr_gnt_id = find_canon_net(engine, "instr_gnt_i");
    if (bus.instr_gnt_id == UINT32_MAX) bus.instr_gnt_id = find_canon_net(engine, "instr_gnt");

    bus.instr_rvalid_id = find_canon_net(engine, "instr_rvalid_i");
    if (bus.instr_rvalid_id == UINT32_MAX) bus.instr_rvalid_id = find_canon_net(engine, "instr_rvalid");

    bus.instr_addr_id = find_canon_net(engine, "instr_addr_o");
    if (bus.instr_addr_id == UINT32_MAX) bus.instr_addr_id = find_canon_net(engine, "instr_addr");

    bus.instr_rdata_id = find_canon_net(engine, "instr_rdata_i");
    if (bus.instr_rdata_id == UINT32_MAX) bus.instr_rdata_id = find_canon_net(engine, "instr_rdata");

    bus.instr_err_id = find_canon_net(engine, "instr_err_i");
    if (bus.instr_err_id == UINT32_MAX) bus.instr_err_id = find_canon_net(engine, "instr_err");

    bus.data_req_id = find_canon_net(engine, "data_req_o");
    bus.data_gnt_id = find_canon_net(engine, "data_gnt_i");
    bus.data_rvalid_id = find_canon_net(engine, "data_rvalid_i");
    bus.data_we_id = find_canon_net(engine, "data_we_o");
    bus.data_be_id = find_canon_net(engine, "data_be_o");
    bus.data_addr_id = find_canon_net(engine, "data_addr_o");
    bus.data_wdata_id = find_canon_net(engine, "data_wdata_o");
    bus.data_rdata_id = find_canon_net(engine, "data_rdata_i");
    bus.data_err_id = find_canon_net(engine, "data_err_i");

    bus.ram_a_rvalid_id = find_canon_net(engine, "u_ram.a_rvalid_o");
    bus.ram_a_rdata_id = find_canon_net(engine, "u_ram.a_rdata_o");

    if (bus.instr_req_id == UINT32_MAX || bus.instr_gnt_id == UINT32_MAX ||
        bus.data_req_id == UINT32_MAX || bus.data_gnt_id == UINT32_MAX) {
        bus.is_valid = false;
        return false;
    }

    ram_array = nullptr;
    for (auto& kv : engine.memory_map) {
        if (kv.first.find("u_ram") != std::string::npos && kv.first.find("mem") != std::string::npos) {
            ram_array = &kv.second;
            break;
        }
    }
    if (!ram_array) {
        for (auto& kv : engine.memory_map) {
            if (kv.second.depth >= 65536) {
                ram_array = &kv.second;
                break;
            }
        }
    }

    bus.is_valid = true;
    if (ram_array) {
        ram_size = ram_array->depth * 4;
        ram_mask = ram_size - 1;
        std::cout << "[MemoryFastPath] Bound direct-mapped bus to host SRAM at "
                  << ram_array->name << " (depth=" << ram_array->depth << ", size=" << (ram_size / 1024) << "KB)\n";
    } else {
        std::cout << "[MemoryFastPath] Bound direct-mapped bus handshake lines (instr_gnt, data_gnt)\n";
    }

    return true;
}

bool MemoryFastPath::service_memory_cycle(
    Engine& /*engine*/,
    uint64_t* __restrict /*cur_nets*/,
    uint64_t* __restrict /*next_nets*/,
    uint8_t* __restrict /*modified_mask*/,
    std::vector<uint32_t>& /*dirty_native_nets*/
) {
    if (!bus.is_valid) return false;
    return true;
}

bool MemoryFastPath::post_comb_sync(Engine& engine, uint64_t* __restrict cur_nets, StratifiedScheduler* sched) {
    if (!bus.is_valid) return false;
    bool any_changed = false;

    // 1. Synchronize instr_gnt_i with instr_req_o
    if (bus.instr_gnt_id != UINT32_MAX && bus.instr_req_id != UINT32_MAX) {
        uint64_t new_val = cur_nets[bus.instr_req_id] & 1;
        uint64_t old_val = cur_nets[bus.instr_gnt_id];
        uint64_t netlist_val = engine.netlist[bus.instr_gnt_id].value.aval;
        if (new_val != old_val || new_val != netlist_val || engine.netlist[bus.instr_gnt_id].value.bval != 0) {
            cur_nets[bus.instr_gnt_id] = new_val;
            LogicVal lval = LogicVal::from_u64_2state(new_val, engine.netlist[bus.instr_gnt_id].width);
            engine.netlist[bus.instr_gnt_id].set(lval);
            if (bus.instr_gnt_id < engine.alias_members.size()) {
                for (size_t other_id : engine.alias_members[bus.instr_gnt_id]) {
                    if (other_id != bus.instr_gnt_id && other_id < engine.netlist.size()) {
                        engine.netlist[other_id].set(lval);
                        cur_nets[other_id] = new_val;
                    }
                }
            }
            if (sched) sched->notify_net_changed(bus.instr_gnt_id);
            any_changed = true;
        }
    }

    // 2. Synchronize data_gnt_i with data_req_o for all bus devices (RAM, SimCtrl UART, Timer)
    if (bus.data_gnt_id != UINT32_MAX && bus.data_req_id != UINT32_MAX) {
        uint64_t new_val = cur_nets[bus.data_req_id] & 1;
        uint64_t old_val = cur_nets[bus.data_gnt_id];
        uint64_t netlist_val = engine.netlist[bus.data_gnt_id].value.aval;
        if (new_val != old_val || new_val != netlist_val || engine.netlist[bus.data_gnt_id].value.bval != 0) {
            cur_nets[bus.data_gnt_id] = new_val;
            LogicVal lval = LogicVal::from_u64_2state(new_val, engine.netlist[bus.data_gnt_id].width);
            engine.netlist[bus.data_gnt_id].set(lval);
            if (bus.data_gnt_id < engine.alias_members.size()) {
                for (size_t other_id : engine.alias_members[bus.data_gnt_id]) {
                    if (other_id != bus.data_gnt_id && other_id < engine.netlist.size()) {
                        engine.netlist[other_id].set(lval);
                        cur_nets[other_id] = new_val;
                    }
                }
            }
            if (sched) sched->notify_net_changed(bus.data_gnt_id);
            any_changed = true;
        }
    }

    return any_changed;
}

} // namespace cas
