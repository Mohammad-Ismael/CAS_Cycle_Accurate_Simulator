#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include "logic_val.hpp"

struct ClassNode;

struct VirtualInterfaceHandle {
    uint64_t id = 0;
    std::string inst_path;
    std::string modport;
    std::string iface_type;
    bool is_valid_vif = false;
};

struct ClassObject {
    uint64_t handle_id = 0;
    std::string class_type_name;
    const ClassNode* class_def = nullptr;
    std::unordered_map<std::string, LogicVal> properties;
    std::unordered_map<std::string, VirtualInterfaceHandle> virtual_interfaces;
    uint32_t prng_state = 0x12345678;

    ClassObject() = default;
    ClassObject(uint64_t id, std::string type_name, const ClassNode* def = nullptr)
        : handle_id(id), class_type_name(std::move(type_name)), class_def(def) {}

    bool has_property(const std::string& name) const {
        return properties.find(name) != properties.end();
    }

    LogicVal get_property(const std::string& name) const {
        auto it = properties.find(name);
        if (it != properties.end()) return it->second;
        return LogicVal::make_x(32);
    }

    void set_property(const std::string& name, const LogicVal& val) {
        properties[name] = val;
    }

    bool has_virtual_interface(const std::string& name) const {
        return virtual_interfaces.find(name) != virtual_interfaces.end();
    }

    VirtualInterfaceHandle get_virtual_interface(const std::string& name) const {
        auto it = virtual_interfaces.find(name);
        if (it != virtual_interfaces.end()) return it->second;
        return VirtualInterfaceHandle();
    }

    void set_virtual_interface(const std::string& name, const VirtualInterfaceHandle& handle) {
        virtual_interfaces[name] = handle;
    }

    bool is_instance_of(const std::string& target_type) const;

    void srandom(uint32_t seed) {
        prng_state = seed;
    }

    uint32_t next_random() {
        prng_state = prng_state * 429 + 25;
        return prng_state >> 16;
    }
};
