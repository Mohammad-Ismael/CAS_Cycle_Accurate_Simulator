#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

class Engine;
struct MemoryArray;

namespace cas {

struct MemoryBusBinding {
    // Instruction interface
    uint32_t instr_req_id = UINT32_MAX;
    uint32_t instr_gnt_id = UINT32_MAX;
    uint32_t instr_rvalid_id = UINT32_MAX;
    uint32_t instr_addr_id = UINT32_MAX;
    uint32_t instr_rdata_id = UINT32_MAX;
    uint32_t instr_err_id = UINT32_MAX;

    // Data interface
    uint32_t data_req_id = UINT32_MAX;
    uint32_t data_gnt_id = UINT32_MAX;
    uint32_t data_rvalid_id = UINT32_MAX;
    uint32_t data_we_id = UINT32_MAX;
    uint32_t data_be_id = UINT32_MAX;
    uint32_t data_addr_id = UINT32_MAX;
    uint32_t data_wdata_id = UINT32_MAX;
    uint32_t data_rdata_id = UINT32_MAX;
    uint32_t data_err_id = UINT32_MAX;

    // RAM device ports (for bus consistency)
    uint32_t ram_a_rvalid_id = UINT32_MAX;
    uint32_t ram_a_rdata_id = UINT32_MAX;

    bool is_valid = false;
};

class MemoryFastPath {
public:
    MemoryBusBinding bus;
    MemoryArray* ram_array = nullptr;
    uint32_t ram_base = 0x00100000;
    uint32_t ram_size = 1024 * 1024; // 1 MB (262,144 words)
    uint32_t ram_mask = 0x000FFFFF;

    // 1-cycle latency pipeline state tracking
    bool pending_instr_req = false;
    uint32_t pending_instr_addr = 0;

    bool pending_data_req = false;
    bool pending_data_is_ram = false;
    bool pending_data_we = false;
    uint32_t pending_data_addr = 0;
    uint32_t pending_data_be = 0;
    uint32_t pending_data_wdata = 0;

    MemoryFastPath() = default;

    bool init(Engine& engine);

    bool service_memory_cycle(
        Engine& engine,
        uint64_t* __restrict cur_nets,
        uint64_t* __restrict next_nets,
        uint8_t* __restrict modified_mask,
        std::vector<uint32_t>& dirty_native_nets
    );

    void post_comb_sync(
        Engine& engine,
        uint64_t* __restrict cur_nets
    );

private:
    uint32_t find_canon_net(const Engine& engine, const std::string& suffix) const;
};

} // namespace cas
