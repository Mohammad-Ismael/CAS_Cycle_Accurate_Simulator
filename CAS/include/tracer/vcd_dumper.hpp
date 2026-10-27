#pragma once
#include "../kernel/netlist.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

class VcdDumper {
private:
    bool enabled = false;
    bool allowed = true;
    std::string filename;
    std::ofstream file;
    uint64_t last_dump_time = ~0ULL;

public:
    VcdDumper() = default;
    ~VcdDumper();

    void set_allowed(bool allow) { allowed = allow; }
    bool is_allowed() const { return allowed; }
    bool is_enabled() const { return enabled && allowed; }

    void enable(const std::string& fname);
    void init_header(std::vector<Net>& netlist, uint64_t initial_time);
    void dump_sample(std::vector<Net>& netlist, uint64_t current_time, bool force_dump = false);
    void close();

    static std::string generate_vcd_id(size_t id);
    const std::string& get_filename() const { return filename; }
};