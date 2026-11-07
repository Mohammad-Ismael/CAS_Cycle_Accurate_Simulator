#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "../common/logic_val.hpp"

enum class Opcode : uint8_t {
    PUSH_NET_U64,      // Push 2-state 64-bit value from netlist[net_id]
    PUSH_NET_VAL,      // Push full LogicVal from netlist[net_id]
    PUSH_CONST_U64,    // Push immediate 64-bit constant
    PUSH_CONST_VAL,    // Push immediate LogicVal
    ADD, SUB, MUL, DIV, MOD,
    AND, OR, XOR, XNOR,
    SHL, SHR, SRA,
    EQ, NE, LT, LE, GT, GE,
    CASE_EQ, CASE_NE,
    LOGIC_AND, LOGIC_OR,
    NOT, BIT_NOT, NEG,
    RED_AND, RED_OR, RED_XOR, RED_NAND, RED_NOR, RED_XNOR,
    TERNARY_MUX,       // cond ? true_val : false_val
    EXTRACT_SLICE,     // [msb:lsb]
    CONCAT,
    DONE
};

struct BytecodeInst {
    Opcode op = Opcode::DONE;
    uint32_t arg0 = 0;   // net_id, slice width, concat count, or operand width
    uint32_t arg1 = 0;   // slice lsb, or auxiliary flags (e.g. is_signed)
    uint64_t imm64 = 0;  // immediate constant (for 2-state)
    LogicVal imm_val;    // immediate constant (for 4-state LogicVal)

    BytecodeInst() = default;
    BytecodeInst(Opcode o, uint32_t a0 = 0, uint32_t a1 = 0, uint64_t imm = 0, LogicVal val = LogicVal())
        : op(o), arg0(a0), arg1(a1), imm64(imm), imm_val(std::move(val)) {}
};

struct CompiledExpr {
    std::vector<BytecodeInst> instructions;
    bool is_compiled = false;
    bool is_pure_2state_64 = false;
    uint32_t result_width = 0;
    bool result_signed = false;
};

class Engine;
struct ExpressionNode;

class BytecodeCompiler {
private:
    struct CompileContext;
    static bool compile_node(ExpressionNode* node, CompileContext& ctx, uint32_t& out_width, bool& out_signed);
public:
    static CompiledExpr compile(ExpressionNode* expr, const std::string& scope, Engine& engine, uint32_t target_width, bool target_signed);
};
