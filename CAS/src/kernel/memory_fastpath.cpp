#include "../../include/kernel/memory_fastpath.hpp"
#include "../../include/engine.hpp"
#include <iostream>

namespace cas {

uint32_t MemoryFastPath::find_canon_net(const Engine& engine, const std::string& suffix) const {
    for (size_t i = 0; i < engine.netlist.size(); ++i) {
        const std::string& n = engine.netlist[i].name;
        if (n.find("u_top") != std::string::npos &&
            (n.size() >= suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            return (i < engine.canonical_net_map.size()) ? (uint32_t)engine.canonical_net_map[i] : (uint32_t)i;
        }
    }
    for (size_t i = 0; i < engine.netlist.size(); ++i) {
        const std::string& n = engine.netlist[i].name;
        if (n == suffix || (n.size() > suffix.size() && n[n.size() - suffix.size() - 1] == '.' &&
            n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)) {
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

    if (bus.instr_req_id == UINT32_MAX || bus.instr_addr_id == UINT32_MAX ||
        bus.instr_rdata_id == UINT32_MAX || bus.instr_rvalid_id == UINT32_MAX ||
        bus.data_req_id == UINT32_MAX || bus.data_addr_id == UINT32_MAX ||
        bus.data_rdata_id == UINT32_MAX || bus.data_rvalid_id == UINT32_MAX) {
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

    if (!ram_array) {
        bus.is_valid = false;
        return false;
    }

    bus.is_valid = true;
    ram_size = ram_array->depth * 4;
    ram_mask = ram_size - 1;

    pending_instr_req = false;
    pending_instr_addr = 0;
    pending_data_req = false;
    pending_data_is_ram = false;
    pending_data_we = false;
    pending_data_addr = 0;
    pending_data_be = 0;
    pending_data_wdata = 0;

    std::cout << "[MemoryFastPath] Bound direct-mapped bus to host SRAM at "
              << ram_array->name << " (depth=" << ram_array->depth << ", size=" << (ram_size / 1024) << "KB)\n";
    return true;
}

bool MemoryFastPath::service_memory_cycle(
    Engine& engine,
    uint64_t* __restrict cur_nets,
    uint64_t* __restrict next_nets,
    uint8_t* __restrict modified_mask,
    std::vector<uint32_t>& dirty_native_nets
) {
    if (!bus.is_valid || !ram_array) return false;

    // 1. Service pending instruction fetch from previous cycle
    if (pending_instr_req) {
        uint32_t addr = pending_instr_addr;
        uint32_t word_offset = 0;
        if (addr >= ram_base && addr < ram_base + ram_size) {
            word_offset = (addr - ram_base) >> 2;
        } else {
            word_offset = (addr >> 2) & (ram_array->depth - 1);
        }
        if (word_offset < ram_array->words.size()) {
            uint32_t instr_val = (uint32_t)ram_array->words[word_offset].aval;
            next_nets[bus.instr_rdata_id] = instr_val;
            cur_nets[bus.instr_rdata_id] = instr_val;
            modified_mask[bus.instr_rdata_id] = 1;
            dirty_native_nets.push_back(bus.instr_rdata_id);

            next_nets[bus.instr_rvalid_id] = 1;
            cur_nets[bus.instr_rvalid_id] = 1;
            modified_mask[bus.instr_rvalid_id] = 1;
            dirty_native_nets.push_back(bus.instr_rvalid_id);

            if (bus.instr_err_id != UINT32_MAX) {
                next_nets[bus.instr_err_id] = 0;
                cur_nets[bus.instr_err_id] = 0;
            }
        }
    } else {
        next_nets[bus.instr_rvalid_id] = 0;
        cur_nets[bus.instr_rvalid_id] = 0;
        modified_mask[bus.instr_rvalid_id] = 1;
        dirty_native_nets.push_back(bus.instr_rvalid_id);
    }

    // 2. Sample current cycle instruction request
    uint64_t req_val = cur_nets[bus.instr_req_id];
    pending_instr_req = (req_val & 1) != 0;
    pending_instr_addr = (uint32_t)cur_nets[bus.instr_addr_id];

    // Combinational instruction grant
    if (bus.instr_gnt_id != UINT32_MAX) {
        next_nets[bus.instr_gnt_id] = req_val & 1;
        cur_nets[bus.instr_gnt_id] = req_val & 1;
        modified_mask[bus.instr_gnt_id] = 1;
        dirty_native_nets.push_back(bus.instr_gnt_id);
    }

    // 3. Service pending data memory transaction from previous cycle
    if (pending_data_req && pending_data_is_ram) {
        uint32_t addr = pending_data_addr;
        uint32_t word_offset = ((addr - ram_base) >> 2) & (ram_array->depth - 1);
        if (word_offset < ram_array->words.size()) {
            if (pending_data_we) {
                // Write transaction
                uint32_t old_val = (uint32_t)ram_array->words[word_offset].aval;
                uint32_t mask = 0;
                if (pending_data_be & 1) mask |= 0x000000FF;
                if (pending_data_be & 2) mask |= 0x0000FF00;
                if (pending_data_be & 4) mask |= 0x00FF0000;
                if (pending_data_be & 8) mask |= 0xFF000000;
                uint32_t new_val = (old_val & ~mask) | (pending_data_wdata & mask);
                ram_array->words[word_offset].aval = new_val;
                ram_array->words[word_offset].bval = 0;
            } else {
                // Read transaction
                uint32_t rdata = (uint32_t)ram_array->words[word_offset].aval;
                next_nets[bus.data_rdata_id] = rdata;
                cur_nets[bus.data_rdata_id] = rdata;
                modified_mask[bus.data_rdata_id] = 1;
                dirty_native_nets.push_back(bus.data_rdata_id);

                if (bus.ram_a_rdata_id != UINT32_MAX) {
                    next_nets[bus.ram_a_rdata_id] = rdata;
                    cur_nets[bus.ram_a_rdata_id] = rdata;
                    modified_mask[bus.ram_a_rdata_id] = 1;
                    dirty_native_nets.push_back(bus.ram_a_rdata_id);
                }
            }

            next_nets[bus.data_rvalid_id] = 1;
            cur_nets[bus.data_rvalid_id] = 1;
            modified_mask[bus.data_rvalid_id] = 1;
            dirty_native_nets.push_back(bus.data_rvalid_id);

            if (bus.ram_a_rvalid_id != UINT32_MAX) {
                next_nets[bus.ram_a_rvalid_id] = 1;
                cur_nets[bus.ram_a_rvalid_id] = 1;
                modified_mask[bus.ram_a_rvalid_id] = 1;
                dirty_native_nets.push_back(bus.ram_a_rvalid_id);
            }

            if (bus.data_err_id != UINT32_MAX) {
                next_nets[bus.data_err_id] = 0;
                cur_nets[bus.data_err_id] = 0;
            }
        }
    } else if (!pending_data_req || pending_data_is_ram) {
        next_nets[bus.data_rvalid_id] = 0;
        cur_nets[bus.data_rvalid_id] = 0;
        modified_mask[bus.data_rvalid_id] = 1;
        dirty_native_nets.push_back(bus.data_rvalid_id);

        if (bus.ram_a_rvalid_id != UINT32_MAX) {
            next_nets[bus.ram_a_rvalid_id] = 0;
            cur_nets[bus.ram_a_rvalid_id] = 0;
            modified_mask[bus.ram_a_rvalid_id] = 1;
            dirty_native_nets.push_back(bus.ram_a_rvalid_id);
        }
    }

    // 4. Sample current cycle data request
    uint64_t dreq_val = cur_nets[bus.data_req_id];
    uint32_t daddr = (uint32_t)cur_nets[bus.data_addr_id];
    bool is_dreq = (dreq_val & 1) != 0;
    bool is_ram = is_dreq && (daddr >= ram_base && daddr < ram_base + ram_size);

    pending_data_req = is_dreq;
    pending_data_is_ram = is_ram;
    pending_data_we = (cur_nets[bus.data_we_id] & 1) != 0;
    pending_data_addr = daddr;
    pending_data_be = (uint32_t)(cur_nets[bus.data_be_id] & 0xF);
    pending_data_wdata = (uint32_t)cur_nets[bus.data_wdata_id];

    // Grant for RAM transactions
    if (is_ram && bus.data_gnt_id != UINT32_MAX) {
        next_nets[bus.data_gnt_id] = 1;
        cur_nets[bus.data_gnt_id] = 1;
        modified_mask[bus.data_gnt_id] = 1;
        dirty_native_nets.push_back(bus.data_gnt_id);
    }

    return true;
}

void MemoryFastPath::post_comb_sync(Engine& engine, uint64_t* __restrict cur_nets) {
    if (!bus.is_valid) return;
    if (bus.instr_gnt_id != UINT32_MAX) {
        cur_nets[bus.instr_gnt_id] = cur_nets[bus.instr_req_id] & 1;
    }
    if (bus.data_gnt_id != UINT32_MAX) {
        uint64_t dreq = cur_nets[bus.data_req_id] & 1;
        uint32_t daddr = (uint32_t)cur_nets[bus.data_addr_id];
        if (dreq && daddr >= ram_base && daddr < ram_base + ram_size) {
            cur_nets[bus.data_gnt_id] = 1;
        }
    }
}

} // namespace cas
