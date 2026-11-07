#include "kernel/codegen.hpp"
#include "kernel/elaborator.hpp"
#include "kernel/bytecode.hpp"
#include "engine.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_set>

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
                             size_t num_nets, const std::vector<Net>* netlist) {
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
            stack.push_back({"nets[" + std::to_string(net_id) + "]", w, s});
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

} // anonymous namespace

bool NativeCodeGen::generate_comb_source(
    const std::string& output_cpp_path,
    const std::vector<ResolvedContinuousAssign>& rcas,
    const std::vector<BoundProcess>& procs,
    const Engine& engine
) {
    size_t num_nets = engine.netlist.size();
    const std::vector<Net>* netlist = &engine.netlist;

    std::ofstream out(output_cpp_path);
    if (!out.is_open()) return false;

    out << "// Auto-generated by CAS NativeCodeGen (Sprint D)\n";
    out << "#include <cstdint>\n";
    out << "#include <cstddef>\n";
    out << "#include <stdbool.h>\n\n";
    out << "typedef void (*NativeEvalFn)(uint64_t* __restrict);\n\n";

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

    out << "extern \"C\" void get_native_eval_table(NativeEvalFn** rca_tbl, size_t* rca_cnt, NativeEvalFn** proc_tbl, size_t* proc_cnt) {\n";
    out << "    if (rca_tbl) *rca_tbl = rca_table;\n";
    out << "    if (rca_cnt) *rca_cnt = " << rcas.size() << ";\n";
    out << "    if (proc_tbl) *proc_tbl = proc_table;\n";
    out << "    if (proc_cnt) *proc_cnt = " << procs.size() << ";\n";
    out << "}\n";

    out.flush();
    return true;
}

bool NativeCodeGen::generate_comb_source(
    const std::string& output_cpp_path,
    const std::vector<ResolvedContinuousAssign>& rcas,
    const std::vector<BoundProcess>& procs,
    size_t num_nets
) {
    std::ofstream out(output_cpp_path);
    if (!out.is_open()) return false;

    out << "// Auto-generated by CAS NativeCodeGen (Sprint D)\n";
    out << "#include <cstdint>\n";
    out << "#include <cstddef>\n";
    out << "#include <stdbool.h>\n\n";
    out << "typedef void (*NativeEvalFn)(uint64_t* __restrict);\n\n";

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

    out << "extern \"C\" void get_native_eval_table(NativeEvalFn** rca_tbl, size_t* rca_cnt, NativeEvalFn** proc_tbl, size_t* proc_cnt) {\n";
    out << "    if (rca_tbl) *rca_tbl = rca_table;\n";
    out << "    if (rca_cnt) *rca_cnt = " << rcas.size() << ";\n";
    out << "    if (proc_tbl) *proc_tbl = proc_table;\n";
    out << "    if (proc_cnt) *proc_cnt = " << procs.size() << ";\n";
    out << "}\n";

    out.flush();
    return true;
}
