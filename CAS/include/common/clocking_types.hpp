#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "edge_types.hpp"
#include "logic_val.hpp"

struct ClockingSignalInfo {
    std::string direction; // "input", "output", "inout"
    std::string signal_name;
    size_t net_id = (size_t)-1;
    std::string skew = "";
    LogicVal sampled_value = LogicVal::make_x(1);
};

struct ClockingPendingDrive {
    size_t net_id;
    LogicVal val;
};

struct ClockingBlockInstance {
    std::string name;
    std::string scope;
    bool is_default = false;
    EdgeType clock_edge = EdgeType::POSEDGE;
    std::string clock_signal;
    size_t clock_net_id = (size_t)-1;

    std::string default_input_skew = "1step";
    std::string default_output_skew = "0";

    std::unordered_map<std::string, ClockingSignalInfo> signals;
    std::vector<std::string> signal_names;
    bool fired_in_time_slot = false;
    std::vector<ClockingPendingDrive> pending_drives;
};