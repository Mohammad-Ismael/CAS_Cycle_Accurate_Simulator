#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "logic_val.hpp"

struct ExpressionNode;

struct StructMember {
    std::string name;
    uint32_t width = 1;
    uint32_t offset = 0; // bit offset from LSB
    bool is_signed = false;
    std::string type_name = "";
    int64_t msb = 0;
    int64_t lsb = 0;
    std::shared_ptr<ExpressionNode> default_expr = nullptr;
    bool has_default = false;
    LogicVal default_val;
    uint32_t elem_width = 1;
    int64_t outer_low = 0;
};

struct PackedStructType {
    std::string name;
    uint32_t total_width = 0;
    bool is_signed = false;
    bool is_union = false;
    bool is_tagged = false;
    uint32_t tag_bits = 0;
    uint32_t payload_width = 0;
    std::vector<StructMember> members;

    const StructMember* get_member(const std::string& member_name) const {
        for (const auto& m : members) {
            if (m.name == member_name) {
                return &m;
            }
        }
        return nullptr;
    }

    int get_member_index(const std::string& member_name) const {
        for (size_t i = 0; i < members.size(); ++i) {
            if (members[i].name == member_name) {
                return (int)i;
            }
        }
        return -1;
    }
};
