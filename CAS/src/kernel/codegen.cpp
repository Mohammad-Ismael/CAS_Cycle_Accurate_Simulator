#include "kernel/codegen.hpp"
#include "kernel/elaborator.hpp"
#include "kernel/bytecode.hpp"
#include "frontend/ast_stmt.hpp"
#include "frontend/ast_expr.hpp"
#include "engine.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

namespace {

struct StackElem {
    std::string expr;
    uint32_t width = 0;
    bool is_signed = false;
};

static std::string mask_u64_str(uint32_t width) {
    if (width >= 64) return "0xFFFFFFFFFFFFFFFFULL";
    if (width == 0) return "0x0ULL";
    uint64_t m = (1ULL << width) - 1ULL;
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << m << "ULL";
    return oss.str();
}

static bool eval_expr_opcode(const BytecodeInst& inst, std::vector<StackElem>& stack,
                             size_t num_nets, const std::vector<Net>* netlist,
                             const std::string& net_arr_name = "nets") {
    switch (inst.op) {
        case Opcode::PUSH_NET_VAL:
        case Opcode::PUSH_NET_U64: {
            uint32_t net_id = inst.arg0;
            if (net_id >= num_nets) return false;
            uint32_t w = inst.arg1;
            bool s = false;
            if (netlist && net_id < netlist->size()) {
                w = (*netlist)[net_id].width;
                s = (*netlist)[net_id].is_signed;
            }
            stack.push_back({net_arr_name + "[" + std::to_string(net_id) + "]", w, s});
            return true;
        }
        case Opcode::PUSH_CONST_VAL:
        case Opcode::PUSH_CONST_U64: {
            uint32_t w = inst.arg0;
            bool s = (inst.arg1 != 0);
            std::ostringstream oss;
            oss << "0x" << std::hex << std::uppercase << inst.imm64 << "ULL";
            stack.push_back({oss.str(), w, s});
            return true;
        }
        case Opcode::ADD: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            uint32_t w = std::min(64U, std::max(a.width, b.width) + 1);
            stack.push_back({"((" + a.expr + " + " + b.expr + ") & " + mask_u64_str(w) + ")", w, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::SUB: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            uint32_t w = std::min(64U, std::max(a.width, b.width) + 1);
            stack.push_back({"((" + a.expr + " - " + b.expr + ") & " + mask_u64_str(w) + ")", w, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::MUL: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            uint32_t w = std::min(64U, a.width + b.width);
            stack.push_back({"((" + a.expr + " * " + b.expr + ") & " + mask_u64_str(w) + ")", w, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::DIV: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"(" + b.expr + " != 0ULL ? ((" + a.expr + " / " + b.expr + ") & " + mask_u64_str(a.width) + ") : 0ULL)",
                             a.width, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::MOD: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"(" + b.expr + " != 0ULL ? ((" + a.expr + " % " + b.expr + ") & " + mask_u64_str(a.width) + ") : 0ULL)",
                             a.width, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::AND: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            uint32_t w = std::max(a.width, b.width);
            stack.push_back({"(" + a.expr + " & " + b.expr + ")", w, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::OR: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            uint32_t w = std::max(a.width, b.width);
            stack.push_back({"(" + a.expr + " | " + b.expr + ")", w, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::XOR: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            uint32_t w = std::max(a.width, b.width);
            stack.push_back({"(" + a.expr + " ^ " + b.expr + ")", w, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::XNOR: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            uint32_t w = std::max(a.width, b.width);
            stack.push_back({"((~(" + a.expr + " ^ " + b.expr + ")) & " + mask_u64_str(w) + ")", w, a.is_signed && b.is_signed});
            return true;
        }
        case Opcode::SHL: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"(" + b.expr + " >= " + std::to_string(a.width) + "ULL ? 0ULL : ((" + a.expr + " << " + b.expr + ") & " + mask_u64_str(a.width) + "))",
                             a.width, a.is_signed});
            return true;
        }
        case Opcode::SHR: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"(" + b.expr + " >= " + std::to_string(a.width) + "ULL ? 0ULL : (" + a.expr + " >> " + b.expr + "))",
                             a.width, a.is_signed});
            return true;
        }
        case Opcode::SRA: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            bool is_s = (inst.arg1 != 0);
            stack.push_back({"cas_sra64(" + a.expr + ", " + std::to_string(a.width) + ", " + b.expr + ", " + (is_s ? "true" : "false") + ")",
                             a.width, a.is_signed});
            return true;
        }
        case Opcode::EQ:
        case Opcode::CASE_EQ: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"(" + a.expr + " == " + b.expr + " ? 1ULL : 0ULL)", 1, false});
            return true;
        }
        case Opcode::NE:
        case Opcode::CASE_NE: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"(" + a.expr + " != " + b.expr + " ? 1ULL : 0ULL)", 1, false});
            return true;
        }
        case Opcode::LT: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            bool is_s = (inst.arg1 != 0);
            if (is_s) {
                stack.push_back({"((int64_t)(" + a.expr + ") < (int64_t)(" + b.expr + ") ? 1ULL : 0ULL)", 1, false});
            } else {
                stack.push_back({"(" + a.expr + " < " + b.expr + " ? 1ULL : 0ULL)", 1, false});
            }
            return true;
        }
        case Opcode::LE: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            bool is_s = (inst.arg1 != 0);
            if (is_s) {
                stack.push_back({"((int64_t)(" + a.expr + ") <= (int64_t)(" + b.expr + ") ? 1ULL : 0ULL)", 1, false});
            } else {
                stack.push_back({"(" + a.expr + " <= " + b.expr + " ? 1ULL : 0ULL)", 1, false});
            }
            return true;
        }
        case Opcode::GT: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            bool is_s = (inst.arg1 != 0);
            if (is_s) {
                stack.push_back({"((int64_t)(" + a.expr + ") > (int64_t)(" + b.expr + ") ? 1ULL : 0ULL)", 1, false});
            } else {
                stack.push_back({"(" + a.expr + " > " + b.expr + " ? 1ULL : 0ULL)", 1, false});
            }
            return true;
        }
        case Opcode::GE: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            bool is_s = (inst.arg1 != 0);
            if (is_s) {
                stack.push_back({"((int64_t)(" + a.expr + ") >= (int64_t)(" + b.expr + ") ? 1ULL : 0ULL)", 1, false});
            } else {
                stack.push_back({"(" + a.expr + " >= " + b.expr + " ? 1ULL : 0ULL)", 1, false});
            }
            return true;
        }
        case Opcode::LOGIC_AND: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"((" + a.expr + " != 0ULL && " + b.expr + " != 0ULL) ? 1ULL : 0ULL)", 1, false});
            return true;
        }
        case Opcode::LOGIC_OR: {
            if (stack.size() < 2) return false;
            auto b = stack.back(); stack.pop_back();
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"((" + a.expr + " != 0ULL || " + b.expr + " != 0ULL) ? 1ULL : 0ULL)", 1, false});
            return true;
        }
        case Opcode::NOT: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"(" + a.expr + " == 0ULL ? 1ULL : 0ULL)", 1, false});
            return true;
        }
        case Opcode::BIT_NOT: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"((~" + a.expr + ") & " + mask_u64_str(a.width) + ")", a.width, a.is_signed});
            return true;
        }
        case Opcode::NEG: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"((~" + a.expr + " + 1ULL) & " + mask_u64_str(a.width) + ")", a.width, true});
            return true;
        }
        case Opcode::RED_AND: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"((" + a.expr + " & " + mask_u64_str(a.width) + ") == " + mask_u64_str(a.width) +
                             (a.width > 0 ? " ? 1ULL : 0ULL)" : " ? 0ULL : 0ULL)"), 1, false});
            return true;
        }
        case Opcode::RED_NAND: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"((" + a.expr + " & " + mask_u64_str(a.width) + ") == " + mask_u64_str(a.width) +
                             (a.width > 0 ? " ? 0ULL : 1ULL)" : " ? 1ULL : 1ULL)"), 1, false});
            return true;
        }
        case Opcode::RED_OR: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"((" + a.expr + " & " + mask_u64_str(a.width) + ") != 0ULL ? 1ULL : 0ULL)", 1, false});
            return true;
        }
        case Opcode::RED_NOR: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"((" + a.expr + " & " + mask_u64_str(a.width) + ") == 0ULL ? 1ULL : 0ULL)", 1, false});
            return true;
        }
        case Opcode::RED_XOR: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"cas_parity64(" + a.expr + " & " + mask_u64_str(a.width) + ")", 1, false});
            return true;
        }
        case Opcode::RED_XNOR: {
            if (stack.empty()) return false;
            auto a = stack.back(); stack.pop_back();
            stack.push_back({"(cas_parity64(" + a.expr + " & " + mask_u64_str(a.width) + ") ^ 1ULL)", 1, false});
            return true;
        }
        case Opcode::TERNARY_MUX: {
            if (stack.size() < 3) return false;
            auto f = stack.back(); stack.pop_back();
            auto t = stack.back(); stack.pop_back();
            auto c = stack.back(); stack.pop_back();
            uint32_t w = std::max(t.width, f.width);
            stack.push_back({"(" + c.expr + " != 0ULL ? " + t.expr + " : " + f.expr + ")", w, t.is_signed && f.is_signed});
            return true;
        }
        case Opcode::EXTRACT_SLICE: {
            if (stack.empty()) return false;
            auto v = stack.back(); stack.pop_back();
            uint32_t slice_w = inst.arg0;
            uint32_t lsb = inst.arg1;
            stack.push_back({"(" + std::to_string(lsb) + "ULL >= 64ULL ? 0ULL : ((" + v.expr + " >> " +
                             std::to_string(lsb) + "ULL) & " + mask_u64_str(slice_w) + "))", slice_w, false});
            return true;
        }
        case Opcode::CONCAT: {
            uint32_t count = inst.arg0;
            uint32_t tot_w = inst.arg1;
            if (stack.size() < count || tot_w > 64) return false;
            std::vector<StackElem> elems(count);
            for (int i = (int)count - 1; i >= 0; --i) {
                elems[i] = stack.back();
                stack.pop_back();
            }
            std::string cat_expr = "((uint64_t)(" + elems[0].expr + " & " + mask_u64_str(elems[0].width) + "))";
            for (size_t i = 1; i < count; ++i) {
                cat_expr = "(((" + cat_expr + " << " + std::to_string(elems[i].width) + "ULL) | ((uint64_t)(" +
                           elems[i].expr + " & " + mask_u64_str(elems[i].width) + "))))";
            }
            stack.push_back({cat_expr, tot_w, false});
            return true;
        }
        default:
            return false;
    }
}

static bool decompile_rca(const ResolvedContinuousAssign& rca, size_t id, size_t num_nets,
                          const std::vector<Net>* netlist, std::ostream& out) {
    if (!rca.compiled_expr.is_compiled || !rca.compiled_expr.is_pure_2state_64) return false;
    if (rca.target.net_id >= num_nets || rca.target.width > 64) return false;
    if (rca.target.offset >= 64 || rca.target.offset + rca.target.width > 64) return false;
    uint32_t target_id = rca.target.net_id;
    uint32_t target_w = rca.target.width;
    uint32_t offset = rca.target.offset;
    uint32_t net_w = (netlist && target_id < netlist->size()) ? (*netlist)[target_id].width : target_w;
    if (net_w > 64) return false;

    std::vector<StackElem> stack;
    for (const auto& inst : rca.compiled_expr.instructions) {
        if (inst.op == Opcode::DONE) break;
        if (!eval_expr_opcode(inst, stack, num_nets, netlist)) return false;
    }
    if (stack.size() != 1) return false;

    auto res = stack.back();
    bool is_signed = (netlist && target_id < netlist->size()) ? (*netlist)[target_id].is_signed : false;

    out << "static inline void eval_native_rca_" << id << "(uint64_t* __restrict nets) {\n";
    std::string rhs = res.expr;
    if (is_signed && res.width < target_w && res.width > 0) {
        rhs = "cas_sign_extend64(" + rhs + ", " + std::to_string(res.width) + ", " + std::to_string(target_w) + ")";
    }
    rhs = "(" + rhs + ") & " + mask_u64_str(target_w);

    if (offset == 0 && target_w == net_w) {
        out << "  nets[" << target_id << "] = " << rhs << ";\n";
    } else {
        std::string slice_mask = mask_u64_str(target_w);
        if (offset > 0) {
            slice_mask = "(" + slice_mask + " << " + std::to_string(offset) + "ULL)";
        }
        out << "  uint64_t v = " << rhs << ";\n";
        out << "  nets[" << target_id << "] = (nets[" << target_id << "] & ~" << slice_mask << ") | ((v << "
            << offset << "ULL) & " << slice_mask << ");\n";
    }
    out << "}\n\n";
    return true;
}

static bool decompile_proc(const BoundProcess& bp, size_t id, size_t num_nets,
                           const std::vector<Net>* netlist, std::ostream& out) {
    if (!bp.is_bytecode_compiled || !bp.compiled_body.is_pure_2state_64) return false;
    const auto& insts = bp.compiled_body.instructions;
    if (insts.empty()) return false;

    std::unordered_set<size_t> jump_targets;
    for (size_t pc = 0; pc < insts.size(); ++pc) {
        const auto& inst = insts[pc];
        if (inst.op == Opcode::JUMP || inst.op == Opcode::JUMP_IF_ZERO) {
            int32_t off = (int32_t)inst.arg0;
            if (off <= 0) return false; // purely forward jumps supported
            size_t target = pc + (size_t)off;
            jump_targets.insert(target);
        }
    }

    std::ostringstream body;
    std::vector<StackElem> stack;

    for (size_t pc = 0; pc < insts.size(); ++pc) {
        if (jump_targets.count(pc)) {
            body << "  L_" << pc << ":;\n";
        }
        const auto& inst = insts[pc];
        switch (inst.op) {
            case Opcode::JUMP: {
                size_t target = pc + (size_t)((int32_t)inst.arg0);
                body << "  goto L_" << target << ";\n";
                break;
            }
            case Opcode::JUMP_IF_ZERO: {
                if (stack.empty()) return false;
                auto cond = stack.back(); stack.pop_back();
                size_t target = pc + (size_t)((int32_t)inst.arg0);
                body << "  if (!(" << cond.expr << ")) goto L_" << target << ";\n";
                break;
            }
            case Opcode::STORE_NET_U64: {
                if (stack.empty()) return false;
                auto val = stack.back(); stack.pop_back();
                uint32_t target_id = inst.arg0;
                if (target_id >= num_nets) return false;
                uint32_t target_w = inst.arg1;
                if (target_w > 64) return false;
                uint32_t net_w = (netlist && target_id < netlist->size()) ? (*netlist)[target_id].width : target_w;
                if (net_w > 64) return false;
                bool is_s = (netlist && target_id < netlist->size()) ? (*netlist)[target_id].is_signed : (inst.imm64 != 0);
                std::string rhs = val.expr;
                if (is_s && val.width < target_w && val.width > 0) {
                    rhs = "cas_sign_extend64(" + rhs + ", " + std::to_string(val.width) + ", " + std::to_string(target_w) + ")";
                }
                rhs = "(" + rhs + ") & " + mask_u64_str(target_w);
                body << "  nets[" << target_id << "] = " << rhs << ";\n";
                break;
            }
            case Opcode::STORE_NET_VAL:
                return false;
            case Opcode::DONE: {
                body << "  return;\n";
                break;
            }
            default: {
                if (!eval_expr_opcode(inst, stack, num_nets, netlist)) return false;
                break;
            }
        }
    }

    if (jump_targets.count(insts.size())) {
        body << "  L_" << insts.size() << ":; return;\n";
    }

    if (!stack.empty()) return false;

    out << "static inline void eval_native_proc_" << id << "(uint64_t* __restrict nets) {\n";
    out << body.str();
    out << "}\n\n";
    return true;
}

struct NativeFlopUpdate {
    uint32_t dst_net_id = UINT32_MAX;
    uint32_t width = 0;
    bool is_signed = false;

    bool has_reset = false;
    std::string rst_expr;
    uint64_t rst_val = 0;

    bool has_enable = false;
    std::string en_expr;

    std::string rhs_expr;
};

struct SimpleAssign {
    uint32_t dst_net_id = UINT32_MAX;
    ExpressionNode* rhs = nullptr;
};

static bool flatten_assign_stmts(const std::vector<std::unique_ptr<StatementNode>>& stmts,
                                 const std::string& scope,
                                 Engine& engine,
                                 std::vector<SimpleAssign>& out_assigns) {
    if (stmts.empty()) return false;
    for (const auto& s_ptr : stmts) {
        StatementNode* s = s_ptr.get();
        if (!s) return false;
        if (auto blk = dynamic_cast<BlockStatementNode*>(s)) {
            if (!flatten_assign_stmts(blk->statements, scope, engine, out_assigns)) return false;
            continue;
        }
        if (auto nba = dynamic_cast<NonBlockingAssignNode*>(s)) {
            if (nba->bit_idx || nba->bit_lsb || nba->stream_lhs || nba->intra_delay > 0 || nba->has_intra_event) {
                return false;
            }
            uint32_t dst = nba->cached_net_id;
            if (dst == UINT32_MAX) {
                size_t resolved = engine.get_net_id(scope, nba->target);
                if (resolved == (size_t)-1) return false;
                dst = (uint32_t)resolved;
            }
            if (dst >= engine.get_netlist().size() || engine.get_netlist()[dst].width > 64) return false;
            out_assigns.push_back({dst, nba->rhs.get()});
            continue;
        }
        if (auto ba = dynamic_cast<BlockingAssignNode*>(s)) {
            if (ba->lhs_targets.size() != 1 || ba->bit_idx || ba->bit_lsb || ba->stream_lhs || ba->intra_delay > 0 || ba->has_intra_event) {
                return false;
            }
            uint32_t dst = ba->cached_net_id;
            if (dst == UINT32_MAX) {
                size_t resolved = engine.get_net_id(scope, ba->lhs_targets[0]);
                if (resolved == (size_t)-1) return false;
                dst = (uint32_t)resolved;
            }
            if (dst >= engine.get_netlist().size() || engine.get_netlist()[dst].width > 64) return false;
            out_assigns.push_back({dst, ba->rhs.get()});
            continue;
        }
        return false;
    }
    return !out_assigns.empty();
}

static bool extract_signal_condition(ExpressionNode* cond,
                                     const std::string& scope,
                                     Engine& engine,
                                     uint32_t& out_net_id,
                                     bool& out_active_low) {
    if (!cond) return false;
    if (auto u = dynamic_cast<UnaryOpNode*>(cond)) {
        if (u->op == "!" || u->op == "~") {
            if (auto id = dynamic_cast<IdentifierNode*>(u->operand.get())) {
                uint32_t nid = id->cached_net_id;
                if (nid == UINT32_MAX) {
                    size_t res = engine.get_net_id(scope, id->name);
                    if (res == (size_t)-1) return false;
                    nid = (uint32_t)res;
                }
                if (nid >= engine.get_netlist().size()) return false;
                out_net_id = nid;
                out_active_low = true;
                return true;
            }
        }
    }
    if (auto b = dynamic_cast<BinaryOpNode*>(cond)) {
        if (b->op == "==" || b->op == "===") {
            IdentifierNode* id = dynamic_cast<IdentifierNode*>(b->lhs.get());
            ExpressionNode* const_side = b->rhs.get();
            if (!id) {
                id = dynamic_cast<IdentifierNode*>(b->rhs.get());
                const_side = b->lhs.get();
            }
            if (id && const_side) {
                LogicVal cv = engine.get_evaluator().eval_expr(const_side, scope);
                if (cv.get_raw_u64() == 0) {
                    uint32_t nid = id->cached_net_id;
                    if (nid == UINT32_MAX) {
                        size_t res = engine.get_net_id(scope, id->name);
                        if (res == (size_t)-1) return false;
                        nid = (uint32_t)res;
                    }
                    if (nid >= engine.get_netlist().size()) return false;
                    out_net_id = nid;
                    out_active_low = true;
                    return true;
                }
            }
        } else if (b->op == "!=" || b->op == "!==") {
            IdentifierNode* id = dynamic_cast<IdentifierNode*>(b->lhs.get());
            ExpressionNode* const_side = b->rhs.get();
            if (!id) {
                id = dynamic_cast<IdentifierNode*>(b->rhs.get());
                const_side = b->lhs.get();
            }
            if (id && const_side) {
                LogicVal cv = engine.get_evaluator().eval_expr(const_side, scope);
                if (cv.get_raw_u64() == 0) {
                    uint32_t nid = id->cached_net_id;
                    if (nid == UINT32_MAX) {
                        size_t res = engine.get_net_id(scope, id->name);
                        if (res == (size_t)-1) return false;
                        nid = (uint32_t)res;
                    }
                    if (nid >= engine.get_netlist().size()) return false;
                    out_net_id = nid;
                    out_active_low = false;
                    return true;
                }
            }
        }
    }
    if (auto id = dynamic_cast<IdentifierNode*>(cond)) {
        uint32_t nid = id->cached_net_id;
        if (nid == UINT32_MAX) {
            size_t res = engine.get_net_id(scope, id->name);
            if (res == (size_t)-1) return false;
            nid = (uint32_t)res;
        }
        if (nid >= engine.get_netlist().size()) return false;
        out_net_id = nid;
        out_active_low = false;
        return true;
    }
    return false;
}

static bool decompile_bytecode_helper(const CompiledExpr& compiled, size_t num_nets,
                                     const std::vector<Net>* netlist, std::string& out_code,
                                     const std::string& net_arr_name) {
    if (!compiled.is_compiled || !compiled.is_pure_2state_64) return false;
    std::vector<StackElem> stack;
    for (const auto& inst : compiled.instructions) {
        if (inst.op == Opcode::DONE) break;
        if (!eval_expr_opcode(inst, stack, num_nets, netlist, net_arr_name)) return false;
    }
    if (stack.size() != 1) return false;
    out_code = stack.back().expr;
    return true;
}

static bool get_rhs_cpp_expr(ExpressionNode* rhs_node,
                             uint32_t target_w,
                             bool target_signed,
                             const std::string& scope,
                             Engine& engine,
                             std::string& out_expr) {
    if (!rhs_node) return false;
    if (auto id = dynamic_cast<IdentifierNode*>(rhs_node)) {
        uint32_t src = id->cached_net_id;
        if (src == UINT32_MAX) {
            size_t res = engine.get_net_id(scope, id->name);
            if (res == (size_t)-1) return false;
            src = (uint32_t)res;
        }
        if (src >= engine.get_netlist().size()) return false;
        const auto& src_net = engine.get_netlist()[src];
        if (src_net.width > 64) return false;
        std::string cur_str = "cur[" + std::to_string(src) + "]";
        if (target_signed && src_net.width < target_w && src_net.width > 0) {
            cur_str = "cas_sign_extend64(" + cur_str + ", " + std::to_string(src_net.width) + ", " + std::to_string(target_w) + ")";
        }
        out_expr = cur_str;
        return true;
    }
    CompiledExpr comp = BytecodeCompiler::compile(rhs_node, scope, engine, target_w, target_signed);
    if (comp.is_compiled && comp.is_pure_2state_64) {
        return decompile_bytecode_helper(comp, engine.get_netlist().size(), &engine.get_netlist(), out_expr, "cur");
    }
    return false;
}

static bool try_extract_sequential_flop(const BoundProcess& bp,
                                        Engine& engine,
                                        std::vector<NativeFlopUpdate>& out_updates) {
    if (!bp.proc || bp.proc->body.empty()) return false;

    // Case 1: bp.is_pure_fast_proc (e.g. prim_flop_no_rst or simple transfers)
    if (bp.is_pure_fast_proc && !bp.fast_assignments.empty()) {
        for (const auto& fa : bp.fast_assignments) {
            uint32_t dst = fa.dst_net_id;
            uint32_t src = fa.src_net_id;
            if (dst >= engine.get_netlist().size() || src >= engine.get_netlist().size()) return false;
            const auto& dst_net = engine.get_netlist()[dst];
            const auto& src_net = engine.get_netlist()[src];
            if (dst_net.width > 64 || src_net.width > 64) return false;
            NativeFlopUpdate u;
            u.dst_net_id = dst;
            u.width = dst_net.width;
            u.is_signed = dst_net.is_signed;
            std::string cur_str = "cur[" + std::to_string(src) + "]";
            if (dst_net.is_signed && src_net.width < dst_net.width && src_net.width > 0) {
                cur_str = "cas_sign_extend64(" + cur_str + ", " + std::to_string(src_net.width) + ", " + std::to_string(dst_net.width) + ")";
            }
            u.rhs_expr = cur_str;
            out_updates.push_back(u);
        }
        return true;
    }

    // Case 2: Unpack top-level statement
    const StatementNode* top_stmt = bp.proc->body[0].get();
    if (bp.proc->body.size() == 1) {
        if (auto blk = dynamic_cast<const BlockStatementNode*>(top_stmt)) {
            if (blk->statements.size() == 1) {
                top_stmt = blk->statements[0].get();
            }
        }
    }

    auto if_stmt = dynamic_cast<const IfStatementNode*>(top_stmt);
    if (if_stmt) {
        std::string rst_expr;
        uint32_t rst_net_id = UINT32_MAX;
        bool rst_active_low = true;
        if (extract_signal_condition(if_stmt->condition.get(), bp.scope, engine, rst_net_id, rst_active_low)) {
            std::string rst_cond = "(cur[" + std::to_string(rst_net_id) + "] & 1ULL)";
            rst_expr = rst_active_low ? ("!(" + rst_cond + ")") : rst_cond;
        } else {
            CompiledExpr comp = BytecodeCompiler::compile(if_stmt->condition.get(), bp.scope, engine, 1, false);
            if (comp.is_compiled && comp.is_pure_2state_64) {
                std::string decomp;
                if (decompile_bytecode_helper(comp, engine.get_netlist().size(), &engine.get_netlist(), decomp, "cur")) {
                    rst_expr = "((" + decomp + ") != 0ULL)";
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }

        std::vector<SimpleAssign> then_assigns;
        if (!flatten_assign_stmts(if_stmt->then_branch, bp.scope, engine, then_assigns)) {
            return false;
        }
        std::unordered_map<uint32_t, uint64_t> reset_vals;
        for (const auto& ta : then_assigns) {
            LogicVal lv = engine.get_evaluator().eval_expr(ta.rhs, bp.scope);
            reset_vals[ta.dst_net_id] = lv.get_raw_u64();
        }

        if (if_stmt->else_branch.empty()) return false;

        bool has_en = false;
        std::string en_expr;
        std::vector<SimpleAssign> else_assigns;

        const StatementNode* else_top = if_stmt->else_branch[0].get();
        if (if_stmt->else_branch.size() == 1) {
            if (auto blk = dynamic_cast<const BlockStatementNode*>(else_top)) {
                if (blk->statements.size() == 1) {
                    else_top = blk->statements[0].get();
                }
            }
        }

        if (auto en_if = dynamic_cast<const IfStatementNode*>(else_top)) {
            uint32_t en_net_id = UINT32_MAX;
            bool en_active_low = false;
            if (extract_signal_condition(en_if->condition.get(), bp.scope, engine, en_net_id, en_active_low)) {
                has_en = true;
                std::string en_cond = "(cur[" + std::to_string(en_net_id) + "] & 1ULL)";
                en_expr = en_active_low ? ("!(" + en_cond + ")") : en_cond;
            } else {
                CompiledExpr comp = BytecodeCompiler::compile(en_if->condition.get(), bp.scope, engine, 1, false);
                if (comp.is_compiled && comp.is_pure_2state_64) {
                    std::string decomp;
                    if (decompile_bytecode_helper(comp, engine.get_netlist().size(), &engine.get_netlist(), decomp, "cur")) {
                        has_en = true;
                        en_expr = "((" + decomp + ") != 0ULL)";
                    }
                }
            }
            if (has_en) {
                if (!flatten_assign_stmts(en_if->then_branch, bp.scope, engine, else_assigns)) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            if (!flatten_assign_stmts(if_stmt->else_branch, bp.scope, engine, else_assigns)) {
                return false;
            }
        }

        for (const auto& ea : else_assigns) {
            uint32_t dst = ea.dst_net_id;
            const auto& dst_net = engine.get_netlist()[dst];
            std::string rhs_code;
            if (!get_rhs_cpp_expr(ea.rhs, dst_net.width, dst_net.is_signed, bp.scope, engine, rhs_code)) {
                return false;
            }
            NativeFlopUpdate u;
            u.dst_net_id = dst;
            u.width = dst_net.width;
            u.is_signed = dst_net.is_signed;
            u.has_reset = true;
            u.rst_expr = rst_expr;
            u.rst_val = reset_vals.count(dst) ? reset_vals[dst] : 0ULL;
            u.has_enable = has_en;
            u.en_expr = en_expr;
            u.rhs_expr = rhs_code;
            out_updates.push_back(u);
        }
        return !out_updates.empty();
    }

    // Case 3: Direct statement list without if
    std::vector<SimpleAssign> direct_assigns;
    if (flatten_assign_stmts(bp.proc->body, bp.scope, engine, direct_assigns)) {
        for (const auto& da : direct_assigns) {
            uint32_t dst = da.dst_net_id;
            const auto& dst_net = engine.get_netlist()[dst];
            std::string rhs_code;
            if (!get_rhs_cpp_expr(da.rhs, dst_net.width, dst_net.is_signed, bp.scope, engine, rhs_code)) {
                return false;
            }
            NativeFlopUpdate u;
            u.dst_net_id = dst;
            u.width = dst_net.width;
            u.is_signed = dst_net.is_signed;
            u.rhs_expr = rhs_code;
            out_updates.push_back(u);
        }
        return !out_updates.empty();
    }

    return false;
}

static void emit_flop_update(const NativeFlopUpdate& f, std::ostream& out) {
    uint32_t dst = f.dst_net_id;
    uint32_t w = f.width;
    std::string mask = mask_u64_str(w);

    out << "  {\n";
    out << "    uint64_t val;\n";
    if (f.has_reset) {
        out << "    if (" << f.rst_expr << ") {\n";
        std::ostringstream rst_oss;
        rst_oss << "0x" << std::hex << std::uppercase << f.rst_val << "ULL";
        out << "      val = " << rst_oss.str() << ";\n";
        out << "    } else {\n";
        if (f.has_enable) {
            out << "      if (" << f.en_expr << ") {\n";
            out << "        val = (" << f.rhs_expr << ") & " << mask << ";\n";
            out << "      } else {\n";
            out << "        val = cur[" << dst << "];\n";
            out << "      }\n";
        } else {
            out << "      val = (" << f.rhs_expr << ") & " << mask << ";\n";
        }
        out << "    }\n";
    } else if (f.has_enable) {
        out << "    if (" << f.en_expr << ") {\n";
        out << "      val = (" << f.rhs_expr << ") & " << mask << ";\n";
        out << "    } else {\n";
        out << "      val = cur[" << dst << "];\n";
        out << "    }\n";
    } else {
        out << "    val = (" << f.rhs_expr << ") & " << mask << ";\n";
    }
    out << "    next[" << dst << "] = val;\n";
    out << "    if (val != cur[" << dst << "]) modified_mask[" << dst << "] = 1;\n";
    out << "  }\n";
}

} // anonymous namespace

bool NativeCodeGen::generate_comb_source(
    const std::string& output_cpp_path,
    const std::vector<ResolvedContinuousAssign>& rcas,
    std::vector<BoundProcess>& procs,
    Engine& engine
) {
    size_t num_nets = engine.get_netlist().size();
    const std::vector<Net>* netlist = &engine.get_netlist();

    // 1. Identify primary design clock
    std::unordered_map<size_t, size_t> clk_counts;
    for (const auto& bp : procs) {
        if (!bp.proc || bp.proc->type != ProcessType::SEQUENTIAL) continue;
        for (const auto& sens : bp.proc->sensitivity_list) {
            if (sens.edge == EdgeType::POSEDGE) {
                size_t nid = sens.net_id;
                if (nid == (size_t)-1) {
                    nid = engine.get_net_id(bp.scope, sens.signal_name);
                }
                if (nid < engine.get_netlist().size()) {
                    size_t canon = (nid < engine.get_canonical_net_map().size()) ? engine.get_canonical_net_map()[nid] : nid;
                    clk_counts[canon]++;
                }
            }
        }
    }

    size_t primary_clk_id = (size_t)-1;
    size_t max_clk_count = 0;
    for (const auto& kv : clk_counts) {
        if (kv.second > max_clk_count) {
            max_clk_count = kv.second;
            primary_clk_id = kv.first;
        }
    }

    std::vector<NativeFlopUpdate> all_seq_updates;
    std::unordered_set<uint32_t> claimed_dst_nets;
    if (primary_clk_id != (size_t)-1) {
        for (size_t i = 0; i < procs.size(); ++i) {
            auto& bp = procs[i];
            if (!bp.proc || bp.proc->type != ProcessType::SEQUENTIAL) continue;

            bool has_primary_clk = false;
            bool other_sens_ok = true;
            for (const auto& sens : bp.proc->sensitivity_list) {
                size_t nid = sens.net_id;
                if (nid == (size_t)-1) {
                    nid = engine.get_net_id(bp.scope, sens.signal_name);
                }
                size_t canon = (nid < engine.get_canonical_net_map().size()) ? engine.get_canonical_net_map()[nid] : nid;
                if (sens.edge == EdgeType::POSEDGE &&
                    (canon == primary_clk_id || sens.signal_name == "clk_i" || sens.signal_name == "clk" || sens.signal_name == "clk_sys")) {
                    has_primary_clk = true;
                } else if (sens.edge == EdgeType::NEGEDGE || sens.edge == EdgeType::POSEDGE) {
                    // Allowed async reset edge
                } else {
                    other_sens_ok = false;
                }
            }

            if (!has_primary_clk || !other_sens_ok) continue;

            std::vector<NativeFlopUpdate> updates;
            if (try_extract_sequential_flop(bp, engine, updates)) {
                bool conflict = false;
                for (const auto& u : updates) {
                    if (claimed_dst_nets.count(u.dst_net_id)) {
                        conflict = true;
                        break;
                    }
                }
                if (!conflict) {
                    bp.is_native_seq = true;
                    for (auto& u : updates) {
                        claimed_dst_nets.insert(u.dst_net_id);
                        bp.comb_target_nets.push_back(u.dst_net_id);
                        std::cout << "[NATIVE SEQ FLOP " << all_seq_updates.size() << "] dst=" << engine.get_netlist()[u.dst_net_id].name
                                  << " (id " << u.dst_net_id << ") rst_val=0x" << std::hex << u.rst_val << std::dec << "\n";
                        all_seq_updates.push_back(std::move(u));
                    }
                }
            }
        }
    }

    std::ofstream out(output_cpp_path);
    if (!out.is_open()) return false;

    out << "// Auto-generated by CAS NativeCodeGen (Sprint E)\n";
    out << "#include <cstdint>\n";
    out << "#include <cstddef>\n";
    out << "#include <stdbool.h>\n\n";
    out << "typedef void (*NativeEvalFn)(uint64_t* __restrict);\n";
    out << "typedef void (*NativeSeqEvalFn)(const uint64_t* __restrict, uint64_t* __restrict, uint8_t* __restrict);\n\n";

    out << "static inline uint64_t cas_sign_extend64(uint64_t val, uint32_t from_w, uint32_t to_w) {\n";
    out << "    if (from_w >= to_w || from_w == 0 || from_w >= 64) return val;\n";
    out << "    if ((val >> (from_w - 1)) & 1ULL) {\n";
    out << "        uint64_t sign_ext = (~0ULL) << from_w;\n";
    out << "        uint64_t mask = (to_w >= 64) ? ~0ULL : ((1ULL << to_w) - 1ULL);\n";
    out << "        return (val | sign_ext) & mask;\n";
    out << "    }\n";
    out << "    return val;\n";
    out << "}\n\n";

    out << "static inline uint64_t cas_sra64(uint64_t a, uint32_t w, uint64_t b, bool is_signed) {\n";
    out << "    if (!is_signed) return (b >= w) ? 0ULL : (a >> b);\n";
    out << "    if (w == 0) return 0ULL;\n";
    out << "    bool sign_bit = (a >> (w - 1)) & 1ULL;\n";
    out << "    if (b >= w) {\n";
    out << "        return sign_bit ? ((w >= 64) ? ~0ULL : ((1ULL << w) - 1ULL)) : 0ULL;\n";
    out << "    }\n";
    out << "    uint64_t res = a >> b;\n";
    if (netlist) {
        out << "    if (sign_bit) {\n";
        out << "        uint64_t fill_mask = (~0ULL) << (w - b);\n";
        out << "        uint64_t width_mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1ULL);\n";
        out << "        res |= (fill_mask & width_mask);\n";
        out << "    }\n";
    }
    out << "    return res;\n";
    out << "}\n\n";

    out << "static inline uint64_t cas_parity64(uint64_t v) {\n";
    out << "    return static_cast<uint64_t>(__builtin_parityll(v));\n";
    out << "}\n\n";

    std::vector<std::string> rca_fn_ptrs(rcas.size(), "nullptr");
    for (size_t i = 0; i < rcas.size(); ++i) {
        if (decompile_rca(rcas[i], i, num_nets, netlist, out)) {
            rca_fn_ptrs[i] = "&eval_native_rca_" + std::to_string(i);
        }
    }

    std::vector<std::string> proc_fn_ptrs(procs.size(), "nullptr");
    for (size_t i = 0; i < procs.size(); ++i) {
        if (decompile_proc(procs[i], i, num_nets, netlist, out)) {
            proc_fn_ptrs[i] = "&eval_native_proc_" + std::to_string(i);
        }
    }

    out << "extern \"C\" void eval_clock_posedge_native(\n";
    out << "    const uint64_t* __restrict cur,\n";
    out << "    uint64_t* __restrict next,\n";
    out << "    uint8_t* __restrict modified_mask\n";
    out << ") {\n";
    for (const auto& u : all_seq_updates) {
        emit_flop_update(u, out);
    }
    out << "}\n\n";

    size_t rca_tbl_size = rcas.empty() ? 1 : rcas.size();
    out << "static NativeEvalFn rca_table[" << rca_tbl_size << "] = {\n";
    if (rcas.empty()) {
        out << "    nullptr\n";
    } else {
        for (size_t i = 0; i < rcas.size(); ++i) {
            out << "    " << rca_fn_ptrs[i] << ",\n";
        }
    }
    out << "};\n\n";

    size_t proc_tbl_size = procs.empty() ? 1 : procs.size();
    out << "static NativeEvalFn proc_table[" << proc_tbl_size << "] = {\n";
    if (procs.empty()) {
        out << "    nullptr\n";
    } else {
        for (size_t i = 0; i < procs.size(); ++i) {
            out << "    " << proc_fn_ptrs[i] << ",\n";
        }
    }
    out << "};\n\n";

    out << "extern \"C\" void get_native_eval_table(NativeEvalFn** rca_tbl, size_t* rca_cnt, NativeEvalFn** proc_tbl, size_t* proc_cnt, NativeSeqEvalFn* seq_fn) {\n";
    out << "    if (rca_tbl) *rca_tbl = rca_table;\n";
    out << "    if (rca_cnt) *rca_cnt = " << rcas.size() << ";\n";
    out << "    if (proc_tbl) *proc_tbl = proc_table;\n";
    out << "    if (proc_cnt) *proc_cnt = " << procs.size() << ";\n";
    out << "    if (seq_fn) *seq_fn = &eval_clock_posedge_native;\n";
    out << "}\n";

    out.flush();
    return true;
}

bool NativeCodeGen::generate_comb_source(
    const std::string& output_cpp_path,
    const std::vector<ResolvedContinuousAssign>& rcas,
    std::vector<BoundProcess>& procs,
    size_t num_nets
) {
    std::ofstream out(output_cpp_path);
    if (!out.is_open()) return false;

    out << "// Auto-generated by CAS NativeCodeGen (Sprint E)\n";
    out << "#include <cstdint>\n";
    out << "#include <cstddef>\n";
    out << "#include <stdbool.h>\n\n";
    out << "typedef void (*NativeEvalFn)(uint64_t* __restrict);\n";
    out << "typedef void (*NativeSeqEvalFn)(const uint64_t* __restrict, uint64_t* __restrict, uint8_t* __restrict);\n\n";

    out << "static inline uint64_t cas_sign_extend64(uint64_t val, uint32_t from_w, uint32_t to_w) {\n";
    out << "    if (from_w >= to_w || from_w == 0 || from_w >= 64) return val;\n";
    out << "    if ((val >> (from_w - 1)) & 1ULL) {\n";
    out << "        uint64_t sign_ext = (~0ULL) << from_w;\n";
    out << "        uint64_t mask = (to_w >= 64) ? ~0ULL : ((1ULL << to_w) - 1ULL);\n";
    out << "        return (val | sign_ext) & mask;\n";
    out << "    }\n";
    out << "    return val;\n";
    out << "}\n\n";

    out << "static inline uint64_t cas_sra64(uint64_t a, uint32_t w, uint64_t b, bool is_signed) {\n";
    out << "    if (!is_signed) return (b >= w) ? 0ULL : (a >> b);\n";
    out << "    if (w == 0) return 0ULL;\n";
    out << "    bool sign_bit = (a >> (w - 1)) & 1ULL;\n";
    out << "    if (b >= w) {\n";
    out << "        return sign_bit ? ((w >= 64) ? ~0ULL : ((1ULL << w) - 1ULL)) : 0ULL;\n";
    out << "    }\n";
    out << "    uint64_t res = a >> b;\n";
    out << "    if (sign_bit) {\n";
    out << "        uint64_t fill_mask = (~0ULL) << (w - b);\n";
    out << "        uint64_t width_mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1ULL);\n";
    out << "        res |= (fill_mask & width_mask);\n";
    out << "    }\n";
    out << "    return res;\n";
    out << "}\n\n";

    out << "static inline uint64_t cas_parity64(uint64_t v) {\n";
    out << "    return static_cast<uint64_t>(__builtin_parityll(v));\n";
    out << "}\n\n";

    std::vector<std::string> rca_fn_ptrs(rcas.size(), "nullptr");
    for (size_t i = 0; i < rcas.size(); ++i) {
        if (decompile_rca(rcas[i], i, num_nets, nullptr, out)) {
            rca_fn_ptrs[i] = "&eval_native_rca_" + std::to_string(i);
        }
    }

    std::vector<std::string> proc_fn_ptrs(procs.size(), "nullptr");
    for (size_t i = 0; i < procs.size(); ++i) {
        if (decompile_proc(procs[i], i, num_nets, nullptr, out)) {
            proc_fn_ptrs[i] = "&eval_native_proc_" + std::to_string(i);
        }
    }

    out << "extern \"C\" void eval_clock_posedge_native(\n";
    out << "    const uint64_t* __restrict cur,\n";
    out << "    uint64_t* __restrict next,\n";
    out << "    uint8_t* __restrict modified_mask\n";
    out << ") {\n";
    out << "}\n\n";

    size_t rca_tbl_size = rcas.empty() ? 1 : rcas.size();
    out << "static NativeEvalFn rca_table[" << rca_tbl_size << "] = {\n";
    if (rcas.empty()) {
        out << "    nullptr\n";
    } else {
        for (size_t i = 0; i < rcas.size(); ++i) {
            out << "    " << rca_fn_ptrs[i] << ",\n";
        }
    }
    out << "};\n\n";

    size_t proc_tbl_size = procs.empty() ? 1 : procs.size();
    out << "static NativeEvalFn proc_table[" << proc_tbl_size << "] = {\n";
    if (procs.empty()) {
        out << "    nullptr\n";
    } else {
        for (size_t i = 0; i < procs.size(); ++i) {
            out << "    " << proc_fn_ptrs[i] << ",\n";
        }
    }
    out << "};\n\n";

    out << "extern \"C\" void get_native_eval_table(NativeEvalFn** rca_tbl, size_t* rca_cnt, NativeEvalFn** proc_tbl, size_t* proc_cnt, NativeSeqEvalFn* seq_fn) {\n";
    out << "    if (rca_tbl) *rca_tbl = rca_table;\n";
    out << "    if (rca_cnt) *rca_cnt = " << rcas.size() << ";\n";
    out << "    if (proc_tbl) *proc_tbl = proc_table;\n";
    out << "    if (proc_cnt) *proc_cnt = " << procs.size() << ";\n";
    out << "    if (seq_fn) *seq_fn = &eval_clock_posedge_native;\n";
    out << "}\n";

    out.flush();
    return true;
}
