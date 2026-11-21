#include "kernel/vpi_runtime.hpp"
#include "engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#endif

namespace {
    Engine* g_vpi_engine = nullptr;
    std::vector<std::unique_ptr<VpiObjectImpl>> g_vpi_objects;
    std::vector<std::unique_ptr<VpiCallbackImpl>> g_vpi_callbacks;
    std::unordered_map<std::string, VpiSystfImpl> g_vpi_systfs;
    uint32_t g_cb_id_counter = 1;
    std::string g_last_str_buf;
    std::vector<s_vpi_vecval> g_vecval_buf;
}

namespace VpiRuntime {

void initialize() {
    g_vpi_objects.clear();
    g_vpi_callbacks.clear();
    g_vpi_systfs.clear();
    g_cb_id_counter = 1;
}

void cleanup() {
    g_vpi_objects.clear();
    g_vpi_callbacks.clear();
    g_vpi_systfs.clear();
    g_vpi_engine = nullptr;
}

void set_active_engine(Engine* eng) {
    g_vpi_engine = eng;
}

Engine* get_active_engine() {
    return g_vpi_engine;
}

VpiObjectImpl* allocate_handle(int type, const std::string& name, const std::string& full_name, size_t net_id, uint32_t width, bool is_signed) {
    auto obj = std::make_unique<VpiObjectImpl>();
    obj->type = type;
    obj->name = name;
    obj->full_name = full_name;
    obj->net_id = net_id;
    obj->width = width;
    obj->is_signed = is_signed;
    VpiObjectImpl* ptr = obj.get();
    g_vpi_objects.push_back(std::move(obj));
    return ptr;
}

VpiSystfImpl* find_systf(const std::string& name) {
    auto it = g_vpi_systfs.find(name);
    if (it != g_vpi_systfs.end()) return &it->second;
    return nullptr;
}

void load_vpi_library(const std::string& path) {
#ifndef _WIN32
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "[CAS-VPI-WARN] Failed to load VPI library '" << path << "': " << dlerror() << std::endl;
        return;
    }
    typedef void (*vlog_routine_t)(void);
    vlog_routine_t* routines = reinterpret_cast<vlog_routine_t*>(dlsym(handle, "vlog_startup_routines"));
    if (routines) {
        for (int i = 0; routines[i] != nullptr; ++i) {
            routines[i]();
        }
    }
#endif
}

void dispatch_start_of_simulation(Engine* eng) {
    g_vpi_engine = eng;
    for (auto& cb : g_vpi_callbacks) {
        if (!cb->removed && cb->reason == cbStartOfSimulation && cb->cb_rtn) {
            s_cb_data data;
            data.reason = cb->reason;
            data.cb_rtn = cb->cb_rtn;
            data.obj = cb->obj;
            data.time = &cb->time_data;
            data.value = &cb->val_data;
            data.index = 0;
            data.user_data = cb->user_data;
            cb->cb_rtn(&data);
        }
    }
}

void dispatch_end_of_simulation(Engine* eng) {
    g_vpi_engine = eng;
    for (auto& cb : g_vpi_callbacks) {
        if (!cb->removed && cb->reason == cbEndOfSimulation && cb->cb_rtn) {
            s_cb_data data;
            data.reason = cb->reason;
            data.cb_rtn = cb->cb_rtn;
            data.obj = cb->obj;
            data.time = &cb->time_data;
            data.value = &cb->val_data;
            data.index = 0;
            data.user_data = cb->user_data;
            cb->cb_rtn(&data);
        }
    }
}

void dispatch_time_callbacks(uint64_t current_time) {
    for (auto& cb : g_vpi_callbacks) {
        if (!cb->removed && cb->reason == cbAfterDelay && cb->cb_rtn) {
            if (current_time >= cb->target_time) {
                s_cb_data data;
                data.reason = cb->reason;
                data.cb_rtn = cb->cb_rtn;
                data.obj = cb->obj;
                cb->time_data.type = vpiSimTime;
                cb->time_data.high = static_cast<uint32_t>(current_time >> 32);
                cb->time_data.low  = static_cast<uint32_t>(current_time & 0xFFFFFFFFULL);
                cb->time_data.real = static_cast<double>(current_time);
                data.time = &cb->time_data;
                data.value = &cb->val_data;
                data.index = 0;
                data.user_data = cb->user_data;
                cb->removed = true;
                cb->cb_rtn(&data);
            }
        }
    }
}

void dispatch_value_change(size_t net_id, const LogicVal& new_val) {
    for (auto& cb : g_vpi_callbacks) {
        if (!cb->removed && cb->reason == cbValueChange && cb->cb_rtn) {
            VpiObjectImpl* obj = reinterpret_cast<VpiObjectImpl*>(cb->obj);
            if (obj && obj->net_id == net_id) {
                s_cb_data data;
                data.reason = cb->reason;
                data.cb_rtn = cb->cb_rtn;
                data.obj = cb->obj;
                uint64_t t = g_vpi_engine ? g_vpi_engine->get_sim_time() : 0;
                cb->time_data.type = vpiSimTime;
                cb->time_data.high = static_cast<uint32_t>(t >> 32);
                cb->time_data.low  = static_cast<uint32_t>(t & 0xFFFFFFFFULL);
                cb->time_data.real = static_cast<double>(t);
                data.time = &cb->time_data;

                if (cb->val_data.format == vpiIntVal) {
                    cb->val_data.value.integer = static_cast<int32_t>(new_val.aval);
                } else if (cb->val_data.format == vpiScalarVal) {
                    cb->val_data.value.scalar = (new_val.aval & 1) ? vpi1 : vpi0;
                }
                data.value = &cb->val_data;
                data.index = 0;
                data.user_data = cb->user_data;
                cb->cb_rtn(&data);
            }
        }
    }
}

} // namespace VpiRuntime

extern "C" {

vpiHandle vpi_register_cb(p_cb_data cb_data_p) {
    if (!cb_data_p || !cb_data_p->cb_rtn) return nullptr;
    auto cb = std::make_unique<VpiCallbackImpl>();
    cb->id = g_cb_id_counter++;
    cb->reason = cb_data_p->reason;
    cb->cb_rtn = cb_data_p->cb_rtn;
    cb->obj = cb_data_p->obj;
    cb->user_data = cb_data_p->user_data;
    if (cb_data_p->time) {
        cb->time_data = *cb_data_p->time;
        uint64_t cur_t = g_vpi_engine ? g_vpi_engine->get_sim_time() : 0;
        uint64_t delta_t = (static_cast<uint64_t>(cb->time_data.high) << 32) | cb->time_data.low;
        cb->target_time = cur_t + delta_t;
    }
    if (cb_data_p->value) {
        cb->val_data = *cb_data_p->value;
    }
    VpiCallbackImpl* ptr = cb.get();
    g_vpi_callbacks.push_back(std::move(cb));
    return reinterpret_cast<vpiHandle>(ptr);
}

int32_t vpi_remove_cb(vpiHandle cb_obj) {
    if (!cb_obj) return 0;
    VpiCallbackImpl* cb = reinterpret_cast<VpiCallbackImpl*>(cb_obj);
    cb->removed = true;
    return 1;
}

vpiHandle vpi_register_systf(p_vpi_systf_data systf_data_p) {
    if (!systf_data_p || !systf_data_p->tfname) return nullptr;
    std::string name = systf_data_p->tfname;
    VpiSystfImpl impl;
    impl.data = *systf_data_p;
    g_vpi_systfs[name] = impl;
    auto obj = VpiRuntime::allocate_handle(vpiTask, name, name);
    return reinterpret_cast<vpiHandle>(obj);
}

vpiHandle vpi_handle_by_name(char *name, vpiHandle scope) {
    if (!name) return nullptr;
    std::string sname = name;
    Engine* eng = g_vpi_engine;
    if (!eng) return nullptr;

    std::string full_name = sname;
    if (scope) {
        VpiObjectImpl* sobj = reinterpret_cast<VpiObjectImpl*>(scope);
        if (sobj && !sobj->full_name.empty()) {
            full_name = sobj->full_name + "." + sname;
        }
    }

    size_t nid = (size_t)-1;
    const auto& nmap = eng->get_net_map();
    if (nmap.count(full_name)) {
        nid = nmap.at(full_name);
    } else if (nmap.count(sname)) {
        nid = nmap.at(sname);
        full_name = sname;
    } else if (!eng->active_scope_name.empty() && nmap.count(eng->active_scope_name + "." + sname)) {
        full_name = eng->active_scope_name + "." + sname;
        nid = nmap.at(full_name);
    }

    if (nid == (size_t)-1) {
        for (const auto& kv : nmap) {
            if (kv.first == sname || (kv.first.size() > sname.size() && kv.first.compare(kv.first.size() - sname.size() - 1, sname.size() + 1, "." + sname) == 0)) {
                nid = kv.second;
                full_name = kv.first;
                break;
            }
        }
    }

    if (nid != (size_t)-1) {
        auto obj = VpiRuntime::allocate_handle(
            vpiNet,
            sname,
            full_name,
            nid,
            eng->get_netlist()[nid].width,
            eng->get_netlist()[nid].is_signed
        );
        return reinterpret_cast<vpiHandle>(obj);
    }

    if (!eng->active_scope_name.empty() && (full_name == eng->active_scope_name || sname == eng->active_scope_name)) {
        auto obj = VpiRuntime::allocate_handle(
            vpiModule,
            sname,
            full_name
        );
        return reinterpret_cast<vpiHandle>(obj);
    }

    return nullptr;
}

vpiHandle vpi_handle(int32_t type, vpiHandle refHandle) {
    if (!refHandle) {
        if (type == vpiModule && g_vpi_engine && !g_vpi_engine->active_scope_name.empty()) {
            auto obj = VpiRuntime::allocate_handle(vpiModule, g_vpi_engine->active_scope_name, g_vpi_engine->active_scope_name);
            return reinterpret_cast<vpiHandle>(obj);
        }
        return nullptr;
    }
    VpiObjectImpl* obj = reinterpret_cast<VpiObjectImpl*>(refHandle);
    if (type == vpiModule) {
        std::string pscope = "";
        auto dot = obj->full_name.rfind('.');
        if (dot != std::string::npos) pscope = obj->full_name.substr(0, dot);
        auto pobj = VpiRuntime::allocate_handle(vpiModule, pscope, pscope);
        return reinterpret_cast<vpiHandle>(pobj);
    }
    return nullptr;
}

vpiHandle vpi_iterate(int32_t type, vpiHandle refHandle) {
    Engine* eng = g_vpi_engine;
    if (!eng) return nullptr;
    std::string prefix = "";
    if (refHandle) {
        VpiObjectImpl* ref = reinterpret_cast<VpiObjectImpl*>(refHandle);
        prefix = ref->full_name + ".";
    }

    auto iter = VpiRuntime::allocate_handle(vpiIterator, "iter", "iter");
    if (type == vpiNet || type == vpiReg) {
        for (const auto& kv : eng->get_net_map()) {
            if (prefix.empty() || kv.first.rfind(prefix, 0) == 0) {
                auto nobj = VpiRuntime::allocate_handle(
                    vpiNet,
                    kv.first.substr(prefix.size()),
                    kv.first,
                    kv.second,
                    eng->get_netlist()[kv.second].width,
                    eng->get_netlist()[kv.second].is_signed
                );
                iter->children.push_back(reinterpret_cast<vpiHandle>(nobj));
            }
        }
    }
    return reinterpret_cast<vpiHandle>(iter);
}

vpiHandle vpi_scan(vpiHandle iterator) {
    if (!iterator) return nullptr;
    VpiObjectImpl* iter = reinterpret_cast<VpiObjectImpl*>(iterator);
    if (iter->scan_idx < iter->children.size()) {
        return iter->children[iter->scan_idx++];
    }
    return nullptr;
}

int32_t vpi_get(int32_t property, vpiHandle object) {
    if (!object) return -1;
    VpiObjectImpl* obj = reinterpret_cast<VpiObjectImpl*>(object);
    switch (property) {
        case vpiType:
            return obj->type;
        case vpiSize:
            return static_cast<int32_t>(obj->width);
        case vpiSigned:
            return obj->is_signed ? 1 : 0;
        case vpiTimeUnit:
            return -9; // 1ns
        case vpiTimePrecision:
            return -12; // 1ps
        default:
            return 0;
    }
}

char *vpi_get_str(int32_t property, vpiHandle object) {
    if (!object) return const_cast<char*>("");
    VpiObjectImpl* obj = reinterpret_cast<VpiObjectImpl*>(object);
    switch (property) {
        case vpiName:
            return const_cast<char*>(obj->name.c_str());
        case vpiFullName:
            return const_cast<char*>(obj->full_name.c_str());
        case vpiType:
            g_last_str_buf = (obj->type == vpiNet) ? "vpiNet" : "vpiModule";
            return const_cast<char*>(g_last_str_buf.c_str());
        default:
            return const_cast<char*>("");
    }
}

void vpi_get_value(vpiHandle expr, p_vpi_value value_p) {
    if (!expr || !value_p) return;
    VpiObjectImpl* obj = reinterpret_cast<VpiObjectImpl*>(expr);
    Engine* eng = g_vpi_engine;
    if (!eng || obj->net_id == (size_t)-1) return;

    LogicVal val = eng->get_netlist()[obj->net_id].value;

    switch (value_p->format) {
        case vpiIntVal:
            value_p->value.integer = static_cast<int32_t>(val.aval);
            break;
        case vpiScalarVal:
            value_p->value.scalar = (val.aval & 1) ? vpi1 : vpi0;
            break;
        case vpiBinStrVal:
            g_last_str_buf = val.to_bin_string();
            value_p->value.str = const_cast<char*>(g_last_str_buf.c_str());
            break;
        case vpiHexStrVal:
            g_last_str_buf = val.to_hex_string();
            value_p->value.str = const_cast<char*>(g_last_str_buf.c_str());
            break;
        case vpiDecStrVal:
            g_last_str_buf = std::to_string(val.aval);
            value_p->value.str = const_cast<char*>(g_last_str_buf.c_str());
            break;
        case vpiStringVal:
            g_last_str_buf = val.to_ascii_string();
            value_p->value.str = const_cast<char*>(g_last_str_buf.c_str());
            break;
        case vpiVectorVal: {
            size_t chunks = (obj->width + 31) / 32;
            if (chunks == 0) chunks = 1;
            g_vecval_buf.resize(chunks);
            g_vecval_buf[0].aval = static_cast<uint32_t>(val.aval);
            g_vecval_buf[0].bval = static_cast<uint32_t>(val.bval);
            if (chunks > 1) {
                g_vecval_buf[1].aval = static_cast<uint32_t>(val.aval >> 32);
                g_vecval_buf[1].bval = static_cast<uint32_t>(val.bval >> 32);
            }
            value_p->value.vector = g_vecval_buf.data();
            break;
        }
        default:
            value_p->value.integer = static_cast<int32_t>(val.aval);
            break;
    }
}

vpiHandle vpi_put_value(vpiHandle object, p_vpi_value value_p, p_vpi_time /*time_p*/, int32_t /*flags*/) {
    if (!object || !value_p) return nullptr;
    VpiObjectImpl* obj = reinterpret_cast<VpiObjectImpl*>(object);
    Engine* eng = g_vpi_engine;
    if (!eng || obj->net_id == (size_t)-1) return nullptr;

    LogicVal new_val(obj->width, 0, 0, obj->is_signed);
    switch (value_p->format) {
        case vpiIntVal:
            new_val = LogicVal::make_known(obj->width, static_cast<uint64_t>(value_p->value.integer));
            break;
        case vpiScalarVal:
            new_val = LogicVal::make_known(1, (value_p->value.scalar == vpi1) ? 1 : 0);
            break;
        case vpiBinStrVal:
            new_val = LogicVal::make_known(obj->width, value_p->value.str ? static_cast<uint64_t>(std::strtoull(value_p->value.str, nullptr, 2)) : 0);
            break;
        case vpiHexStrVal:
            new_val = LogicVal::make_known(obj->width, value_p->value.str ? static_cast<uint64_t>(std::strtoull(value_p->value.str, nullptr, 16)) : 0);
            break;
        case vpiDecStrVal:
            new_val = LogicVal::make_known(obj->width, value_p->value.str ? static_cast<uint64_t>(std::strtoull(value_p->value.str, nullptr, 10)) : 0);
            break;
        case vpiOctStrVal:
            new_val = LogicVal::make_known(obj->width, value_p->value.str ? static_cast<uint64_t>(std::strtoull(value_p->value.str, nullptr, 8)) : 0);
            break;
        case vpiVectorVal:
            if (value_p->value.vector) {
                uint64_t a = value_p->value.vector[0].aval;
                uint64_t b = value_p->value.vector[0].bval;
                new_val = LogicVal(obj->width, a, b, obj->is_signed);
            }
            break;
        default:
            break;
    }

    eng->get_netlist()[obj->net_id].set(new_val);
    VpiRuntime::dispatch_value_change(obj->net_id, new_val);
    return object;
}

void vpi_get_time(vpiHandle /*object*/, p_vpi_time time_p) {
    if (!time_p) return;
    uint64_t t = g_vpi_engine ? g_vpi_engine->get_sim_time() : 0;
    time_p->type = vpiSimTime;
    time_p->high = static_cast<uint32_t>(t >> 32);
    time_p->low  = static_cast<uint32_t>(t & 0xFFFFFFFFULL);
    time_p->real = static_cast<double>(t);
}

int32_t vpi_printf(const char *format, ...) {
    if (!format) return 0;
    va_list ap;
    va_start(ap, format);
    int ret = std::vprintf(format, ap);
    va_end(ap);
    return ret;
}

int32_t vpi_vprintf(const char *format, va_list ap) {
    if (!format) return 0;
    return std::vprintf(format, ap);
}

int32_t vpi_control(int32_t operation, ...) {
    if (operation == vpiFinish) {
        if (g_vpi_engine) {
            std::cout << "[CAS $finish VPI] Simulation Terminated at time " << g_vpi_engine->get_sim_time() << "." << std::endl;
            g_vpi_engine->get_scheduler().terminate();
        }
        return 0;
    } else if (operation == vpiStop) {
        if (g_vpi_engine) {
            std::cout << "[CAS $stop VPI] Simulation Stopped at time " << g_vpi_engine->get_sim_time() << "." << std::endl;
        }
        return 0;
    }
    return 0;
}

int32_t vpi_free_object(vpiHandle /*object*/) {
    return 1;
}

} // extern "C"
