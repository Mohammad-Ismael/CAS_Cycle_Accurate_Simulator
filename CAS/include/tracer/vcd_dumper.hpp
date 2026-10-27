#pragma once
#include "../kernel/netlist.hpp"
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class VcdDumper {
private:
    std::ofstream vcd_file;
    bool enabled = false;
    bool allowed = true;
    std::string filename;
    uint32_t max_levels = 0; // 0 = all levels

    std::vector<LogicVal> last_dumped_values;
    std::unordered_map<size_t, bool> is_net_dumpable;

    static size_t count_dots(const std::string& name);
    void dump_value_change(const LogicVal& val, const std::string& vcd_id);

public:
    VcdDumper();
    ~VcdDumper();

    void set_allowed(bool al);
    bool is_enabled() const;
    std::string get_filename() const;

    void enable(const std::string& fname);
    void set_levels(uint32_t lvl);
    void init_header(const std::vector<Net>& netlist, uint64_t initial_time);
    void dump_sample(const std::vector<Net>& netlist, uint64_t current_time);
    void close();

    static std::string generate_vcd_id(size_t index);
};