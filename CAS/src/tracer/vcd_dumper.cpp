#include "../../include/tracer/vcd_dumper.hpp"

VcdDumper::VcdDumper() = default;

VcdDumper::~VcdDumper() {
    close();
}

size_t VcdDumper::count_dots(const std::string& name) {
    size_t c = 0;
    for (char ch : name) {
        if (ch == '.') c++;
    }
    return c;
}

std::string VcdDumper::generate_vcd_id(size_t index) {
    std::string id = "";
    do {
        id += (char)('!' + (index % 93));
        index /= 93;
    } while (index > 0);
    return id;
}

void VcdDumper::set_allowed(bool al) {
    allowed = al;
}

bool VcdDumper::is_enabled() const {
    return enabled && allowed;
}

std::string VcdDumper::get_filename() const {
    return filename;
}

void VcdDumper::enable(const std::string& fname) {
    if (!allowed) return;
    filename = fname;
    enabled = true;
    vcd_file.open(fname);
}

void VcdDumper::set_levels(uint32_t lvl) {
    max_levels = lvl;
}

void VcdDumper::dump_value_change(const LogicVal& val, const std::string& vcd_id) {
    if (val.width == 1) {
        if (val.is_unknown()) {
            vcd_file << "x" << vcd_id << "\n";
        } else {
            vcd_file << (val.aval & 1) << vcd_id << "\n";
        }
    } else {
        if (val.is_unknown()) {
            vcd_file << "bx " << vcd_id << "\n";
        } else {
            vcd_file << "b" << val.to_bin_string() << " " << vcd_id << "\n";
        }
    }
}

void VcdDumper::init_header(const std::vector<Net>& netlist, uint64_t initial_time) {
    if (!is_enabled()) return;

    is_net_dumpable.clear();
    for (size_t i = 0; i < netlist.size(); ++i) {
        const auto& name = netlist[i].name;
        // Exclude internal synthesized interconnects
        if (name.find(".__") != std::string::npos) {
            is_net_dumpable[i] = false;
            continue;
        }
        if (max_levels == 0) {
            is_net_dumpable[i] = true;
        } else {
            size_t dots = count_dots(name);
            is_net_dumpable[i] = (dots <= max_levels);
        }
    }

    vcd_file << "$date\n  CAS Simulator Generated\n$end\n";
    vcd_file << "$version\n  CAS v1.0.0\n$end\n";
    vcd_file << "$timescale\n  1ns\n$end\n";
    vcd_file << "$scope module top $end\n";

    for (size_t i = 0; i < netlist.size(); ++i) {
        if (!is_net_dumpable[i]) continue;
        const auto& net = netlist[i];
        vcd_file << "$var wire " << net.width << " " << net.vcd_id << " " << net.name << " $end\n";
    }

    vcd_file << "$upscope $end\n";
    vcd_file << "$enddefinitions $end\n";
    vcd_file << "$dumpvars\n";

    last_dumped_values.resize(netlist.size(), LogicVal::make_x(1));

    for (size_t i = 0; i < netlist.size(); ++i) {
        if (!is_net_dumpable[i]) continue;
        const auto& net = netlist[i];
        dump_value_change(net.value, net.vcd_id);
        last_dumped_values[i] = net.value;
    }

    vcd_file << "$end\n";
}

void VcdDumper::dump_sample(const std::vector<Net>& netlist, uint64_t current_time) {
    if (!is_enabled() || !vcd_file.is_open()) return;

    bool time_emitted = false;

    for (size_t i = 0; i < netlist.size(); ++i) {
        if (i < is_net_dumpable.size() && !is_net_dumpable[i]) continue;
        const auto& net = netlist[i];

        if (i >= last_dumped_values.size() || net.value != last_dumped_values[i]) {
            if (!time_emitted) {
                vcd_file << "#" << current_time << "\n";
                time_emitted = true;
            }
            dump_value_change(net.value, net.vcd_id);
            if (i >= last_dumped_values.size()) {
                last_dumped_values.resize(netlist.size(), LogicVal::make_x(1));
            }
            last_dumped_values[i] = net.value;
        }
    }
}

void VcdDumper::close() {
    if (vcd_file.is_open()) {
        vcd_file.close();
    }
}