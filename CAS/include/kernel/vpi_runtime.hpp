#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "../dpi/vpi_user.h"
#include "common/logic_val.hpp"

class Engine;

struct VpiObjectImpl {
    int type = 0;
    std::string name;
    std::string full_name;
    size_t net_id = (size_t)-1;
    uint32_t width = 0;
    bool is_signed = false;
    void* user_ptr = nullptr;
    std::vector<vpiHandle> children;
    size_t scan_idx = 0;
};

struct VpiCallbackImpl {
    uint32_t id = 0;
    int reason = 0;
    int (*cb_rtn)(p_cb_data) = nullptr;
    vpiHandle obj = nullptr;
    uint64_t target_time = 0;
    s_vpi_time time_data;
    s_vpi_value val_data;
    char* user_data = nullptr;
    bool removed = false;
};

struct VpiSystfImpl {
    s_vpi_systf_data data;
};

namespace VpiRuntime {
    void initialize();
    void cleanup();
    void set_active_engine(Engine* eng);
    Engine* get_active_engine();

    void dispatch_start_of_simulation(Engine* eng);
    void dispatch_end_of_simulation(Engine* eng);
    void dispatch_time_callbacks(uint64_t current_time);
    void dispatch_value_change(size_t net_id, const LogicVal& new_val);
    bool has_value_change_callbacks();

    void set_args(int argc, char** argv);
    int get_argc();
    char** get_argv();

    void load_vpi_library(const std::string& path);
    VpiObjectImpl* allocate_handle(int type, const std::string& name, const std::string& full_name, size_t net_id = (size_t)-1, uint32_t width = 0, bool is_signed = false);
    VpiSystfImpl* find_systf(const std::string& name);
}
