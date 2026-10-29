#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include "logic_val.hpp"

struct ClassNode;

struct ClassObject {
    uint64_t handle_id = 0;
    std::string class_type_name;
    const ClassNode* class_def = nullptr;
    std::unordered_map<std::string, LogicVal> properties;

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
};
