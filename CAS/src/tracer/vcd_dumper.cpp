#include "../../include/tracer/vcd_dumper.hpp"
#include <iostream>
#include <ctime>
#include <unordered_map>

VcdDumper::~VcdDumper() {
    close();
}

std::string VcdDumper::generate_vcd_id(size_t id) {
    std::string res;
    size_t base = 94;
    do {
        res += (char)(33 + (id % base));
        id /= base;
    } while (id > 0);
    return res;
}

void VcdDumper::enable(const std::string& fname) {
    if (!allowed) return;
    filename = fname;
    enabled = true;
}

void VcdDumper::init_header(std::vector<Net>& netlist, uint64_t initial_time) {
    if (!enabled || !allowed || file.is_open()) return;

    file.open(filename);
    if (!file.is_open()) {
        std::cerr << "[CAS ERROR] Unable to create VCD file: " << filename << "\n";
        enabled = false;
        return;
    }

    std::time_t now = std::time(nullptr);
    file << "$date\n   " << std::asctime(std::localtime(&now)) << "$end\n";
    file << "$version\n   CAS (Cycle Accurate Simulator) v1.0.0 [Production Kernel]\n$end\n";
    file << "$timescale 1ns $end\n";

    // Assign IDs and group by hierarchical scope
    std::unordered_map<std::string, std::vector<size_t>> scopes;
    for (size_t i = 0; i < netlist.size(); ++i) {
        if (netlist[i].vcd_id.empty()) {
            netlist[i].vcd_id = generate_vcd_id(i);
        }
        std::string full_name = netlist[i].name;
        size_t dot_pos = full_name.rfind('.');
        std::string scope_name = (dot_pos == std::string::npos) ? "top" : full_name.substr(0, dot_pos);
        scopes[scope_name].push_back(i);
    }

    for (const auto& kv : scopes) {
        file << "$scope module " << kv.first << " $end\n";
        for (size_t net_id : kv.second) {
            const auto& net = netlist[net_id];
            size_t dot_pos = net.name.rfind('.');
            std::string leaf_name = (dot_pos == std::string::npos) ? net.name : net.name.substr(dot_pos + 1);
            file << "$var wire " << net.width << " " << net.vcd_id << " " << leaf_name << " $end\n";
        }
        file << "$upscope $end\n";
    }

    file << "$enddefinitions $end\n";
    file << "$dumpvars\n";
    dump_sample(netlist, initial_time, true);
    file << "$end\n";
}

void VcdDumper::dump_sample(std::vector<Net>& netlist, uint64_t current_time, bool force_dump) {
    if (!enabled || !allowed || !file.is_open()) return;

    if (!force_dump && current_time != last_dump_time) {
        file << "#" << current_time << "\n";
        last_dump_time = current_time;
    }

    for (auto& net : netlist) {
        if (force_dump || net.modified) {
            if (net.width == 1) {
                if (net.value.is_unknown()) file << "x" << net.vcd_id << "\n";
                else file << (net.value.aval & 1) << net.vcd_id << "\n";
            } else {
                file << "b" << net.value.to_bin_string() << " " << net.vcd_id << "\n";
            }
        }
    }
}

void VcdDumper::close() {
    if (file.is_open()) {
        file.close();
    }
}