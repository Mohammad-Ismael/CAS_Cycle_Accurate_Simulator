#include "kernel/llvm_jit.hpp"
#include "kernel/elaborator.hpp"
#include "kernel/bytecode.hpp"
#include "kernel/dependency_graph.hpp"
#include "engine.hpp"

#include <iostream>

#if HAS_LLVM_ORC_JIT
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/TargetSelect.h>

namespace {

static inline uint64_t mask_u64(uint32_t width) {
    if (width >= 64) return ~0ULL;
    if (width == 0) return 0ULL;
    return (1ULL << width) - 1ULL;
}

struct LlvmStackElem {
    llvm::Value* val = nullptr;
    uint32_t width = 0;
    bool is_signed = false;
};

static bool emit_rca_llvm(
    const ResolvedContinuousAssign& rca,
    size_t num_nets,
    const std::vector<Net>* netlist,
    llvm::IRBuilder<>& builder,
    llvm::Value* nets_arg
) {
    if (!rca.compiled_expr.is_compiled || !rca.compiled_expr.is_pure_2state_64) return false;
    if (rca.target.net_id >= num_nets || rca.target.width > 64) return false;
    if (rca.target.offset >= 64 || rca.target.offset + rca.target.width > 64) return false;

    uint32_t target_id = rca.target.net_id;
    uint32_t target_w = rca.target.width;
    uint32_t offset = rca.target.offset;
    uint32_t net_w = (netlist && target_id < netlist->size()) ? (*netlist)[target_id].width : target_w;
    if (net_w > 64) return false;

    auto* i64Ty = builder.getInt64Ty();
    std::vector<LlvmStackElem> stack;

    for (const auto& inst : rca.compiled_expr.instructions) {
        if (inst.op == Opcode::DONE) break;
        switch (inst.op) {
            case Opcode::PUSH_NET_VAL:
            case Opcode::PUSH_NET_U64: {
                uint32_t nid = inst.arg0;
                if (nid >= num_nets) return false;
                uint32_t w = inst.arg1;
                bool s = false;
                if (netlist && nid < netlist->size()) {
                    w = (*netlist)[nid].width;
                    s = (*netlist)[nid].is_signed;
                }
                auto* ptr = builder.CreateInBoundsGEP(i64Ty, nets_arg, builder.getInt32(nid));
                auto* val = builder.CreateLoad(i64Ty, ptr);
                stack.push_back({val, w, s});
                break;
            }
            case Opcode::PUSH_CONST_VAL:
            case Opcode::PUSH_CONST_U64: {
                uint32_t w = inst.arg0;
                bool s = (inst.arg1 != 0);
                stack.push_back({builder.getInt64(inst.imm64), w, s});
                break;
            }
            case Opcode::ADD: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                uint32_t w = std::min(64U, std::max(a.width, b.width) + 1);
                auto* sum = builder.CreateAdd(a.val, b.val);
                sum = builder.CreateAnd(sum, builder.getInt64(mask_u64(w)));
                stack.push_back({sum, w, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::SUB: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                uint32_t w = std::min(64U, std::max(a.width, b.width) + 1);
                auto* diff = builder.CreateSub(a.val, b.val);
                diff = builder.CreateAnd(diff, builder.getInt64(mask_u64(w)));
                stack.push_back({diff, w, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::MUL: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                uint32_t w = std::min(64U, a.width + b.width);
                auto* prod = builder.CreateMul(a.val, b.val);
                prod = builder.CreateAnd(prod, builder.getInt64(mask_u64(w)));
                stack.push_back({prod, w, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::DIV: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                auto* is_nz = builder.CreateICmpNE(b.val, builder.getInt64(0));
                auto* safe_b = builder.CreateSelect(is_nz, b.val, builder.getInt64(1));
                auto* div = builder.CreateUDiv(a.val, safe_b);
                auto* masked = builder.CreateAnd(div, builder.getInt64(mask_u64(a.width)));
                stack.push_back({builder.CreateSelect(is_nz, masked, builder.getInt64(0)), a.width, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::MOD: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                auto* is_nz = builder.CreateICmpNE(b.val, builder.getInt64(0));
                auto* safe_b = builder.CreateSelect(is_nz, b.val, builder.getInt64(1));
                auto* rem = builder.CreateURem(a.val, safe_b);
                auto* masked = builder.CreateAnd(rem, builder.getInt64(mask_u64(a.width)));
                stack.push_back({builder.CreateSelect(is_nz, masked, builder.getInt64(0)), a.width, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::AND: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                uint32_t w = std::max(a.width, b.width);
                stack.push_back({builder.CreateAnd(a.val, b.val), w, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::OR: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                uint32_t w = std::max(a.width, b.width);
                stack.push_back({builder.CreateOr(a.val, b.val), w, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::XOR: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                uint32_t w = std::max(a.width, b.width);
                stack.push_back({builder.CreateXor(a.val, b.val), w, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::XNOR: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                uint32_t w = std::max(a.width, b.width);
                auto* xn = builder.CreateNot(builder.CreateXor(a.val, b.val));
                stack.push_back({builder.CreateAnd(xn, builder.getInt64(mask_u64(w))), w, a.is_signed && b.is_signed});
                break;
            }
            case Opcode::SHL: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                auto* in_range = builder.CreateICmpULT(b.val, builder.getInt64(a.width));
                auto* safe_b = builder.CreateSelect(in_range, b.val, builder.getInt64(0));
                auto* shl = builder.CreateShl(a.val, safe_b);
                shl = builder.CreateAnd(shl, builder.getInt64(mask_u64(a.width)));
                stack.push_back({builder.CreateSelect(in_range, shl, builder.getInt64(0)), a.width, a.is_signed});
                break;
            }
            case Opcode::SHR: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                auto* in_range = builder.CreateICmpULT(b.val, builder.getInt64(a.width));
                auto* safe_b = builder.CreateSelect(in_range, b.val, builder.getInt64(0));
                auto* shr = builder.CreateLShr(a.val, safe_b);
                stack.push_back({builder.CreateSelect(in_range, shr, builder.getInt64(0)), a.width, a.is_signed});
                break;
            }
            case Opcode::SRA: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                bool is_s = (inst.arg1 != 0);
                if (!is_s) {
                    auto* in_range = builder.CreateICmpULT(b.val, builder.getInt64(a.width));
                    auto* safe_b = builder.CreateSelect(in_range, b.val, builder.getInt64(0));
                    auto* shr = builder.CreateLShr(a.val, safe_b);
                    stack.push_back({builder.CreateSelect(in_range, shr, builder.getInt64(0)), a.width, false});
                } else {
                    if (a.width == 0) {
                        stack.push_back({builder.getInt64(0), 0, true});
                    } else {
                        uint32_t shift_amt = (a.width < 64) ? (64 - a.width) : 0;
                        llvm::Value* sext = a.val;
                        if (shift_amt > 0) {
                            sext = builder.CreateAShr(builder.CreateShl(a.val, builder.getInt64(shift_amt)), builder.getInt64(shift_amt));
                        }
                        auto* in_range = builder.CreateICmpULT(b.val, builder.getInt64(a.width));
                        auto* sign_bit = builder.CreateAnd(builder.CreateLShr(a.val, builder.getInt64(a.width - 1)), builder.getInt64(1));
                        auto* fill_all = builder.CreateSelect(builder.CreateICmpNE(sign_bit, builder.getInt64(0)), builder.getInt64(mask_u64(a.width)), builder.getInt64(0));
                        auto* safe_b = builder.CreateSelect(in_range, b.val, builder.getInt64(0));
                        auto* normal_sra = builder.CreateAnd(builder.CreateAShr(sext, safe_b), builder.getInt64(mask_u64(a.width)));
                        stack.push_back({builder.CreateSelect(in_range, normal_sra, fill_all), a.width, true});
                    }
                }
                break;
            }
            case Opcode::EQ:
            case Opcode::CASE_EQ: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                stack.push_back({builder.CreateZExt(builder.CreateICmpEQ(a.val, b.val), i64Ty), 1, false});
                break;
            }
            case Opcode::NE:
            case Opcode::CASE_NE: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                stack.push_back({builder.CreateZExt(builder.CreateICmpNE(a.val, b.val), i64Ty), 1, false});
                break;
            }
            case Opcode::LT: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                bool is_s = (inst.arg1 != 0);
                auto* cmp = is_s ? builder.CreateICmpSLT(a.val, b.val) : builder.CreateICmpULT(a.val, b.val);
                stack.push_back({builder.CreateZExt(cmp, i64Ty), 1, false});
                break;
            }
            case Opcode::LE: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                bool is_s = (inst.arg1 != 0);
                auto* cmp = is_s ? builder.CreateICmpSLE(a.val, b.val) : builder.CreateICmpULE(a.val, b.val);
                stack.push_back({builder.CreateZExt(cmp, i64Ty), 1, false});
                break;
            }
            case Opcode::GT: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                bool is_s = (inst.arg1 != 0);
                auto* cmp = is_s ? builder.CreateICmpSGT(a.val, b.val) : builder.CreateICmpUGT(a.val, b.val);
                stack.push_back({builder.CreateZExt(cmp, i64Ty), 1, false});
                break;
            }
            case Opcode::GE: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                bool is_s = (inst.arg1 != 0);
                auto* cmp = is_s ? builder.CreateICmpSGE(a.val, b.val) : builder.CreateICmpUGE(a.val, b.val);
                stack.push_back({builder.CreateZExt(cmp, i64Ty), 1, false});
                break;
            }
            case Opcode::LOGIC_AND: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                auto* ca = builder.CreateICmpNE(a.val, builder.getInt64(0));
                auto* cb = builder.CreateICmpNE(b.val, builder.getInt64(0));
                stack.push_back({builder.CreateZExt(builder.CreateAnd(ca, cb), i64Ty), 1, false});
                break;
            }
            case Opcode::LOGIC_OR: {
                if (stack.size() < 2) return false;
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                auto* ca = builder.CreateICmpNE(a.val, builder.getInt64(0));
                auto* cb = builder.CreateICmpNE(b.val, builder.getInt64(0));
                stack.push_back({builder.CreateZExt(builder.CreateOr(ca, cb), i64Ty), 1, false});
                break;
            }
            case Opcode::NOT: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                stack.push_back({builder.CreateZExt(builder.CreateICmpEQ(a.val, builder.getInt64(0)), i64Ty), 1, false});
                break;
            }
            case Opcode::BIT_NOT: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                auto* not_val = builder.CreateAnd(builder.CreateNot(a.val), builder.getInt64(mask_u64(a.width)));
                stack.push_back({not_val, a.width, a.is_signed});
                break;
            }
            case Opcode::NEG: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                auto* neg_val = builder.CreateAnd(builder.CreateNeg(a.val), builder.getInt64(mask_u64(a.width)));
                stack.push_back({neg_val, a.width, true});
                break;
            }
            case Opcode::RED_AND: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                auto* cmp = (a.width > 0) ? builder.CreateICmpEQ(builder.CreateAnd(a.val, builder.getInt64(mask_u64(a.width))), builder.getInt64(mask_u64(a.width))) : builder.getFalse();
                stack.push_back({builder.CreateZExt(cmp, i64Ty), 1, false});
                break;
            }
            case Opcode::RED_NAND: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                auto* cmp = (a.width > 0) ? builder.CreateICmpNE(builder.CreateAnd(a.val, builder.getInt64(mask_u64(a.width))), builder.getInt64(mask_u64(a.width))) : builder.getTrue();
                stack.push_back({builder.CreateZExt(cmp, i64Ty), 1, false});
                break;
            }
            case Opcode::RED_OR: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                auto* cmp = builder.CreateICmpNE(builder.CreateAnd(a.val, builder.getInt64(mask_u64(a.width))), builder.getInt64(0));
                stack.push_back({builder.CreateZExt(cmp, i64Ty), 1, false});
                break;
            }
            case Opcode::RED_NOR: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                auto* cmp = builder.CreateICmpEQ(builder.CreateAnd(a.val, builder.getInt64(mask_u64(a.width))), builder.getInt64(0));
                stack.push_back({builder.CreateZExt(cmp, i64Ty), 1, false});
                break;
            }
            case Opcode::RED_XOR: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                auto* masked = builder.CreateAnd(a.val, builder.getInt64(mask_u64(a.width)));
                auto* ctpop = builder.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, masked);
                stack.push_back({builder.CreateAnd(ctpop, builder.getInt64(1)), 1, false});
                break;
            }
            case Opcode::RED_XNOR: {
                if (stack.empty()) return false;
                auto a = stack.back(); stack.pop_back();
                auto* masked = builder.CreateAnd(a.val, builder.getInt64(mask_u64(a.width)));
                auto* ctpop = builder.CreateUnaryIntrinsic(llvm::Intrinsic::ctpop, masked);
                stack.push_back({builder.CreateXor(builder.CreateAnd(ctpop, builder.getInt64(1)), builder.getInt64(1)), 1, false});
                break;
            }
            case Opcode::TERNARY_MUX: {
                if (stack.size() < 3) return false;
                auto f = stack.back(); stack.pop_back();
                auto t = stack.back(); stack.pop_back();
                auto c = stack.back(); stack.pop_back();
                uint32_t w = std::max(t.width, f.width);
                auto* cmp = builder.CreateICmpNE(c.val, builder.getInt64(0));
                stack.push_back({builder.CreateSelect(cmp, t.val, f.val), w, t.is_signed && f.is_signed});
                break;
            }
            case Opcode::EXTRACT_SLICE: {
                if (stack.empty()) return false;
                auto v = stack.back(); stack.pop_back();
                uint32_t slice_w = inst.arg0;
                uint32_t lsb = inst.arg1;
                if (lsb >= 64) {
                    stack.push_back({builder.getInt64(0), slice_w, false});
                } else {
                    auto* shr = builder.CreateLShr(v.val, builder.getInt64(lsb));
                    auto* masked = builder.CreateAnd(shr, builder.getInt64(mask_u64(slice_w)));
                    stack.push_back({masked, slice_w, false});
                }
                break;
            }
            case Opcode::CONCAT: {
                uint32_t count = inst.arg0;
                uint32_t tot_w = inst.arg1;
                if (stack.size() < count || tot_w > 64) return false;
                std::vector<LlvmStackElem> elems(count);
                for (int i = (int)count - 1; i >= 0; --i) {
                    elems[i] = stack.back(); stack.pop_back();
                }
                llvm::Value* cat = builder.CreateAnd(elems[0].val, builder.getInt64(mask_u64(elems[0].width)));
                for (size_t i = 1; i < count; ++i) {
                    auto* shifted = builder.CreateShl(cat, builder.getInt64(elems[i].width));
                    auto* masked = builder.CreateAnd(elems[i].val, builder.getInt64(mask_u64(elems[i].width)));
                    cat = builder.CreateOr(shifted, masked);
                }
                stack.push_back({cat, tot_w, false});
                break;
            }
            default:
                return false;
        }
    }

    if (stack.size() != 1) return false;
    auto rhs = stack.back();
    uint64_t tgt_mask = mask_u64(target_w);
    auto* rhs_val = builder.CreateAnd(rhs.val, builder.getInt64(tgt_mask));

    auto* target_ptr = builder.CreateInBoundsGEP(i64Ty, nets_arg, builder.getInt32(target_id));
    if (offset == 0 && target_w == net_w) {
        builder.CreateStore(rhs_val, target_ptr);
    } else {
        uint64_t slice_mask = mask_u64(target_w);
        if (offset > 0) slice_mask <<= offset;
        auto* old_val = builder.CreateLoad(i64Ty, target_ptr);
        auto* cleared = builder.CreateAnd(old_val, builder.getInt64(~slice_mask));
        auto* shifted = builder.CreateShl(rhs_val, builder.getInt64(offset));
        auto* masked_shifted = builder.CreateAnd(shifted, builder.getInt64(slice_mask));
        auto* new_val = builder.CreateOr(cleared, masked_shifted);
        builder.CreateStore(new_val, target_ptr);
    }
    return true;
}

} // namespace
#endif // HAS_LLVM_ORC_JIT

bool LlvmJIT::is_available() {
#if HAS_LLVM_ORC_JIT
    return true;
#else
    return false;
#endif
}

bool LlvmJIT::compile_and_bind(
    const std::vector<ResolvedContinuousAssign>& rcas,
    std::vector<BoundProcess>& procs,
    Engine& engine,
    LlvmDispatchTable& out_table
) {
#if !HAS_LLVM_ORC_JIT
    (void)rcas; (void)procs; (void)engine; (void)out_table;
    return false;
#else
    static bool targets_initialized = false;
    if (!targets_initialized) {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        targets_initialized = true;
    }

    auto jit_exp = llvm::orc::LLJITBuilder().create();
    if (!jit_exp) {
        std::cerr << "[LlvmJIT] Failed to initialize LLJIT builder.\n";
        return false;
    }
    auto jit = std::make_shared<std::unique_ptr<llvm::orc::LLJIT>>(std::move(*jit_exp));

    auto Ctx = std::make_unique<llvm::LLVMContext>();
    auto Mod = std::make_unique<llvm::Module>("cas_orc_module", *Ctx);
    llvm::IRBuilder<> builder(*Ctx);

    auto* voidTy = builder.getVoidTy();
    auto* i64PtrTy = builder.getInt64Ty()->getPointerTo();

    // 1. Emit eval_topological_comb_native(uint64_t* nets)
    auto* combFnTy = llvm::FunctionType::get(voidTy, {i64PtrTy}, false);
    auto* combFn = llvm::Function::Create(combFnTy, llvm::Function::ExternalLinkage, "eval_topological_comb_native", *Mod);
    auto* combEntry = llvm::BasicBlock::Create(*Ctx, "entry", combFn);
    builder.SetInsertPoint(combEntry);

    auto* nets_arg = combFn->getArg(0);
    size_t num_nets = engine.get_netlist().size();
    const auto* netlist = &engine.get_netlist();

    const auto& topo_order = engine.get_topological_order().topological_rca_order;
    size_t compiled_rcas = 0;
    for (uint32_t rca_idx : topo_order) {
        if (rca_idx < rcas.size()) {
            if (emit_rca_llvm(rcas[rca_idx], num_nets, netlist, builder, nets_arg)) {
                compiled_rcas++;
            }
        }
    }
    builder.CreateRetVoid();

    // 2. Compile via LLJIT directly in memory
    auto TSM = llvm::orc::ThreadSafeModule(std::move(Mod), std::move(Ctx));
    if (auto Err = (*jit)->addIRModule(std::move(TSM))) {
        std::cerr << "[LlvmJIT] Failed to add ThreadSafeModule to LLJIT.\n";
        return false;
    }

    auto combSym = (*jit)->lookup("eval_topological_comb_native");
    if (!combSym) {
        std::cerr << "[LlvmJIT] Failed to lookup eval_topological_comb_native symbol.\n";
        return false;
    }

    out_table.topological_comb_fn = combSym->toPtr<LlvmTopologicalCombFn>();
    out_table.jit_engine_handle = jit;
    out_table.is_active = true;

    std::cout << "[LlvmJIT] In-memory LLVM ORC JIT v2 compilation successful: "
              << compiled_rcas << " inlined RCAs compiled directly into executable RAM.\n";

    return true;
#endif
}
