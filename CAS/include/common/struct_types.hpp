#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct StructMember {
    std::string name;
    uint32_t width = 1;
    uint32_t offset = 0; // bit offset from LSB
    bool is_signed = false;
    std::string type_name = "";
    int64_t msb = 0;
    int64_t lsb = 0;
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
