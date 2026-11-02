#include "../../include/kernel/elaborator.hpp"
#include "../../include/tracer/vcd_dumper.hpp"
#include "../../include/frontend/parser.hpp"
#include "../../include/kernel/expr_evaluator.hpp"
#include <stdexcept>
#include <unordered_set>
#include <queue>
#include <functional>

size_t Elaborator::get_or_create_net(const std::string& scoped_name, uint32_t width, bool is_signed, NetKind kind) {
    if (result.net_map.find(scoped_name) != result.net_map.end()) {
        size_t existing_id = result.net_map[scoped_name];
        if (result.netlist[existing_id].kind == NetKind::WIRE && kind != NetKind::WIRE) {
            result.netlist[existing_id].kind = kind;
            if (kind == NetKind::VAR) {
                result.netlist[existing_id].value = LogicVal::make_x(width);
                result.netlist[existing_id].prev_value = LogicVal::make_x(width);
            }
        }
        return existing_id;
    }
    size_t id = result.netlist.size();
    result.netlist.emplace_back(scoped_name, width, is_signed, kind);
    result.netlist.back().vcd_id = VcdDumper::generate_vcd_id(id);
    result.net_map[scoped_name] = id;
    return id;
}

uint64_t Elaborator::eval_const_expr(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return 0;
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return num->aval;
    }
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        std::string cur_scope = scope;
        while (!cur_scope.empty()) {
            std::string scoped_id = cur_scope + "." + id->name;
            if (result.param_map.find(scoped_id) != result.param_map.end()) {
                return result.param_map.at(scoped_id);
            }
            if (result.enum_constant_map.find(scoped_id) != result.enum_constant_map.end()) {
                return result.enum_constant_map.at(scoped_id).aval;
            }
            auto dot = cur_scope.rfind('.');
            if (dot == std::string::npos) cur_scope = "";
            else cur_scope = cur_scope.substr(0, dot);
        }
        if (result.param_map.find(id->name) != result.param_map.end()) {
            return result.param_map.at(id->name);
        }
        if (result.enum_constant_map.find(id->name) != result.enum_constant_map.end()) {
            return result.enum_constant_map.at(id->name).aval;
        }
        return 0;
    }
    if (auto sma = dynamic_cast<StaticMemberAccessNode*>(expr)) {
        std::string full_id = sma->class_type_name + "::" + sma->member_name;
        if (result.param_map.find(full_id) != result.param_map.end()) {
            return result.param_map.at(full_id);
        }
        if (result.enum_constant_map.find(full_id) != result.enum_constant_map.end()) {
            return result.enum_constant_map.at(full_id).aval;
        }
        if (result.param_map.find(sma->member_name) != result.param_map.end()) {
            return result.param_map.at(sma->member_name);
        }
        if (result.enum_constant_map.find(sma->member_name) != result.enum_constant_map.end()) {
            return result.enum_constant_map.at(sma->member_name).aval;
        }
        return 0;
    }
    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        uint64_t res = 0;
        for (auto& elem : cat->elements) {
            uint64_t val = eval_const_expr(elem.get(), scope);
            uint32_t w = 1;
            if (auto num = dynamic_cast<NumberNode*>(elem.get())) {
                w = num->width;
            }
            res = (res << w) | val;
        }
        return res;
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        TypeOpNode* l_top = dynamic_cast<TypeOpNode*>(bin->lhs.get());
        TypeOpNode* r_top = dynamic_cast<TypeOpNode*>(bin->rhs.get());
        if (l_top || r_top) {
            if (!l_top || !r_top) {
                throw std::runtime_error("[CAS-DTINPCIL] Both operands of type comparison must be type() references per IEEE 1800-2023 section 6.23");
            }
            if (bin->op != "==" && bin->op != "!=" && bin->op != "===" && bin->op != "!==") {
                throw std::runtime_error("[CAS-DTINPCIL] Relational operator '" + bin->op + "' is invalid between type() references per IEEE 1800-2023 section 6.23");
            }
            std::string l_type = resolve_type_in_elaboration(l_top, scope);
            std::string r_type = resolve_type_in_elaboration(r_top, scope);
            bool eq = (l_type == r_type);
            if (bin->op == "==" || bin->op == "===") return eq ? 1 : 0;
            return eq ? 0 : 1;
        }

        uint64_t l = eval_const_expr(bin->lhs.get(), scope);
        uint64_t r = eval_const_expr(bin->rhs.get(), scope);
        if (bin->op == "+") return l + r;
        if (bin->op == "-") return (l >= r) ? (l - r) : 0;
        if (bin->op == "*") return l * r;
        if (bin->op == "<<") return l << r;
        if (bin->op == ">>" || bin->op == ">>>") return l >> r;
        if (bin->op == "|") return l | r;
        if (bin->op == "&") return l & r;
        if (bin->op == "^") return l ^ r;
        if (bin->op == "/" && r != 0) return l / r;
        if (bin->op == "%" && r != 0) return l % r;
        if (bin->op == "<") return (l < r) ? 1 : 0;
        if (bin->op == ">") return (l > r) ? 1 : 0;
        if (bin->op == "<=") return (l <= r) ? 1 : 0;
        if (bin->op == ">=") return (l >= r) ? 1 : 0;
        if (bin->op == "==" || bin->op == "===") return (l == r) ? 1 : 0;
        if (bin->op == "!=" || bin->op == "!==") return (l != r) ? 1 : 0;
        if (bin->op == "&&") return (l && r) ? 1 : 0;
        if (bin->op == "||") return (l || r) ? 1 : 0;
    }
    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        uint64_t val = eval_const_expr(un->operand.get(), scope);
        if (un->op == "!" || un->op == "~") return !val;
        if (un->op == "-") return -val;
        return val;
    }
    if (auto ter = dynamic_cast<TernaryOpNode*>(expr)) {
        uint64_t c = eval_const_expr(ter->condition.get(), scope);
        return c ? eval_const_expr(ter->true_expr.get(), scope) : eval_const_expr(ter->false_expr.get(), scope);
    }
    if (auto aqn = dynamic_cast<ArrayQueryNode*>(expr)) {
        std::string var_name = "";
        if (auto id = dynamic_cast<IdentifierNode*>(aqn->array_expr.get())) {
            var_name = id->name;
        } else if (auto ma = dynamic_cast<MemberAccessNode*>(aqn->array_expr.get())) {
            var_name = ma->var_name + "." + ma->member_name;
        }
        std::string scoped_var = scope.empty() ? var_name : (scope + "." + var_name);
        VariableDimMetadata meta;
        bool found = false;
        if (result.var_dimensions.find(scoped_var) != result.var_dimensions.end()) {
            meta = result.var_dimensions.at(scoped_var);
            found = true;
        } else if (result.var_dimensions.find(var_name) != result.var_dimensions.end()) {
            meta = result.var_dimensions.at(var_name);
            found = true;
        }
        if (!found || meta.dimensions.empty()) {
            throw std::runtime_error("[CAS-DTINPCIL] Non-array or non-vector variable '" + var_name + "' passed to array querying function per IEEE 1800-2023 section 7.11");
        }
        if (aqn->query_type == ArrayQueryType::DIMENSIONS) {
            return meta.dimensions.size();
        }
        if (aqn->query_type == ArrayQueryType::UNPACKED_DIMENSIONS) {
            return meta.unpacked_dims;
        }
        int64_t dim = 1;
        if (aqn->dim_expr) {
            dim = (int64_t)eval_const_expr(aqn->dim_expr.get(), scope);
        }
        if (dim < 1 || (size_t)dim > meta.dimensions.size()) {
            throw std::runtime_error("[CAS-DTINPCIL] Invalid dimension index " + std::to_string(dim) + " for array querying function per IEEE 1800-2023 section 7.11");
        }
        const auto& d = meta.dimensions[dim - 1];
        switch (aqn->query_type) {
            case ArrayQueryType::SIZE: return (uint64_t)d.size();
            case ArrayQueryType::LEFT: return (uint64_t)d.left;
            case ArrayQueryType::RIGHT: return (uint64_t)d.right;
            case ArrayQueryType::LOW: return (uint64_t)d.low();
            case ArrayQueryType::HIGH: return (uint64_t)d.high();
            case ArrayQueryType::INCREMENT: return (uint64_t)(int64_t)d.increment();
            default: break;
        }
    }
    return 0;
}

std::string Elaborator::resolve_type_in_elaboration(const TypeOpNode* ton, const std::string& scope) {
    if (!ton) return "";
    if (!ton->type_name_arg.empty()) return ton->type_name_arg;
    if (!ton->target_expr) return "";

    auto format_udt = [&](const std::string& type_name) -> std::string {
        if (type_name.empty()) return "";
        std::string unqualified = type_name;
        std::string explicit_pkg = "";
        auto cpos = type_name.rfind("::");
        if (cpos != std::string::npos) {
            explicit_pkg = type_name.substr(0, cpos);
            unqualified = type_name.substr(cpos + 2);
        }
        for (const auto& kv : Parser::global_packages) {
            const auto& pkg = kv.second;
            if (!explicit_pkg.empty() && pkg->name != explicit_pkg) continue;
            if (pkg->enum_typedefs.count(unqualified)) return "enum " + pkg->name + "::" + unqualified;
            if (pkg->struct_typedefs.count(unqualified)) return "struct " + pkg->name + "::" + unqualified;
            if (pkg->class_map.count(unqualified)) return "class " + pkg->name + "::" + unqualified;
        }
        if (result.enum_types.count(type_name) || result.enum_types.count(unqualified)) {
            return "enum " + (explicit_pkg.empty() ? unqualified : type_name);
        }
        if (result.struct_types.count(type_name) || result.struct_types.count(unqualified)) {
            return "struct " + (explicit_pkg.empty() ? unqualified : type_name);
        }
        return "";
    };

    if (auto id = dynamic_cast<IdentifierNode*>(ton->target_expr.get())) {
        const std::string& name = id->name;
        std::string udt_res = format_udt(name);
        if (!udt_res.empty()) return udt_res;

        if (name == "int" || name == "byte" || name == "shortint" || name == "longint" ||
            name == "integer" || name == "time" || name == "string" || name == "real" ||
            name == "shortreal" || name == "realtime" || name == "chandle" || name == "void") return name;
        if (name == "logic" || name == "bit" || name == "reg") return name;

        std::string scoped_net = scope.empty() ? name : (scope + "." + name);
        size_t nid = (size_t)-1;
        if (result.net_map.count(scoped_net)) nid = result.net_map.at(scoped_net);
        else if (result.net_map.count(name)) nid = result.net_map.at(name);

        if (nid != (size_t)-1) {
            const Net& net = result.netlist[nid];
            if (result.var_to_enum_type.count(scoped_net)) {
                std::string fres = format_udt(result.var_to_enum_type.at(scoped_net));
                if (!fres.empty()) return fres;
            } else if (result.var_to_enum_type.count(name)) {
                std::string fres = format_udt(result.var_to_enum_type.at(name));
                if (!fres.empty()) return fres;
            }
            if (result.var_to_struct_type.count(scoped_net)) {
                std::string fres = format_udt(result.var_to_struct_type.at(scoped_net));
                if (!fres.empty()) return fres;
            } else if (result.var_to_struct_type.count(name)) {
                std::string fres = format_udt(result.var_to_struct_type.at(name));
                if (!fres.empty()) return fres;
            }
            if (!net.type_name.empty()) {
                std::string fres = format_udt(net.type_name);
                if (!fres.empty()) return fres;
                if (net.type_name == "int" || net.type_name == "byte" || net.type_name == "string" || net.type_name == "real") return net.type_name;
            }
            if (result.var_dimensions.count(scoped_net)) {
                auto& vdm = result.var_dimensions.at(scoped_net);
                if (!vdm.dimensions.empty()) {
                    auto& d = vdm.dimensions[0];
                    std::string base = (net.type_name == "bit") ? "bit" : "logic";
                    return base + "[" + std::to_string(d.left) + ":" + std::to_string(d.right) + "]";
                }
            } else if (result.var_dimensions.count(name)) {
                auto& vdm = result.var_dimensions.at(name);
                if (!vdm.dimensions.empty()) {
                    auto& d = vdm.dimensions[0];
                    std::string base = (net.type_name == "bit") ? "bit" : "logic";
                    return base + "[" + std::to_string(d.left) + ":" + std::to_string(d.right) + "]";
                }
            }
            if (net.type_name == "bit") return (net.width > 1) ? ("bit[" + std::to_string(net.width - 1) + ":0]") : "bit";
            if (net.width > 1) return "logic[" + std::to_string(net.width - 1) + ":0]";
            return "logic";
        }
    }

    if (auto sma = dynamic_cast<StaticMemberAccessNode*>(ton->target_expr.get())) {
        std::string full = sma->class_type_name + "::" + sma->member_name;
        std::string res = format_udt(full);
        if (!res.empty()) return res;
        return full;
    }

    return "";
}

NetSliceRef Elaborator::resolve_slice_ref(ExpressionNode* expr, const std::string& scope) {
    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        std::string full_name = scope.empty() ? ident->name : (scope + "." + ident->name);
        size_t id = result.net_map.at(full_name);
        return {id, 0, result.netlist[id].width};
    }
    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        std::string full_name = scope.empty() ? idx->name : (scope + "." + idx->name);
        size_t id = result.net_map.at(full_name);
        if (!idx->is_part_select) {
            uint64_t off = eval_const_expr(idx->msb_expr.get(), scope);
            return {id, (uint32_t)off, 1};
        } else {
            uint64_t msb = eval_const_expr(idx->msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(idx->lsb_expr.get(), scope);
            uint32_t w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
            return {id, (uint32_t)lsb, w};
        }
    }
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        static size_t const_counter = 0;
        std::string const_net_name = scope + ".__const_port_" + std::to_string(const_counter++);
        size_t id = get_or_create_net(const_net_name, num->width, false);
        LogicVal val(num->width, num->aval, num->bval);
        result.netlist[id].set(val);
        return {id, 0, num->width};
    }

    static size_t expr_port_counter = 0;
    std::string temp_net_name = scope + ".__expr_port_" + std::to_string(expr_port_counter++);
    
    uint32_t synth_width = 8;
    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        synth_width = 0;
        for (auto& el : cat->elements) {
            if (auto n = dynamic_cast<NumberNode*>(el.get())) {
                synth_width += n->width;
            } else if (auto id = dynamic_cast<IdentifierNode*>(el.get())) {
                std::string full = scope.empty() ? id->name : (scope + "." + id->name);
                if (result.net_map.find(full) != result.net_map.end()) {
                    synth_width += result.netlist[result.net_map[full]].width;
                } else {
                    synth_width += 1;
                }
            } else {
                synth_width += 1;
            }
        }
        if (synth_width == 0) synth_width = 32;
    }

    size_t id = get_or_create_net(temp_net_name, synth_width, false);
    result.continuous_assigns.push_back({scope, {id, 0, synth_width}, expr});
    return {id, 0, synth_width};
}

static std::unique_ptr<ExpressionNode> substitute_genvar_in_expr(ExpressionNode* expr, const std::string& gname, int64_t val) {
    if (!expr) return nullptr;
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        if (id->name == gname) {
            return std::make_unique<NumberNode>((uint64_t)val, 32, false);
        }
        return std::make_unique<IdentifierNode>(id->name);
    }
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return num->clone();
    }
    if (auto ton = dynamic_cast<TypeOpNode*>(expr)) {
        return ton->clone();
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        return std::make_unique<BinaryOpNode>(bin->op,
            substitute_genvar_in_expr(bin->lhs.get(), gname, val),
            substitute_genvar_in_expr(bin->rhs.get(), gname, val));
    }
    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        return std::make_unique<UnaryOpNode>(un->op,
            substitute_genvar_in_expr(un->operand.get(), gname, val));
    }
    if (auto ter = dynamic_cast<TernaryOpNode*>(expr)) {
        return std::make_unique<TernaryOpNode>(
            substitute_genvar_in_expr(ter->condition.get(), gname, val),
            substitute_genvar_in_expr(ter->true_expr.get(), gname, val),
            substitute_genvar_in_expr(ter->false_expr.get(), gname, val));
    }
    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        auto node = std::make_unique<IndexSelectNode>(idx->name,
            substitute_genvar_in_expr(idx->msb_expr.get(), gname, val));
        if (idx->lsb_expr) node->lsb_expr = substitute_genvar_in_expr(idx->lsb_expr.get(), gname, val);
        node->is_part_select = idx->is_part_select;
        node->has_bit_select = idx->has_bit_select;
        if (idx->bit_msb_expr) node->bit_msb_expr = substitute_genvar_in_expr(idx->bit_msb_expr.get(), gname, val);
        if (idx->bit_lsb_expr) node->bit_lsb_expr = substitute_genvar_in_expr(idx->bit_lsb_expr.get(), gname, val);
        return node;
    }
    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> els;
        for (auto& e : cat->elements) els.push_back(substitute_genvar_in_expr(e.get(), gname, val));
        return std::make_unique<ConcatNode>(std::move(els));
    }
    if (auto rep = dynamic_cast<ReplicateNode*>(expr)) {
        return std::make_unique<ReplicateNode>(
            substitute_genvar_in_expr(rep->count_expr.get(), gname, val),
            substitute_genvar_in_expr(rep->target_expr.get(), gname, val));
    }
    if (auto fn = dynamic_cast<FunctionCallNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> args;
        for (auto& a : fn->args) args.push_back(substitute_genvar_in_expr(a.get(), gname, val));
        return std::make_unique<FunctionCallNode>(fn->func_name, std::move(args), fn->arg_names);
    }
    if (auto cst = dynamic_cast<CastNode*>(expr)) {
        return std::make_unique<CastNode>(cst->target_type, substitute_genvar_in_expr(cst->expr.get(), gname, val));
    }
    if (auto scst = dynamic_cast<StaticCastExprNode*>(expr)) {
        return std::make_unique<StaticCastExprNode>(scst->kind, scst->target_width, scst->target_type_name, substitute_genvar_in_expr(scst->expr.get(), gname, val));
    }
    if (auto ma = dynamic_cast<MemberAccessNode*>(expr)) {
        return std::make_unique<MemberAccessNode>(ma->var_name, ma->member_name);
    }
    return expr->clone();
}

static std::unique_ptr<StatementNode> substitute_genvar_in_stmt(StatementNode* stmt, const std::string& gname, int64_t val) {
    if (!stmt) return nullptr;
    if (auto mem = dynamic_cast<MemAssignNode*>(stmt)) {
        auto node = std::make_unique<MemAssignNode>(mem->mem_name,
            substitute_genvar_in_expr(mem->addr_expr.get(), gname, val),
            substitute_genvar_in_expr(mem->rhs.get(), gname, val),
            mem->is_non_blocking, mem->intra_delay);
        node->has_intra_event = mem->has_intra_event;
        node->intra_edge = mem->intra_edge;
        node->intra_signal = mem->intra_signal;
        return node;
    }
    if (auto nba = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
        auto node = std::make_unique<NonBlockingAssignNode>(nba->target,
            substitute_genvar_in_expr(nba->rhs.get(), gname, val), nba->intra_delay);
        node->has_intra_event = nba->has_intra_event;
        node->intra_edge = nba->intra_edge;
        node->intra_signal = nba->intra_signal;
        return node;
    }
    if (auto ba = dynamic_cast<BlockingAssignNode*>(stmt)) {
        auto node = std::make_unique<BlockingAssignNode>(ba->lhs_targets,
            substitute_genvar_in_expr(ba->rhs.get(), gname, val), ba->intra_delay);
        node->bit_idx = substitute_genvar_in_expr(ba->bit_idx.get(), gname, val);
        node->has_intra_event = ba->has_intra_event;
        node->intra_edge = ba->intra_edge;
        node->intra_signal = ba->intra_signal;
        return node;
    }
    if (auto ifs = dynamic_cast<IfStatementNode*>(stmt)) {
        auto node = std::make_unique<IfStatementNode>(
            substitute_genvar_in_expr(ifs->condition.get(), gname, val));
        node->modifier = ifs->modifier;
        node->src_file = ifs->src_file;
        node->src_line = ifs->src_line;
        for (auto& s : ifs->then_branch) node->then_branch.push_back(substitute_genvar_in_stmt(s.get(), gname, val));
        for (auto& s : ifs->else_branch) node->else_branch.push_back(substitute_genvar_in_stmt(s.get(), gname, val));
        return node;
    }
    if (auto cs = dynamic_cast<CaseStatementNode*>(stmt)) {
        auto node = std::make_unique<CaseStatementNode>(
            substitute_genvar_in_expr(cs->selector.get(), gname, val), cs->case_type);
        node->modifier = cs->modifier;
        node->src_file = cs->src_file;
        node->src_line = cs->src_line;
        for (const auto& item : cs->items) {
            CaseItem ci;
            ci.is_default = item.is_default;
            if (item.match_expr) ci.match_expr = substitute_genvar_in_expr(item.match_expr.get(), gname, val);
            for (const auto& me : item.match_exprs) {
                ci.match_exprs.push_back(substitute_genvar_in_expr(me.get(), gname, val));
            }
            for (const auto& s : item.statements) {
                ci.statements.push_back(substitute_genvar_in_stmt(s.get(), gname, val));
            }
            node->items.push_back(std::move(ci));
        }
        return node;
    }
    if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
        std::vector<std::unique_ptr<StatementNode>> stmts;
        for (auto& s : blk->statements) stmts.push_back(substitute_genvar_in_stmt(s.get(), gname, val));
        return std::make_unique<BlockStatementNode>(std::move(stmts));
    }
    return stmt->clone();
}

void Elaborator::unroll_generate_block(GenerateBlock* blk,
                                       const std::string& current_scope,
                                       const std::string& parent_scope,
                                       const std::string& gname,
                                       int64_t gval,
                                       bool has_genvar,
                                       std::ostream* debug_out) {
    if (!blk) return;

    for (const auto& sig : blk->signals) {
        PortNode p = sig.clone();
        if (has_genvar) {
            if (p.msb_expr) p.msb_expr = substitute_genvar_in_expr(p.msb_expr.get(), gname, gval);
            if (p.lsb_expr) p.lsb_expr = substitute_genvar_in_expr(p.lsb_expr.get(), gname, gval);
        }
        uint32_t w = 1;
        if (p.msb_expr && p.lsb_expr) {
            uint64_t msb = eval_const_expr(p.msb_expr.get(), current_scope);
            uint64_t lsb = eval_const_expr(p.lsb_expr.get(), current_scope);
            w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
        }
        std::string full_name = current_scope + "." + p.name;
        size_t nid = get_or_create_net(full_name, w, p.is_signed, p.net_kind);
        result.netlist[nid].value = LogicVal::make_x(w);
        result.netlist[nid].prev_value = LogicVal::make_x(w);
        if (debug_out) {
            *debug_out << "     * Generated Net allocated: " << full_name << " [" << w << "-bit]\n";
        }
    }

    for (const auto& ca : blk->continuous_assigns) {
        auto ca_copy = ca->clone();
        if (has_genvar) {
            if (ca_copy->target_index_expr) {
                ca_copy->target_index_expr = substitute_genvar_in_expr(ca_copy->target_index_expr.get(), gname, gval);
            }
            if (ca_copy->rhs) {
                ca_copy->rhs = substitute_genvar_in_expr(ca_copy->rhs.get(), gname, gval);
            }
        }
        if (ca_copy->target_index_expr) {
            result.memory_continuous_assigns.push_back({current_scope, ca_copy->target_name, std::move(ca_copy->target_index_expr), std::move(ca_copy->rhs)});
        } else {
            std::string full_target = current_scope + "." + ca_copy->target_name;
            size_t tid = (size_t)-1;
            if (result.net_map.find(full_target) != result.net_map.end()) {
                tid = result.net_map.at(full_target);
            } else if (result.net_map.find(parent_scope + "." + ca_copy->target_name) != result.net_map.end()) {
                tid = result.net_map.at(parent_scope + "." + ca_copy->target_name);
            }
            if (tid != (size_t)-1) {
                auto rhs_ptr = ca_copy->rhs.get();
                result.generated_continuous_assigns.push_back(std::move(ca_copy));
                result.continuous_assigns.push_back({current_scope, {tid, 0, result.netlist[tid].width}, rhs_ptr});
            }
        }
    }

    for (const auto& proc : blk->processes) {
        auto proc_copy = proc->clone();
        if (has_genvar) {
            for (size_t i = 0; i < proc_copy->body.size(); ++i) {
                proc_copy->body[i] = substitute_genvar_in_stmt(proc_copy->body[i].get(), gname, gval);
            }
        }
        BoundProcess bp(current_scope, proc_copy.get(), 0, false);
        bp.pid = result.processes.size();
        result.generated_processes.push_back(std::move(proc_copy));
        bp.proc = result.generated_processes.back().get();
        result.processes.push_back(std::move(bp));
    }

    for (const auto& gen_ast : blk->nested_generates) {
        unroll_generate_node(gen_ast.get(), current_scope, gname, gval, has_genvar, debug_out);
    }
}

void Elaborator::unroll_generate_node(ASTNode* node,
                                      const std::string& scope,
                                      const std::string& parent_gname,
                                      int64_t parent_gval,
                                      bool has_parent_gvar,
                                      std::ostream* debug_out) {
    if (auto loop = dynamic_cast<LoopGenerateNode*>(node)) {
        auto init_expr = has_parent_gvar ? substitute_genvar_in_expr(loop->init_expr.get(), parent_gname, parent_gval) : loop->init_expr->clone();
        int64_t val = (int64_t)eval_const_expr(init_expr.get(), scope);
        std::string gname = loop->genvar_name;

        while (true) {
            auto cond_e = substitute_genvar_in_expr(loop->cond_expr.get(), gname, val);
            if (has_parent_gvar) cond_e = substitute_genvar_in_expr(cond_e.get(), parent_gname, parent_gval);
            uint64_t cval = 0;
            try {
                cval = eval_const_expr(cond_e.get(), scope);
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("[CAS-DTINPCIL] Dynamic type in non-procedural context: generate condition cannot be evaluated as elaboration-time constant per IEEE 1800-2023 section 27.5: ") + e.what());
            }
            if (cval == 0) break;

            std::string iter_label = loop->block_label.empty() ? ("genblk" + std::to_string(val)) : (loop->block_label + "[" + std::to_string(val) + "]");
            std::string iter_scope = scope + "." + iter_label;
            result.param_map[iter_scope + "." + gname] = (uint64_t)val;

            unroll_generate_block(loop->body.get(), iter_scope, scope, gname, val, true, debug_out);

            // Step
            if (loop->step_op == "++") {
                val++;
            } else if (loop->step_op == "--") {
                val--;
            } else if (loop->step_op == "+=") {
                auto se = substitute_genvar_in_expr(loop->step_expr.get(), gname, val);
                if (has_parent_gvar) se = substitute_genvar_in_expr(se.get(), parent_gname, parent_gval);
                val += (int64_t)eval_const_expr(se.get(), scope);
            } else if (loop->step_op == "-=") {
                auto se = substitute_genvar_in_expr(loop->step_expr.get(), gname, val);
                if (has_parent_gvar) se = substitute_genvar_in_expr(se.get(), parent_gname, parent_gval);
                val -= (int64_t)eval_const_expr(se.get(), scope);
            } else if (loop->step_op == "=") {
                auto se = substitute_genvar_in_expr(loop->step_expr.get(), gname, val);
                if (has_parent_gvar) se = substitute_genvar_in_expr(se.get(), parent_gname, parent_gval);
                val = (int64_t)eval_const_expr(se.get(), scope);
            }
        }
    } else if (auto if_node = dynamic_cast<IfGenerateNode*>(node)) {
        auto cond_e = has_parent_gvar ? substitute_genvar_in_expr(if_node->cond_expr.get(), parent_gname, parent_gval) : if_node->cond_expr->clone();
        uint64_t cval = 0;
        try {
            cval = eval_const_expr(cond_e.get(), scope);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("[CAS-DTINPCIL] Dynamic type in non-procedural context: generate condition cannot be evaluated as elaboration-time constant per IEEE 1800-2023 section 27.5: ") + e.what());
        }
        if (cval != 0) {
            std::string sub_scope = if_node->then_label.empty() ? scope : (scope + "." + if_node->then_label);
            unroll_generate_block(if_node->then_block.get(), sub_scope, scope, parent_gname, parent_gval, has_parent_gvar, debug_out);
        } else if (if_node->has_else) {
            std::string sub_scope = if_node->else_label.empty() ? scope : (scope + "." + if_node->else_label);
            unroll_generate_block(if_node->else_block.get(), sub_scope, scope, parent_gname, parent_gval, has_parent_gvar, debug_out);
        }
    } else if (auto case_node = dynamic_cast<CaseGenerateNode*>(node)) {
        auto sel_e = has_parent_gvar ? substitute_genvar_in_expr(case_node->select_expr.get(), parent_gname, parent_gval) : case_node->select_expr->clone();
        uint64_t sel_val = 0;
        try {
            sel_val = eval_const_expr(sel_e.get(), scope);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("[CAS-DTINPCIL] Dynamic type in non-procedural context: generate condition cannot be evaluated as elaboration-time constant per IEEE 1800-2023 section 27.5: ") + e.what());
        }
        bool matched = false;
        for (const auto& item : case_node->items) {
            if (!item.is_default) {
                for (const auto& me : item.match_exprs) {
                    auto me_sub = has_parent_gvar ? substitute_genvar_in_expr(me.get(), parent_gname, parent_gval) : me->clone();
                    if (eval_const_expr(me_sub.get(), scope) == sel_val) {
                        matched = true;
                        std::string sub_scope = item.label.empty() ? scope : (scope + "." + item.label);
                        unroll_generate_block(item.block.get(), sub_scope, scope, parent_gname, parent_gval, has_parent_gvar, debug_out);
                        break;
                    }
                }
                if (matched) break;
            }
        }
        if (!matched) {
            for (const auto& item : case_node->items) {
                if (item.is_default) {
                    std::string sub_scope = item.label.empty() ? scope : (scope + "." + item.label);
                    unroll_generate_block(item.block.get(), sub_scope, scope, parent_gname, parent_gval, has_parent_gvar, debug_out);
                    break;
                }
            }
        }
    }
}

void Elaborator::elaborate_module(ModuleNode* mod, const std::string& scope,
                                 const std::unordered_map<std::string, ModuleNode*>& module_defs,
                                 const std::vector<ParamOverride>* overrides,
                                 const std::string& parent_scope,
                                 std::ostream* debug_out) {
    if (debug_out) {
        *debug_out << "  -> Scoping module '" << mod->name << "' as [" << scope << "]\n";
    }

    // Bind wildcard package imports
    for (const auto& pkg_name : mod->wildcard_imports) {
        auto it = Parser::global_packages.find(pkg_name);
        if (it != Parser::global_packages.end()) {
            const auto& pkg = it->second;
            for (const auto& p : pkg->param_constants) {
                result.param_map[scope + "." + p.first] = p.second;
                if (parent_scope.empty()) result.param_map[p.first] = p.second;
            }
            for (const auto& m : pkg->enum_constant_map) {
                result.enum_constant_map[scope + "." + m.first] = m.second;
                if (parent_scope.empty()) result.enum_constant_map[m.first] = m.second;
            }
            for (const auto& fn : pkg->functions) {
                result.function_map[scope + "." + fn->name] = fn.get();
                if (parent_scope.empty()) result.function_map[fn->name] = fn.get();
            }
            for (const auto& t : pkg->tasks) {
                result.task_map[scope + "." + t->name] = t.get();
                if (parent_scope.empty()) result.task_map[t->name] = t.get();
            }
        }
    }

    // Bind explicit package imports
    for (const auto& kv : mod->explicit_imports) {
        const std::string& item_name = kv.first;
        const std::string& pkg_name = kv.second.first;
        auto it = Parser::global_packages.find(pkg_name);
        if (it != Parser::global_packages.end()) {
            const auto& pkg = it->second;
            if (pkg->param_constants.count(item_name)) {
                result.param_map[scope + "." + item_name] = pkg->param_constants.at(item_name);
                if (parent_scope.empty()) result.param_map[item_name] = pkg->param_constants.at(item_name);
            }
            if (pkg->enum_constant_map.count(item_name)) {
                result.enum_constant_map[scope + "." + item_name] = pkg->enum_constant_map.at(item_name);
                if (parent_scope.empty()) result.enum_constant_map[item_name] = pkg->enum_constant_map.at(item_name);
            }
            if (pkg->function_map.count(item_name)) {
                result.function_map[scope + "." + item_name] = pkg->function_map.at(item_name);
                if (parent_scope.empty()) result.function_map[item_name] = pkg->function_map.at(item_name);
            }
            if (pkg->task_map.count(item_name)) {
                result.task_map[scope + "." + item_name] = pkg->task_map.at(item_name);
                if (parent_scope.empty()) result.task_map[item_name] = pkg->task_map.at(item_name);
            }
        }
    }

    for (const auto& td : mod->typedefs) {
        if (td && td->enum_type) {
            EnumType et;
            et.name = td->type_name;
            et.width = td->enum_type->base_width;
            et.is_signed = td->enum_type->is_signed;
            for (const auto& m : td->enum_type->members) {
                et.members.push_back({m.first, m.second});
                result.enum_constant_map[m.first] = m.second;
                result.enum_constant_map[scope + "." + m.first] = m.second;
            }
            result.enum_types[td->type_name] = et;
            result.enum_types[scope + "." + td->type_name] = et;
        }
        if (td && td->struct_type) {
            PackedStructType st;
            st.name = td->type_name;
            st.is_signed = td->struct_type->is_signed;
            st.total_width = td->struct_type->total_width;

            size_t n = td->struct_type->members.size();
            std::vector<uint32_t> offsets(n, 0);
            uint32_t running_offset = 0;
            for (int i = (int)n - 1; i >= 0; --i) {
                offsets[i] = running_offset;
                running_offset += td->struct_type->members[i].width;
            }
            if (st.total_width == 0) {
                st.total_width = running_offset;
            }

            for (size_t i = 0; i < n; ++i) {
                const auto& mn = td->struct_type->members[i];
                StructMember sm;
                sm.name = mn.name;
                sm.width = mn.width;
                sm.offset = offsets[i];
                sm.is_signed = mn.is_signed;
                sm.type_name = mn.type_name;
                st.members.push_back(sm);
            }
            result.struct_types[td->type_name] = st;
            result.struct_types[scope + "." + td->type_name] = st;
        }
        if (!td->base_type.empty()) {
            if (td->is_unpacked_array) {
                UnpackedArrayTypeInfo uinfo;
                uinfo.name = td->type_name;
                uinfo.elem_type = td->elem_type;
                uinfo.elem_width = td->elem_width;
                uinfo.elem_signed = td->elem_signed;
                uinfo.array_size = td->array_size;
                uinfo.start_idx = td->arr_start_idx;
                uinfo.end_idx = td->arr_end_idx;
                result.unpacked_array_types[td->type_name] = uinfo;
                result.unpacked_array_types[scope + "." + td->type_name] = uinfo;
            } else {
                SimpleTypeInfo sinfo;
                sinfo.name = td->type_name;
                sinfo.base_type = td->base_type;
                sinfo.width = td->width;
                sinfo.is_signed = td->is_signed;
                result.simple_types[td->type_name] = sinfo;
                result.simple_types[scope + "." + td->type_name] = sinfo;
            }
        }
    }

    for (size_t i = 0; i < mod->parameters.size(); ++i) {
        const auto& p = mod->parameters[i];
        uint64_t val = 0;
        bool overridden = false;

        // 1. Check defparam overrides FIRST (defparam takes precedence over instantiation defaults/bindings)
        auto dp_inst_it = defparam_overrides.find(scope);
        if (dp_inst_it != defparam_overrides.end()) {
            auto dp_it = dp_inst_it->second.find(p.name);
            if (dp_it != dp_inst_it->second.end()) {
                val = dp_it->second;
                overridden = true;
            }
        }

        // 2. Check instantiation-site #() overrides if not overridden by defparam
        if (!overridden && overrides && !p.is_localparam) {
            for (size_t oi = 0; oi < overrides->size(); ++oi) {
                const auto& ov = (*overrides)[oi];
                if (!ov.param_name.empty() && ov.param_name == p.name) {
                    val = eval_const_expr(ov.expr.get(), parent_scope);
                    overridden = true;
                    break;
                } else if (ov.param_name.empty() && oi == i) {
                    val = eval_const_expr(ov.expr.get(), parent_scope);
                    overridden = true;
                    break;
                }
            }
        }

        if (overridden) {
            if (debug_out) *debug_out << "     * Parameter override: " << p.name << " = " << val << "\n";
        } else {
            val = eval_const_expr(p.default_expr.get(), scope);
            if (debug_out) *debug_out << "     * Parameter default: " << p.name << " = " << val << "\n";
        }
        result.param_map[scope + "." + p.name] = val;
    }

    for (auto& p : mod->ports) {
        uint32_t w = 1;
        if (p.msb_expr && p.lsb_expr) {
            uint64_t msb = eval_const_expr(p.msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(p.lsb_expr.get(), scope);
            w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
        }
        if (p.is_array) {
            std::vector<ArrayDimInfo> all_dims;
            uint32_t total_depth = 1;
            if (!p.unpacked_dimensions.empty()) {
                for (const auto& udim : p.unpacked_dimensions) {
                    int64_t b1 = (int64_t)eval_const_expr(udim.left_expr.get(), scope);
                    int64_t b2 = udim.right_expr ? (int64_t)eval_const_expr(udim.right_expr.get(), scope) : 0;
                    if (udim.is_single) {
                        b2 = b1 > 0 ? (b1 - 1) : 0;
                        b1 = 0;
                    }
                    ArrayDimInfo info;
                    info.left = b1;
                    info.right = b2;
                    info.is_unpacked = true;
                    all_dims.push_back(info);
                    total_depth *= (uint32_t)info.size();
                }
            } else {
                int64_t b1 = 0;
                int64_t b2 = 0;
                if (p.bound1_expr && p.bound2_expr) {
                    b1 = (int64_t)eval_const_expr(p.bound1_expr.get(), scope);
                    b2 = (int64_t)eval_const_expr(p.bound2_expr.get(), scope);
                } else if (p.bound1_expr) {
                    uint32_t d = (uint32_t)eval_const_expr(p.bound1_expr.get(), scope);
                    b1 = 0;
                    b2 = d > 0 ? (int64_t)(d - 1) : 0;
                }
                ArrayDimInfo info;
                info.left = b1;
                info.right = b2;
                info.is_unpacked = true;
                all_dims.push_back(info);
                total_depth = (uint32_t)info.size();
            }
            uint32_t unpacked_cnt = (uint32_t)all_dims.size();

            // Packed dimensions
            if (!p.packed_dimensions.empty()) {
                for (const auto& pdim : p.packed_dimensions) {
                    int64_t b1 = (int64_t)eval_const_expr(pdim.left_expr.get(), scope);
                    int64_t b2 = pdim.right_expr ? (int64_t)eval_const_expr(pdim.right_expr.get(), scope) : 0;
                    ArrayDimInfo info;
                    info.left = b1;
                    info.right = b2;
                    info.is_unpacked = false;
                    all_dims.push_back(info);
                }
            } else if (p.msb_expr) {
                int64_t b1 = (int64_t)eval_const_expr(p.msb_expr.get(), scope);
                int64_t b2 = p.lsb_expr ? (int64_t)eval_const_expr(p.lsb_expr.get(), scope) : 0;
                ArrayDimInfo info;
                info.left = b1;
                info.right = b2;
                info.is_unpacked = false;
                all_dims.push_back(info);
            } else if (p.type_name == "int" || p.type_name == "integer") {
                all_dims.push_back({31, 0, false, false, false});
            } else if (p.type_name == "byte") {
                all_dims.push_back({7, 0, false, false, false});
            } else if (p.type_name == "shortint") {
                all_dims.push_back({15, 0, false, false, false});
            } else if (p.type_name == "longint" || p.type_name == "time") {
                all_dims.push_back({63, 0, false, false, false});
            }

            std::string mem_scope = scope + "." + p.name;
            int64_t b1_first = all_dims.empty() ? 0 : all_dims[0].left;
            int64_t b2_first = all_dims.empty() ? 0 : all_dims[0].right;
            MemoryArray marr(mem_scope, w, total_depth, b1_first, b2_first);
            marr.dimensions = all_dims;
            marr.unpacked_dims_count = unpacked_cnt;
            result.memory_map[mem_scope] = marr;

            VariableDimMetadata vdm;
            vdm.name = mem_scope;
            vdm.dimensions = all_dims;
            vdm.unpacked_dims = unpacked_cnt;
            vdm.is_vector_or_array = true;
            result.var_dimensions[mem_scope] = vdm;
            result.var_dimensions[p.name] = vdm;

            if (debug_out) {
                *debug_out << "     * Port Memory allocated: " << mem_scope 
                           << " [" << w << "-bit x " << total_depth << " words]\n";
            }
            continue;
        }
        size_t pid = get_or_create_net(scope + "." + p.name, w, p.is_signed, p.net_kind);
        if (p.is_chandle || p.type_name == "chandle") {
            result.netlist[pid].is_chandle = true;
            result.netlist[pid].type_name = "chandle";
            result.netlist[pid].value = LogicVal(64, 0, 0);
            result.netlist[pid].prev_value = LogicVal(64, 0, 0);
            result.chandle_names.insert(scope + "." + p.name);
            result.chandle_names.insert(p.name);
        }
        if (p.is_semaphore || p.type_name == "semaphore") {
            result.netlist[pid].is_semaphore = true;
            result.netlist[pid].type_name = "semaphore";
            result.netlist[pid].value = LogicVal(64, 0, 0);
            result.netlist[pid].prev_value = LogicVal(64, 0, 0);
            result.semaphore_handle_names.insert(scope + "." + p.name);
            result.semaphore_handle_names.insert(p.name);
        }
        if (p.is_mailbox || p.type_name == "mailbox") {
            result.netlist[pid].is_mailbox = true;
            result.netlist[pid].type_name = "mailbox";
            result.netlist[pid].value = LogicVal(64, 0, 0);
            result.netlist[pid].prev_value = LogicVal(64, 0, 0);
            result.mailbox_handle_names.insert(scope + "." + p.name);
            result.mailbox_handle_names.insert(p.name);
        }
        if (p.is_open_array) {
            result.open_array_names.insert(scope + "." + p.name);
            result.open_array_names.insert(p.name);
        }
        if (p.type_name == "string") {
            result.netlist[pid].type_name = "string";
            result.netlist[pid].value = LogicVal::make_string("");
            result.netlist[pid].prev_value = LogicVal::make_string("");
            result.string_net_names.insert(scope + "." + p.name);
            result.string_net_names.insert(p.name);
        }
        if (!p.type_name.empty()) {
            result.netlist[pid].type_name = p.type_name;
            result.var_to_enum_type[scope + "." + p.name] = p.type_name;
            result.var_to_enum_type[p.name] = p.type_name;
            result.var_to_struct_type[scope + "." + p.name] = p.type_name;
            result.var_to_struct_type[p.name] = p.type_name;
        }
        if (p.default_value) {
            uint64_t init_val = eval_const_expr(p.default_value.get(), scope);
            LogicVal iv = LogicVal(w, init_val, 0);
            result.netlist[pid].value = iv;
            result.netlist[pid].prev_value = iv;
            result.netlist[pid].modified = false;
        }

        VariableDimMetadata pvdm;
        pvdm.name = scope + "." + p.name;
        if (!p.packed_dimensions.empty()) {
            for (const auto& pdim : p.packed_dimensions) {
                int64_t b1 = (int64_t)eval_const_expr(pdim.left_expr.get(), scope);
                int64_t b2 = pdim.right_expr ? (int64_t)eval_const_expr(pdim.right_expr.get(), scope) : 0;
                pvdm.dimensions.push_back({b1, b2, false, false, false});
            }
            pvdm.is_vector_or_array = true;
        } else if (p.msb_expr) {
            int64_t b1 = (int64_t)eval_const_expr(p.msb_expr.get(), scope);
            int64_t b2 = p.lsb_expr ? (int64_t)eval_const_expr(p.lsb_expr.get(), scope) : 0;
            pvdm.dimensions.push_back({b1, b2, false, false, false});
            pvdm.is_vector_or_array = true;
        } else if (p.type_name == "int" || p.type_name == "integer") {
            pvdm.dimensions.push_back({31, 0, false, false, false});
            pvdm.is_vector_or_array = true;
        } else if (p.type_name == "byte") {
            pvdm.dimensions.push_back({7, 0, false, false, false});
            pvdm.is_vector_or_array = true;
        } else if (p.type_name == "shortint") {
            pvdm.dimensions.push_back({15, 0, false, false, false});
            pvdm.is_vector_or_array = true;
        } else if (p.type_name == "longint" || p.type_name == "time") {
            pvdm.dimensions.push_back({63, 0, false, false, false});
            pvdm.is_vector_or_array = true;
        } else if (w > 1) {
            pvdm.dimensions.push_back({(int64_t)(w - 1), 0, false, false, false});
            pvdm.is_vector_or_array = true;
        }
        if (pvdm.is_vector_or_array) {
            result.var_dimensions[scope + "." + p.name] = pvdm;
            result.var_dimensions[p.name] = pvdm;
        }
    }

    for (auto& s : mod->signals) {
        uint32_t w = 1;
        if (s.msb_expr && s.lsb_expr) {
            uint64_t msb = eval_const_expr(s.msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(s.lsb_expr.get(), scope);
            w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
        }
        if (s.is_associative_array) {
            std::string aa_scope = scope + "." + s.name;
            result.assoc_array_map[aa_scope] = AssociativeArrayObject(aa_scope, w, s.is_signed, s.assoc_key_type, s.assoc_key_width);
            if (debug_out) {
                *debug_out << "     * Associative Array allocated: " << aa_scope 
                           << " [" << w << "-bit, key=" << s.assoc_key_type << " (" << s.assoc_key_width << "-bit)]\n";
            }
        } else if (s.is_dynamic_array) {
            std::string da_scope = scope + "." + s.name;
            result.dynamic_array_map[da_scope] = DynamicArrayObject(da_scope, w, s.is_signed);
            if (debug_out) {
                *debug_out << "     * Dynamic Array allocated: " << da_scope 
                           << " [" << w << "-bit]\n";
            }
        } else if (s.is_queue) {
            int64_t max_bound = -1;
            if (s.queue_bound_expr) {
                max_bound = (int64_t)eval_const_expr(s.queue_bound_expr.get(), scope);
            }
            std::string q_scope = scope + "." + s.name;
            result.queue_map[q_scope] = QueueObject(q_scope, w, s.is_signed, max_bound);
            if (debug_out) {
                *debug_out << "     * Queue allocated: " << q_scope 
                           << " [" << w << "-bit, bound=" << max_bound << "]\n";
            }
        } else if (s.is_array) {
            std::vector<ArrayDimInfo> all_dims;
            uint32_t total_depth = 1;
            if (!s.unpacked_dimensions.empty()) {
                for (const auto& udim : s.unpacked_dimensions) {
                    int64_t b1 = (int64_t)eval_const_expr(udim.left_expr.get(), scope);
                    int64_t b2 = udim.right_expr ? (int64_t)eval_const_expr(udim.right_expr.get(), scope) : 0;
                    if (udim.is_single) {
                        b2 = b1 > 0 ? (b1 - 1) : 0;
                        b1 = 0;
                    }
                    ArrayDimInfo info;
                    info.left = b1;
                    info.right = b2;
                    info.is_unpacked = true;
                    all_dims.push_back(info);
                    total_depth *= (uint32_t)info.size();
                }
            } else {
                int64_t b1 = 0;
                int64_t b2 = 0;
                if (s.bound1_expr && s.bound2_expr) {
                    b1 = (int64_t)eval_const_expr(s.bound1_expr.get(), scope);
                    b2 = (int64_t)eval_const_expr(s.bound2_expr.get(), scope);
                } else if (s.bound1_expr) {
                    uint32_t d = (uint32_t)eval_const_expr(s.bound1_expr.get(), scope);
                    b1 = 0;
                    b2 = d > 0 ? (int64_t)(d - 1) : 0;
                }
                ArrayDimInfo info;
                info.left = b1;
                info.right = b2;
                info.is_unpacked = true;
                all_dims.push_back(info);
                total_depth = (uint32_t)info.size();
            }
            uint32_t unpacked_cnt = (uint32_t)all_dims.size();

            // Packed dimensions
            if (!s.packed_dimensions.empty()) {
                for (const auto& pdim : s.packed_dimensions) {
                    int64_t b1 = (int64_t)eval_const_expr(pdim.left_expr.get(), scope);
                    int64_t b2 = pdim.right_expr ? (int64_t)eval_const_expr(pdim.right_expr.get(), scope) : 0;
                    ArrayDimInfo info;
                    info.left = b1;
                    info.right = b2;
                    info.is_unpacked = false;
                    all_dims.push_back(info);
                }
            } else if (s.msb_expr) {
                int64_t b1 = (int64_t)eval_const_expr(s.msb_expr.get(), scope);
                int64_t b2 = s.lsb_expr ? (int64_t)eval_const_expr(s.lsb_expr.get(), scope) : 0;
                ArrayDimInfo info;
                info.left = b1;
                info.right = b2;
                info.is_unpacked = false;
                all_dims.push_back(info);
            } else if (s.type_name == "int" || s.type_name == "integer") {
                all_dims.push_back({31, 0, false, false, false});
            } else if (s.type_name == "byte") {
                all_dims.push_back({7, 0, false, false, false});
            } else if (s.type_name == "shortint") {
                all_dims.push_back({15, 0, false, false, false});
            } else if (s.type_name == "longint" || s.type_name == "time") {
                all_dims.push_back({63, 0, false, false, false});
            }

            std::string mem_scope = scope + "." + s.name;
            int64_t b1_first = all_dims.empty() ? 0 : all_dims[0].left;
            int64_t b2_first = all_dims.empty() ? 0 : all_dims[0].right;
            MemoryArray marr(mem_scope, w, total_depth, b1_first, b2_first);
            marr.dimensions = all_dims;
            marr.unpacked_dims_count = unpacked_cnt;
            result.memory_map[mem_scope] = marr;

            VariableDimMetadata vdm;
            vdm.name = mem_scope;
            vdm.dimensions = all_dims;
            vdm.unpacked_dims = unpacked_cnt;
            vdm.is_vector_or_array = true;
            result.var_dimensions[mem_scope] = vdm;
            result.var_dimensions[s.name] = vdm;

            if (debug_out) {
                *debug_out << "     * Memory allocated: " << mem_scope 
                           << " [" << w << "-bit x " << total_depth << " words]\n";
            }
        } else {
            size_t sid = get_or_create_net(scope + "." + s.name, w, s.is_signed, s.net_kind);
            VariableDimMetadata svdm;
            svdm.name = scope + "." + s.name;
            if (s.is_dynamic_array) {
                svdm.is_dynamic = true;
                svdm.is_vector_or_array = true;
                svdm.unpacked_dims = 1;
            } else if (s.is_queue) {
                svdm.is_queue = true;
                svdm.is_vector_or_array = true;
                svdm.unpacked_dims = 1;
            } else if (!s.packed_dimensions.empty()) {
                for (const auto& pdim : s.packed_dimensions) {
                    int64_t b1 = (int64_t)eval_const_expr(pdim.left_expr.get(), scope);
                    int64_t b2 = pdim.right_expr ? (int64_t)eval_const_expr(pdim.right_expr.get(), scope) : 0;
                    svdm.dimensions.push_back({b1, b2, false, false, false});
                }
                svdm.is_vector_or_array = true;
            } else if (s.msb_expr) {
                int64_t b1 = (int64_t)eval_const_expr(s.msb_expr.get(), scope);
                int64_t b2 = s.lsb_expr ? (int64_t)eval_const_expr(s.lsb_expr.get(), scope) : 0;
                svdm.dimensions.push_back({b1, b2, false, false, false});
                svdm.is_vector_or_array = true;
            } else if (s.type_name == "int" || s.type_name == "integer") {
                svdm.dimensions.push_back({31, 0, false, false, false});
                svdm.is_vector_or_array = true;
            } else if (s.type_name == "byte") {
                svdm.dimensions.push_back({7, 0, false, false, false});
                svdm.is_vector_or_array = true;
            } else if (s.type_name == "shortint") {
                svdm.dimensions.push_back({15, 0, false, false, false});
                svdm.is_vector_or_array = true;
            } else if (s.type_name == "longint" || s.type_name == "time") {
                svdm.dimensions.push_back({63, 0, false, false, false});
                svdm.is_vector_or_array = true;
            } else if (w > 1) {
                svdm.dimensions.push_back({(int64_t)(w - 1), 0, false, false, false});
                svdm.is_vector_or_array = true;
            }
            if (svdm.is_vector_or_array) {
                result.var_dimensions[scope + "." + s.name] = svdm;
                result.var_dimensions[s.name] = svdm;
            }
            if (s.is_chandle || s.type_name == "chandle") {
                result.netlist[sid].is_chandle = true;
                result.netlist[sid].type_name = "chandle";
                result.netlist[sid].value = LogicVal(64, 0, 0);
                result.netlist[sid].prev_value = LogicVal(64, 0, 0);
                result.chandle_names.insert(scope + "." + s.name);
                result.chandle_names.insert(s.name);
            }
            if (s.is_semaphore || s.type_name == "semaphore") {
                result.netlist[sid].is_semaphore = true;
                result.netlist[sid].type_name = "semaphore";
                result.netlist[sid].value = LogicVal(64, 0, 0);
                result.netlist[sid].prev_value = LogicVal(64, 0, 0);
                result.semaphore_handle_names.insert(scope + "." + s.name);
                result.semaphore_handle_names.insert(s.name);
            }
            if (s.is_mailbox || s.type_name == "mailbox") {
                result.netlist[sid].is_mailbox = true;
                result.netlist[sid].type_name = "mailbox";
                result.netlist[sid].value = LogicVal(64, 0, 0);
                result.netlist[sid].prev_value = LogicVal(64, 0, 0);
                result.mailbox_handle_names.insert(scope + "." + s.name);
                result.mailbox_handle_names.insert(s.name);
            }
            if (s.is_open_array) {
                result.open_array_names.insert(scope + "." + s.name);
                result.open_array_names.insert(s.name);
            }
            if (s.type_name == "string") {
                result.netlist[sid].type_name = "string";
                result.netlist[sid].value = LogicVal::make_string("");
                result.netlist[sid].prev_value = LogicVal::make_string("");
                result.string_net_names.insert(scope + "." + s.name);
                result.string_net_names.insert(s.name);
            }
            if (s.is_class_handle) {
                result.netlist[sid].is_class_handle = true;
                result.netlist[sid].value = LogicVal(64, 0, 0);
                result.netlist[sid].prev_value = LogicVal(64, 0, 0);
                result.class_handle_names.insert(scope + "." + s.name);
                result.class_handle_names.insert(s.name);
                result.var_to_class_type[scope + "." + s.name] = s.class_type_name;
                result.var_to_class_type[s.name] = s.class_type_name;
            }
            if (s.is_covergroup_handle) {
                result.netlist[sid].is_covergroup_handle = true;
                result.netlist[sid].covergroup_type_name = s.covergroup_type_name;
                result.netlist[sid].value = LogicVal(64, 0, 0);
                result.netlist[sid].prev_value = LogicVal(64, 0, 0);
                result.covergroup_handle_names.insert(scope + "." + s.name);
                result.covergroup_handle_names.insert(s.name);
                result.var_to_covergroup_type[scope + "." + s.name] = s.covergroup_type_name;
                result.var_to_covergroup_type[s.name] = s.covergroup_type_name;
            }
            if (s.is_virtual_interface) {
                result.netlist[sid].is_virtual_interface = true;
                result.netlist[sid].interface_name = s.interface_name;
                result.netlist[sid].modport_name = s.modport_name;
                result.netlist[sid].value = LogicVal(64, 0, 0);
                result.netlist[sid].prev_value = LogicVal(64, 0, 0);
                result.virtual_interface_names.insert(scope + "." + s.name);
                result.virtual_interface_names.insert(s.name);
            }
            if (s.type_name == "bit") {
                result.netlist[sid].type_name = "bit";
                result.netlist[sid].value = LogicVal(w, 0, 0);
                result.netlist[sid].prev_value = LogicVal(w, 0, 0);
            }
            if (!s.type_name.empty()) {
                result.netlist[sid].type_name = s.type_name;
                result.var_to_enum_type[scope + "." + s.name] = s.type_name;
                result.var_to_enum_type[s.name] = s.type_name;
                result.var_to_struct_type[scope + "." + s.name] = s.type_name;
                result.var_to_struct_type[s.name] = s.type_name;
            }
            if (s.default_value) {
                uint64_t init_val = eval_const_expr(s.default_value.get(), scope);
                LogicVal iv = LogicVal(w, init_val, 0);
                result.netlist[sid].value = iv;
                result.netlist[sid].prev_value = iv;
                result.netlist[sid].modified = false;
            }
        }
    }

    for (auto& t : mod->tasks) {
        t->scope = scope;
        result.task_map[scope + "." + t->name] = t.get();
        result.task_map[t->name] = t.get();
    }

    for (auto& f : mod->functions) {
        f->scope = scope;
        result.function_map[scope + "." + f->name] = f.get();
        result.function_map[f->name] = f.get();
    }

    for (auto& dpi : mod->dpi_imports) {
        result.dpi_import_map[scope + "." + dpi->sv_name] = dpi.get();
        result.dpi_import_map[dpi->sv_name] = dpi.get();
    }

    for (auto& exp : mod->dpi_exports) {
        result.dpi_export_map[scope + "." + exp->c_name] = exp.get();
        result.dpi_export_map[scope + "." + exp->sv_name] = exp.get();
        result.dpi_export_map[exp->c_name] = exp.get();
        result.dpi_export_map[exp->sv_name] = exp.get();
    }

    for (auto& prop : mod->properties) {
        std::string full_prop_name = scope.empty() ? prop->name : (scope + "." + prop->name);
        result.property_defs[full_prop_name] = prop;
        result.property_defs[prop->name] = prop;
        result.property_names.insert(full_prop_name);
        result.property_names.insert(prop->name);
        for (const auto& lv : prop->local_vars) {
            result.sva_local_var_names.insert(lv.name);
            result.sva_local_var_names.insert(full_prop_name + "." + lv.name);
            if (!scope.empty()) result.sva_local_var_names.insert(scope + "." + lv.name);
        }
    }

    for (auto& seq : mod->sequences) {
        std::string full_seq_name = scope.empty() ? seq->name : (scope + "." + seq->name);
        result.sequence_defs[full_seq_name] = seq;
        result.sequence_defs[seq->name] = seq;
        result.sequence_names.insert(full_seq_name);
        result.sequence_names.insert(seq->name);
        for (const auto& lv : seq->local_vars) {
            result.sva_local_var_names.insert(lv.name);
            result.sva_local_var_names.insert(full_seq_name + "." + lv.name);
            if (!scope.empty()) result.sva_local_var_names.insert(scope + "." + lv.name);
        }
    }

    for (auto& ca : mod->continuous_assigns) {
        std::string scoped_target = scope + "." + ca->target_name;
        if (result.covergroup_handle_names.find(scoped_target) != result.covergroup_handle_names.end() ||
            result.covergroup_handle_names.find(ca->target_name) != result.covergroup_handle_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to covergroup handle '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 19");
        }
        if (result.class_handle_names.find(scoped_target) != result.class_handle_names.end() ||
            result.class_handle_names.find(ca->target_name) != result.class_handle_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to class handle '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 8.4");
        }
        if (result.queue_map.find(scoped_target) != result.queue_map.end() ||
            result.queue_map.find(ca->target_name) != result.queue_map.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to queue '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 7.10.4");
        }
        if (result.dynamic_array_map.find(scoped_target) != result.dynamic_array_map.end() ||
            result.dynamic_array_map.find(ca->target_name) != result.dynamic_array_map.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to dynamic array '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 7.5");
        }
        if (result.assoc_array_map.find(scoped_target) != result.assoc_array_map.end() ||
            result.assoc_array_map.find(ca->target_name) != result.assoc_array_map.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to associative array '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 7.8");
        }
        if (result.property_names.find(scoped_target) != result.property_names.end() ||
            result.property_names.find(ca->target_name) != result.property_names.end() ||
            mod->property_map.find(ca->target_name) != mod->property_map.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to property '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 16");
        }
        if (result.sequence_names.find(scoped_target) != result.sequence_names.end() ||
            result.sequence_names.find(ca->target_name) != result.sequence_names.end() ||
            mod->sequence_map.find(ca->target_name) != mod->sequence_map.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to sequence '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 16");
        }
        if (result.assertion_names.find(scoped_target) != result.assertion_names.end() ||
            result.assertion_names.find(ca->target_name) != result.assertion_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to assertion '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 16");
        }
        if (result.chandle_names.find(scoped_target) != result.chandle_names.end() ||
            result.chandle_names.find(ca->target_name) != result.chandle_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to chandle '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 35.5.4");
        }
        if (result.semaphore_handle_names.find(scoped_target) != result.semaphore_handle_names.end() ||
            result.semaphore_handle_names.find(ca->target_name) != result.semaphore_handle_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to semaphore '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 15.3");
        }
        if (result.mailbox_handle_names.find(scoped_target) != result.mailbox_handle_names.end() ||
            result.mailbox_handle_names.find(ca->target_name) != result.mailbox_handle_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to mailbox '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 15.4");
        }
        if (result.open_array_names.find(scoped_target) != result.open_array_names.end() ||
            result.open_array_names.find(ca->target_name) != result.open_array_names.end() ||
            (!ca->target_index_expr && (result.memory_map.find(scoped_target) != result.memory_map.end() ||
                                        result.memory_map.find(ca->target_name) != result.memory_map.end()))) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to open array or unpacked array '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 35.5.6.1");
        }
        if (result.sva_local_var_names.find(scoped_target) != result.sva_local_var_names.end() ||
            result.sva_local_var_names.find(ca->target_name) != result.sva_local_var_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to sequence or property local variable '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 16.10");
        }
        std::string base_target = ca->target_name;
        auto dot_p = ca->target_name.find('.');
        if (dot_p != std::string::npos) {
            base_target = ca->target_name.substr(0, dot_p);
        }
        std::string scoped_base_target = scope + "." + base_target;
        if (result.virtual_interface_names.find(scoped_target) != result.virtual_interface_names.end() ||
            result.virtual_interface_names.find(ca->target_name) != result.virtual_interface_names.end() ||
            result.virtual_interface_names.find(scoped_base_target) != result.virtual_interface_names.end() ||
            result.virtual_interface_names.find(base_target) != result.virtual_interface_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment driving through virtual interface '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 25.9");
        }
        if (mod->is_program) {
            bool drives_external = false;
            for (const auto& p : mod->ports) {
                if (p.name == ca->target_name && (p.direction == "output" || p.direction == "inout")) {
                    drives_external = true;
                    break;
                }
            }
            if (ca->target_name.find('.') != std::string::npos) {
                drives_external = true;
            }
            if (drives_external) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment in program '" + mod->name + "' driving external net '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 24.3");
            }
        }
        std::function<void(ExpressionNode*)> check_expr = [&](ExpressionNode* expr) {
            if (!expr) return;
            if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
                std::string s_id = scope + "." + id->name;
                if (result.covergroup_handle_names.find(s_id) != result.covergroup_handle_names.end() ||
                    result.covergroup_handle_names.find(id->name) != result.covergroup_handle_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: covergroup handle '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 19");
                }
                if (result.class_handle_names.find(s_id) != result.class_handle_names.end() ||
                    result.class_handle_names.find(id->name) != result.class_handle_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: class handle '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 8.4");
                }
                if (result.queue_map.find(s_id) != result.queue_map.end() ||
                    result.queue_map.find(id->name) != result.queue_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: queue '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 7.10.4");
                }
                if (result.dynamic_array_map.find(s_id) != result.dynamic_array_map.end() ||
                    result.dynamic_array_map.find(id->name) != result.dynamic_array_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: dynamic array '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 7.5");
                }
                if (result.assoc_array_map.find(s_id) != result.assoc_array_map.end() ||
                    result.assoc_array_map.find(id->name) != result.assoc_array_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: associative array '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 7.8");
                }
                if (result.property_names.find(s_id) != result.property_names.end() ||
                    result.property_names.find(id->name) != result.property_names.end() ||
                    mod->property_map.find(id->name) != mod->property_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: property '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 16");
                }
                if (result.sequence_names.find(s_id) != result.sequence_names.end() ||
                    result.sequence_names.find(id->name) != result.sequence_names.end() ||
                    mod->sequence_map.find(id->name) != mod->sequence_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: sequence '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 16");
                }
                if (result.assertion_names.find(s_id) != result.assertion_names.end() ||
                    result.assertion_names.find(id->name) != result.assertion_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: assertion '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 16");
                }
                if (result.chandle_names.find(s_id) != result.chandle_names.end() ||
                    result.chandle_names.find(id->name) != result.chandle_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: chandle '" + id->name + "' used directly in continuous assignment per IEEE 1800-2023 section 35.5.4");
                }
                if (result.semaphore_handle_names.find(s_id) != result.semaphore_handle_names.end() ||
                    result.semaphore_handle_names.find(id->name) != result.semaphore_handle_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: semaphore '" + id->name + "' used directly in continuous assignment per IEEE 1800-2023 section 15.3");
                }
                if (result.mailbox_handle_names.find(s_id) != result.mailbox_handle_names.end() ||
                    result.mailbox_handle_names.find(id->name) != result.mailbox_handle_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: mailbox '" + id->name + "' used directly in continuous assignment per IEEE 1800-2023 section 15.4");
                }
                if (result.string_net_names.find(s_id) != result.string_net_names.end() ||
                    result.string_net_names.find(id->name) != result.string_net_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: string variable '" + id->name + "' used directly in continuous assignment per IEEE 1800-2023 section 6.16");
                }
                if (result.open_array_names.find(s_id) != result.open_array_names.end() ||
                    result.open_array_names.find(id->name) != result.open_array_names.end() ||
                    result.memory_map.find(s_id) != result.memory_map.end() ||
                    result.memory_map.find(id->name) != result.memory_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: open array handle or unpacked array '" + id->name + "' used directly in continuous assignment per IEEE 1800-2023 section 35.5.6.1");
                }
                if (result.sva_local_var_names.find(s_id) != result.sva_local_var_names.end() ||
                    result.sva_local_var_names.find(id->name) != result.sva_local_var_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: sequence or property local variable '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 16.10");
                }
                if (result.virtual_interface_names.find(s_id) != result.virtual_interface_names.end() ||
                    result.virtual_interface_names.find(id->name) != result.virtual_interface_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: virtual interface '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 25.9");
                }
            } else if (dynamic_cast<SequenceMatchItemNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: SVA sequence match item used in continuous assignment per IEEE 1800-2023 section 16.10 & 16.11");
            } else if (auto ma = dynamic_cast<MemberAccessNode*>(expr)) {
                std::string s_id = scope + "." + ma->var_name;
                if (result.virtual_interface_names.find(s_id) != result.virtual_interface_names.end() ||
                    result.virtual_interface_names.find(ma->var_name) != result.virtual_interface_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: virtual interface dereference '" + ma->var_name + "." + ma->member_name + "' used in continuous assignment per IEEE 1800-2023 section 25.9");
                }
                if (result.covergroup_handle_names.find(s_id) != result.covergroup_handle_names.end() ||
                    result.covergroup_handle_names.find(ma->var_name) != result.covergroup_handle_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: covergroup item '" + ma->var_name + "." + ma->member_name + "' used in continuous assignment per IEEE 1800-2023 section 19");
                }
                if (result.class_handle_names.find(s_id) != result.class_handle_names.end() ||
                    result.class_handle_names.find(ma->var_name) != result.class_handle_names.end() ||
                    ma->var_name == "this" || ma->var_name == "super") {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: class property '" + ma->var_name + "." + ma->member_name + "' used in continuous assignment per IEEE 1800-2023 section 8.4");
                }
            } else if (dynamic_cast<SysCastCallNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: '$cast' used in continuous assignment per IEEE 1800-2023 section 8.16");
            } else if (dynamic_cast<NullNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'null' literal used in continuous assignment per IEEE 1800-2023 section 8.4");
            } else if (dynamic_cast<ClassNewNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: class constructor 'new()' used in continuous assignment per IEEE 1800-2023 section 8.7");
            } else if (dynamic_cast<ThisNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'this' used in continuous assignment per IEEE 1800-2023 section 8.11");
            } else if (dynamic_cast<RandomizeCallNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'randomize()' used in continuous assignment per IEEE 1800-2023 section 18");
            } else if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
                std::string s_id = scope + "." + idx->name;
                if (result.queue_map.find(s_id) != result.queue_map.end() ||
                    result.queue_map.find(idx->name) != result.queue_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: queue index select '" + idx->name + "[]' used in continuous assignment per IEEE 1800-2023 section 7.10.4");
                }
                if (result.dynamic_array_map.find(s_id) != result.dynamic_array_map.end() ||
                    result.dynamic_array_map.find(idx->name) != result.dynamic_array_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: dynamic array index select '" + idx->name + "[]' used in continuous assignment per IEEE 1800-2023 section 7.5");
                }
                if (result.assoc_array_map.find(s_id) != result.assoc_array_map.end() ||
                    result.assoc_array_map.find(idx->name) != result.assoc_array_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: associative array index select '" + idx->name + "[]' used in continuous assignment per IEEE 1800-2023 section 7.8");
                }
                check_expr(idx->msb_expr.get());
                check_expr(idx->lsb_expr.get());
            } else if (auto smc = dynamic_cast<StaticMethodCallNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: static class method '" + smc->class_type_name + "::" + smc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 8.25");
            } else if (auto sma = dynamic_cast<StaticMemberAccessNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: static class member '" + sma->class_type_name + "::" + sma->member_name + "' used in continuous assignment per IEEE 1800-2023 section 8.25");
            } else if (auto mc = dynamic_cast<MethodCallNode*>(expr)) {
                std::string s_id = scope + "." + mc->var_name;
                if (result.covergroup_handle_names.find(s_id) != result.covergroup_handle_names.end() ||
                    result.covergroup_handle_names.find(mc->var_name) != result.covergroup_handle_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: covergroup method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 19");
                }
                if (result.class_handle_names.find(s_id) != result.class_handle_names.end() ||
                    result.class_handle_names.find(mc->var_name) != result.class_handle_names.end() ||
                    mc->var_name == "this") {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: class method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 8.5");
                }
                if (result.queue_map.find(s_id) != result.queue_map.end() ||
                    result.queue_map.find(mc->var_name) != result.queue_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: queue method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 7.10.4");
                }
                if (result.dynamic_array_map.find(s_id) != result.dynamic_array_map.end() ||
                    result.dynamic_array_map.find(mc->var_name) != result.dynamic_array_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: dynamic array method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 7.5");
                }
                if (result.assoc_array_map.find(s_id) != result.assoc_array_map.end() ||
                    result.assoc_array_map.find(mc->var_name) != result.assoc_array_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: associative array method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 7.8");
                }
                if (result.memory_map.find(s_id) != result.memory_map.end() ||
                    result.memory_map.find(mc->var_name) != result.memory_map.end() ||
                    result.open_array_names.find(s_id) != result.open_array_names.end() ||
                    result.open_array_names.find(mc->var_name) != result.open_array_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: array method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 7.12");
                }
                if (mc->method_name == "len" || mc->method_name == "putc" || mc->method_name == "getc" ||
                    mc->method_name == "toupper" || mc->method_name == "tolower" || mc->method_name == "compare" ||
                    mc->method_name == "icompare" || mc->method_name == "substr" || mc->method_name == "atoi" ||
                    mc->method_name == "atohex" || mc->method_name == "atooct" || mc->method_name == "atobin" ||
                    mc->method_name == "atoreal") {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: string method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 6.16");
                }
                for (auto& a : mc->args) check_expr(a.get());
            } else if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
                if (bin->op == "==" || bin->op == "!=") {
                    auto is_null = [](ExpressionNode* e) {
                        return dynamic_cast<NullNode*>(e) != nullptr;
                    };
                    auto is_chandle_expr = [&](ExpressionNode* e) {
                        if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                            std::string s_id = scope + "." + id->name;
                            return (result.chandle_names.find(s_id) != result.chandle_names.end() ||
                                    result.chandle_names.find(id->name) != result.chandle_names.end());
                        }
                        return false;
                    };
                    if ((is_chandle_expr(bin->lhs.get()) && is_null(bin->rhs.get())) ||
                        (is_null(bin->lhs.get()) && is_chandle_expr(bin->rhs.get()))) {
                        return;
                    }
                }
                check_expr(bin->lhs.get());
                check_expr(bin->rhs.get());
            } else if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
                check_expr(un->operand.get());
            } else if (auto ter = dynamic_cast<TernaryOpNode*>(expr)) {
                check_expr(ter->condition.get());
                check_expr(ter->true_expr.get());
                check_expr(ter->false_expr.get());
            } else if (auto sf = dynamic_cast<SysSampledFuncNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: SVA sampled value function '" + sf->func_name + "' used in continuous assignment per IEEE 1800-2023 section 16.9.3");
            } else if (dynamic_cast<SequenceRepetitionNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] SVA sequence repetition operator used outside sequence/property context in continuous assignment per IEEE 1800-2023 section 16.9.2");
            } else if (dynamic_cast<SequenceConcatNode*>(expr)) {
                throw std::runtime_error("[CAS-DTINPCIL] SVA sequence concatenation used outside sequence/property context in continuous assignment per IEEE 1800-2023 section 16.9.2");
            } else if (auto sc = dynamic_cast<StreamingConcatNode*>(expr)) {
                if (sc->slice_size) check_expr(sc->slice_size.get());
                for (auto& item : sc->stream_items) {
                    check_expr(item.get());
                }
            } else if (auto fc = dynamic_cast<FunctionCallNode*>(expr)) {
                if (result.dpi_import_map.find(fc->func_name) != result.dpi_import_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: DPI-C foreign function '" + fc->func_name + "' used in continuous assignment per IEEE 1800-2023 section 35");
                }
                if (result.dpi_export_map.find(fc->func_name) != result.dpi_export_map.end() ||
                    result.dpi_export_map.find(scope + "." + fc->func_name) != result.dpi_export_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: DPI-C exported function '" + fc->func_name + "' used in continuous assignment per IEEE 1800-2023 section 35");
                }
                for (auto& a : fc->args) check_expr(a.get());
            }
        };
        if (ca->stream_target) {
            check_expr(ca->rhs.get());
            continue;
        }
        if (ca->target_index_expr) {
            check_expr(ca->rhs.get());
            result.memory_continuous_assigns.push_back({scope, ca->target_name, ca->target_index_expr->clone(), ca->rhs->clone()});
            continue;
        }
        {
            std::string t_id = scope + "." + ca->target_name;
            if (result.string_net_names.find(t_id) != result.string_net_names.end() ||
                result.string_net_names.find(ca->target_name) != result.string_net_names.end()) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: string variable '" + ca->target_name + "' driven by continuous assignment per IEEE 1800-2023 section 6.16");
            }
        }
        check_expr(ca->rhs.get());
        size_t target_id = result.net_map.at(scoped_target);
        result.continuous_assigns.push_back({scope, {target_id, 0, result.netlist[target_id].width}, ca->rhs.get()});
    }

    // IEEE 1800-2023 §10.6: Process alias statements
    for (auto& al : mod->aliases) {
        if (al->aliased_exprs.size() < 2) continue;
        NetSliceRef first_ref = resolve_slice_ref(al->aliased_exprs[0].get(), scope);
        if (result.netlist[first_ref.net_id].kind == NetKind::VAR) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: alias operand is a variable, only nets are permitted per IEEE 1800-2023 section 10.6");
        }
        for (size_t k = 1; k < al->aliased_exprs.size(); ++k) {
            NetSliceRef ref = resolve_slice_ref(al->aliased_exprs[k].get(), scope);
            if (result.netlist[ref.net_id].kind == NetKind::VAR) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: alias operand is a variable, only nets are permitted per IEEE 1800-2023 section 10.6");
            }
            if (ref.width != first_ref.width) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: bit width mismatch between aliased nets per IEEE 1800-2023 section 10.6");
            }
            NetKind k1 = result.netlist[first_ref.net_id].kind;
            NetKind k2 = result.netlist[ref.net_id].kind;
            if ((k1 == NetKind::WAND && k2 == NetKind::WOR) || (k1 == NetKind::WOR && k2 == NetKind::WAND)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: net type mismatch in alias per IEEE 1800-2023 section 10.6");
            }
            if (first_ref.offset == 0 && ref.offset == 0 &&
                first_ref.width == result.netlist[first_ref.net_id].width &&
                ref.width == result.netlist[ref.net_id].width) {
                alias_pairs.push_back(NetAliasPair(first_ref.net_id, ref.net_id, al->declaring_file, al->declaring_line));
            } else {
                result.slice_bindings.push_back({first_ref, ref});
                result.slice_bindings.push_back({ref, first_ref});
            }
        }
    }

    for (auto& g : mod->gates) {
        GateInstance gi;
        gi.type = g->type;
        gi.scope = scope;
        gi.udp_name = g->udp_name;
        gi.instance_name = g->instance_name;
        gi.out_target = resolve_slice_ref(g->out_expr.get(), scope);
        if (result.netlist[gi.out_target.net_id].kind == NetKind::VAR) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: gate or primitive output cannot drive variable/reg '" + result.netlist[gi.out_target.net_id].name + "' per IEEE 1800-2023 section 28 & 29");
        }
        for (auto& ie : g->in_exprs) gi.in_exprs.push_back(ie.get());
        result.gates.push_back(gi);
    }

    for (auto& pd : mod->path_delays) {
        uint64_t base_delay = eval_const_expr(pd->delay_expr.get(), scope);
        uint64_t delay_ticks = base_delay * Lexer::get_timescale_ratio();
        for (const auto& s_name : pd->sources) {
            std::string scoped_s = scope + "." + s_name;
            auto it_s = result.net_map.find(scoped_s);
            if (it_s == result.net_map.end()) continue;
            size_t s_id = it_s->second;
            if (result.netlist[s_id].kind == NetKind::VAR) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: specify path terminal '" + scoped_s + "' cannot be a variable/reg per IEEE 1800-2023 section 32");
            }
            for (const auto& d_name : pd->destinations) {
                std::string scoped_d = scope + "." + d_name;
                auto it_d = result.net_map.find(scoped_d);
                if (it_d == result.net_map.end()) continue;
                size_t d_id = it_d->second;
                if (result.netlist[d_id].kind == NetKind::VAR) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: specify path terminal '" + scoped_d + "' cannot be a variable/reg per IEEE 1800-2023 section 32");
                }
                ModulePathDelay mpd;
                mpd.src_net_id = s_id;
                mpd.dst_net_id = d_id;
                mpd.cond_expr = pd->cond_expr.get();
                mpd.scope = scope;
                mpd.delay_ticks = delay_ticks;
                result.module_path_delays.push_back(mpd);
                result.netlist[d_id].value = LogicVal::make_x(result.netlist[d_id].width);
                result.netlist[d_id].prev_value = LogicVal::make_x(result.netlist[d_id].width);
            }
        }
    }

    for (auto& tc : mod->timing_checks) {
        RuntimeTimingCheck rtc;
        rtc.type = tc->type;
        rtc.scope = scope;
        rtc.file = tc->file;
        rtc.line = tc->line;

        // Resolve reference signal
        std::string scoped_ref = scope + "." + tc->ref_event.signal_name;
        auto it_ref = result.net_map.find(scoped_ref);
        if (it_ref != result.net_map.end()) {
            rtc.ref_net_id = it_ref->second;
            rtc.ref_name = tc->ref_event.signal_name;
        } else {
            rtc.ref_name = tc->ref_event.signal_name;
        }
        rtc.ref_edge = tc->ref_event.edge;

        // Resolve data signal (if applicable)
        if (!tc->data_event.signal_name.empty()) {
            std::string scoped_data = scope + "." + tc->data_event.signal_name;
            auto it_data = result.net_map.find(scoped_data);
            if (it_data != result.net_map.end()) {
                rtc.data_net_id = it_data->second;
                rtc.data_name = tc->data_event.signal_name;
            } else {
                rtc.data_name = tc->data_event.signal_name;
            }
            rtc.data_edge = tc->data_event.edge;
        }

        uint64_t ratio = Lexer::get_timescale_ratio();
        if (ratio == 0) ratio = 1;

        if (tc->limit_expr) {
            uint64_t base_limit = eval_const_expr(tc->limit_expr.get(), scope);
            rtc.limit_ticks = base_limit * ratio;
        }
        if (tc->threshold_expr) {
            uint64_t base_thresh = eval_const_expr(tc->threshold_expr.get(), scope);
            rtc.threshold_ticks = base_thresh * ratio;
        }

        if (!tc->notifier_name.empty()) {
            std::string scoped_notif = scope + "." + tc->notifier_name;
            auto it_notif = result.net_map.find(scoped_notif);
            if (it_notif != result.net_map.end()) {
                rtc.notifier_net_id = it_notif->second;
                rtc.notifier_name = tc->notifier_name;
            } else {
                rtc.notifier_name = tc->notifier_name;
            }
        }

        result.timing_checks.push_back(rtc);
    }

    for (const auto& ev_name : mod->events) {
        std::string scoped_ev = scope + "." + ev_name;
        size_t ev_id = result.named_events.size();
        result.named_events.push_back({scoped_ev, (uint64_t)-1, {}});
        result.named_event_map[scoped_ev] = ev_id;
        if (result.named_event_map.find(ev_name) == result.named_event_map.end()) {
            result.named_event_map[ev_name] = ev_id;
        }
    }

    for (auto& cb_node : mod->clocking_blocks) {
        ClockingBlockInstance cb_inst;
        cb_inst.name = cb_node->name;
        cb_inst.scope = scope;
        cb_inst.is_default = cb_node->is_default;
        cb_inst.clock_edge = cb_node->clock_edge;
        cb_inst.clock_signal = cb_node->clock_signal;
        cb_inst.default_input_skew = cb_node->default_input_skew;
        cb_inst.default_output_skew = cb_node->default_output_skew;

        uint64_t timescale_ratio = Lexer::get_timescale_ratio();
        if (timescale_ratio == 0) timescale_ratio = 1;

        if (cb_node->default_input_skew == "1step") {
            cb_inst.default_input_skew_type = SkewType::STEP;
        }

        uint64_t def_out_ticks = 0;
        if (!cb_node->default_output_skew.empty() && cb_node->default_output_skew != "0" && cb_node->default_output_skew != "1step") {
            try {
                def_out_ticks = std::stoull(cb_node->default_output_skew) * timescale_ratio;
            } catch (...) {}
        }
        cb_inst.default_output_skew_ticks = def_out_ticks;

        std::string clk_scoped = scope.empty() ? cb_node->clock_signal : (scope + "." + cb_node->clock_signal);
        if (result.net_map.find(clk_scoped) != result.net_map.end()) {
            cb_inst.clock_net_id = result.net_map.at(clk_scoped);
        } else if (result.net_map.find(cb_node->clock_signal) != result.net_map.end()) {
            cb_inst.clock_net_id = result.net_map.at(cb_node->clock_signal);
        }

        for (const auto& item : cb_node->items) {
            ClockingSignalInfo sinfo;
            sinfo.direction = item.direction;
            sinfo.signal_name = item.signal_name;
            std::string eff_skew = item.skew.empty() 
                ? (item.direction == "input" ? cb_node->default_input_skew : cb_node->default_output_skew) 
                : item.skew;
            sinfo.skew = eff_skew;

            if (eff_skew == "1step" || eff_skew == "#1step") {
                sinfo.is_1step = true;
                sinfo.skew_type = SkewType::STEP;
                sinfo.skew_ticks = 0;
            } else if (!eff_skew.empty() && eff_skew != "0") {
                sinfo.skew_type = SkewType::TIME;
                try {
                    sinfo.skew_ticks = std::stoull(eff_skew) * timescale_ratio;
                } catch (...) {
                    sinfo.skew_ticks = 0;
                }
            } else {
                sinfo.skew_type = SkewType::NONE;
                sinfo.skew_ticks = 0;
            }

            std::string sig_scoped = scope.empty() ? item.signal_name : (scope + "." + item.signal_name);
            if (result.net_map.find(sig_scoped) != result.net_map.end()) {
                sinfo.net_id = result.net_map.at(sig_scoped);
            } else if (result.net_map.find(item.signal_name) != result.net_map.end()) {
                sinfo.net_id = result.net_map.at(item.signal_name);
            }
            cb_inst.signals[item.signal_name] = sinfo;
            cb_inst.signal_names.push_back(item.signal_name);
        }

        size_t cb_idx = result.clocking_blocks.size();
        result.clocking_blocks.push_back(std::move(cb_inst));
        std::string full_cb_name = scope.empty() ? cb_node->name : (scope + "." + cb_node->name);
        result.clocking_block_map[full_cb_name] = cb_idx;
        result.clocking_block_map[cb_node->name] = cb_idx;

        if (cb_node->is_default || mod->default_clocking_name == cb_node->name) {
            result.default_clocking_map[scope] = cb_idx;
        }
    }

    std::function<void(StatementNode*)> check_dcns = [&](StatementNode* s) {
        if (!s) return;
        if (auto cd = dynamic_cast<CycleDelayStatementNode*>(s)) {
            if (cd->clocking_name.empty()) {
                if (mod->default_clocking_name.empty()) {
                    throw std::runtime_error("[CAS-DTINPCIL] [CAS-DCNS] Default clocking not specified: Cycle delay operator requires a default clocking to be specified.");
                }
            } else {
                if (mod->clocking_block_map.find(cd->clocking_name) == mod->clocking_block_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] [CAS-DCNS] Default clocking not specified: Cycle delay operator requires a default clocking to be specified.");
                }
            }
        } else if (auto blk = dynamic_cast<BlockStatementNode*>(s)) {
            for (auto& child : blk->statements) check_dcns(child.get());
        } else if (auto if_s = dynamic_cast<IfStatementNode*>(s)) {
            for (auto& child : if_s->then_branch) check_dcns(child.get());
            for (auto& child : if_s->else_branch) check_dcns(child.get());
        } else if (auto for_s = dynamic_cast<ForStatementNode*>(s)) {
            for (auto& child : for_s->body) check_dcns(child.get());
        } else if (auto wh_s = dynamic_cast<WhileStatementNode*>(s)) {
            for (auto& child : wh_s->body) check_dcns(child.get());
        } else if (auto rep_s = dynamic_cast<RepeatStatementNode*>(s)) {
            for (auto& child : rep_s->body) check_dcns(child.get());
        } else if (auto fo_s = dynamic_cast<ForeverStatementNode*>(s)) {
            for (auto& child : fo_s->body) check_dcns(child.get());
        } else if (auto fork_s = dynamic_cast<ForkStatementNode*>(s)) {
            for (auto& child : fork_s->branches) check_dcns(child.get());
        }
    };
    for (auto& proc : mod->processes) {
        for (auto& s : proc->body) {
            check_dcns(s.get());
        }
    }
    for (auto& t : mod->tasks) {
        for (auto& s : t->body) {
            check_dcns(s.get());
        }
    }

    for (auto& proc : mod->processes) {
        BoundProcess bp(scope, proc.get(), 0, false);
        bp.pid = result.processes.size();
        if (mod->is_program) {
            bp.is_reactive = true;
            bp.is_program_process = true;
        }
        result.processes.push_back(std::move(bp));
    }

    // IEEE 1800-2023 §27: Unroll generate blocks
    for (auto& gnode : mod->generate_nodes) {
        unroll_generate_node(gnode.get(), scope, "", 0, false, debug_out);
    }

    for (auto& prop : mod->properties) {
        std::string full_prop_name = scope.empty() ? prop->name : (scope + "." + prop->name);
        result.property_defs[full_prop_name] = prop;
        result.property_defs[prop->name] = prop;
        result.property_names.insert(full_prop_name);
        result.property_names.insert(prop->name);
    }

    for (auto& seq : mod->sequences) {
        std::string full_seq_name = scope.empty() ? seq->name : (scope + "." + seq->name);
        result.sequence_defs[full_seq_name] = seq;
        result.sequence_defs[seq->name] = seq;
        result.sequence_names.insert(full_seq_name);
        result.sequence_names.insert(seq->name);
    }

    for (auto& asrt : mod->assertions) {
        SvaAssertionInstance inst;
        inst.label = asrt->label;
        inst.property_name = asrt->property_name;
        inst.scope = scope;

        if (!asrt->label.empty()) {
            std::string full_label = scope.empty() ? asrt->label : (scope + "." + asrt->label);
            result.assertion_names.insert(full_label);
            result.assertion_names.insert(asrt->label);
        }

        PropertyNode* target_prop = nullptr;
        if (!asrt->property_name.empty()) {
            std::string full_pname = scope.empty() ? asrt->property_name : (scope + "." + asrt->property_name);
            if (result.property_defs.count(full_pname)) target_prop = result.property_defs[full_pname].get();
            else if (result.property_defs.count(asrt->property_name)) target_prop = result.property_defs[asrt->property_name].get();
            else if (mod->property_map.count(asrt->property_name)) target_prop = mod->property_map[asrt->property_name];
        } else if (asrt->inlined_property) {
            target_prop = asrt->inlined_property.get();
        }

        if (target_prop) {
            inst.clock_edge = target_prop->clock_edge;
            inst.clock_signal = target_prop->clock_signal;

            std::string clk_scoped = scope.empty() ? target_prop->clock_signal : (scope + "." + target_prop->clock_signal);
            if (result.net_map.find(clk_scoped) != result.net_map.end()) {
                inst.clock_net_id = result.net_map.at(clk_scoped);
            } else if (result.net_map.find(target_prop->clock_signal) != result.net_map.end()) {
                inst.clock_net_id = result.net_map.at(target_prop->clock_signal);
            }

            inst.disable_expr = target_prop->disable_expr.get();
            inst.imp_type = target_prop->imp_type;
            inst.antecedent = target_prop->antecedent.get();
            inst.consequent = target_prop->consequent.get();
            inst.local_vars = target_prop->local_vars;
            for (const auto& lv : target_prop->local_vars) {
                result.sva_local_var_names.insert(lv.name);
                if (!scope.empty()) result.sva_local_var_names.insert(scope + "." + lv.name);
            }
            extract_sequence_steps(inst.consequent ? inst.consequent : inst.antecedent, inst.consequent_steps);
        }

        for (auto& s : asrt->pass_statements) {
            inst.pass_statements.push_back(s.get());
        }
        for (auto& s : asrt->fail_statements) {
            inst.fail_statements.push_back(s.get());
        }

        result.sva_assertions.push_back(std::move(inst));
    }

    for (auto& inst : mod->instantiations) {
        std::string child_scope = scope + "." + inst->instance_name;
        valid_instance_paths.insert(child_scope);
        valid_instance_paths.insert(inst->instance_name);

        std::string effective_mod_type = inst->module_type;

        // Check instance rebinding
        auto inst_rule_it = active_instance_cell_map.find(child_scope);
        if (inst_rule_it != active_instance_cell_map.end()) {
            effective_mod_type = inst_rule_it->second;
            if (debug_out) {
                *debug_out << "     [Config Rebind] Instance '" << child_scope 
                           << "' rebound: '" << inst->module_type << "' -> '" << effective_mod_type << "'\n";
            }
        } else {
            std::string rel_scope = child_scope;
            auto dot_pos = rel_scope.find('.');
            if (dot_pos != std::string::npos) {
                rel_scope = rel_scope.substr(dot_pos + 1);
            }
            inst_rule_it = active_instance_cell_map.find(rel_scope);
            if (inst_rule_it != active_instance_cell_map.end()) {
                effective_mod_type = inst_rule_it->second;
                if (debug_out) {
                    *debug_out << "     [Config Rebind] Instance '" << rel_scope 
                               << "' rebound: '" << inst->module_type << "' -> '" << effective_mod_type << "'\n";
                }
            } else {
                auto cell_rule_it = active_cell_rebind_map.find(inst->module_type);
                if (cell_rule_it != active_cell_rebind_map.end()) {
                    effective_mod_type = cell_rule_it->second;
                    if (debug_out) {
                        *debug_out << "     [Config Rebind] Cell '" << inst->module_type 
                                   << "' rebound globally -> '" << effective_mod_type << "'\n";
                    }
                }
            }
        }

        if (module_defs.find(effective_mod_type) == module_defs.end()) {
            throw std::runtime_error("Undefined module type instantiated: " + effective_mod_type);
        }
        ModuleNode* submod = module_defs.at(effective_mod_type);

        if (submod->is_interface) {
            result.interface_instances.insert(child_scope);
            if (debug_out) {
                *debug_out << "     * Interface instantiated: " << child_scope 
                           << " (" << submod->name << ")\n";
            }
        }

        elaborate_module(submod, child_scope, module_defs, &inst->param_overrides, scope, debug_out);

        for (size_t conn_idx = 0; conn_idx < inst->connections.size(); ++conn_idx) {
            auto& conn = inst->connections[conn_idx];
            std::string target_port_name;
            if (inst->is_positional) {
                if (conn_idx >= submod->ports.size()) {
                    throw std::runtime_error("Positional connection out of bounds in " + inst->instance_name);
                }
                target_port_name = submod->ports[conn_idx].name;
            } else {
                target_port_name = conn.port_name;
            }

            PortNode* formal_p = nullptr;
            for (auto& p : submod->ports) {
                if (p.name == target_port_name) {
                    formal_p = &p;
                    break;
                }
            }

            bool is_iface_port = false;
            if (formal_p) {
                if (formal_p->is_interface || formal_p->direction == "interface") {
                    is_iface_port = true;
                } else if (!formal_p->type_name.empty()) {
                    std::string base_type = formal_p->type_name;
                    auto dot_pos = base_type.find('.');
                    if (dot_pos != std::string::npos) {
                        base_type = base_type.substr(0, dot_pos);
                    }
                    if (module_defs.count(base_type) && module_defs.at(base_type)->is_interface) {
                        is_iface_port = true;
                    }
                } else if (!formal_p->interface_type_name.empty()) {
                    is_iface_port = true;
                }
            }
            if (!is_iface_port && conn.expr) {
                if (auto mem = dynamic_cast<MemberAccessNode*>(conn.expr.get())) {
                    std::string pkey = scope.empty() ? mem->var_name : (scope + "." + mem->var_name);
                    if (result.interface_instances.count(pkey) || result.interface_port_map.count(pkey)) {
                        is_iface_port = true;
                    }
                }
            }

            if (is_iface_port) {
                std::string actual_iface_name = "";
                std::string modport_view = "";
                if (auto id = dynamic_cast<IdentifierNode*>(conn.expr.get())) {
                    actual_iface_name = id->name;
                } else if (auto mem = dynamic_cast<MemberAccessNode*>(conn.expr.get())) {
                    actual_iface_name = mem->var_name;
                    modport_view = mem->member_name;
                }

                if (modport_view.empty() && formal_p && !formal_p->modport_name.empty()) {
                    modport_view = formal_p->modport_name;
                }

                std::string parent_port_key = scope.empty() ? actual_iface_name : (scope + "." + actual_iface_name);
                std::string bound_target = "";
                if (result.interface_port_map.find(parent_port_key) != result.interface_port_map.end()) {
                    bound_target = result.interface_port_map.at(parent_port_key);
                } else if (result.interface_instances.find(parent_port_key) != result.interface_instances.end()) {
                    bound_target = parent_port_key;
                } else {
                    bound_target = parent_port_key;
                }

                std::string child_port_key = child_scope + "." + target_port_name;
                result.interface_port_map[child_port_key] = bound_target;
                if (!modport_view.empty()) {
                    result.modport_binding_map[child_port_key] = modport_view;
                }
                if (debug_out) {
                    *debug_out << "     * Bound interface port: " << child_port_key 
                               << " -> " << bound_target;
                    if (!modport_view.empty()) {
                        *debug_out << " (modport: " << modport_view << ")";
                    }
                    *debug_out << "\n";
                }
                continue;
            }

            std::string dir = formal_p ? formal_p->direction : "input";
            if (formal_p && formal_p->is_array) {
                std::string child_mem_key = child_scope + "." + target_port_name;
                std::string parent_mem_key = "";
                if (auto pid = dynamic_cast<IdentifierNode*>(conn.expr.get())) {
                    parent_mem_key = scope.empty() ? pid->name : (scope + "." + pid->name);
                }
                if (dir == "inout") {
                    result.memory_bindings.push_back({parent_mem_key, child_mem_key});
                    result.memory_bindings.push_back({child_mem_key, parent_mem_key});
                } else if (dir == "input") {
                    result.memory_bindings.push_back({parent_mem_key, child_mem_key});
                } else {
                    result.memory_bindings.push_back({child_mem_key, parent_mem_key});
                }
                continue;
            }

            NetSliceRef parent_ref = resolve_slice_ref(conn.expr.get(), scope);
            size_t child_net_id = result.net_map.at(child_scope + "." + target_port_name);
            NetSliceRef child_ref = {child_net_id, 0, result.netlist[child_net_id].width};

            if (dir == "inout") {
                if (result.netlist[parent_ref.net_id].kind == NetKind::VAR) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: variable connected to inout port per IEEE 1800-2023 section 23.3.3.1");
                }
                if (parent_ref.width != child_ref.width) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: bit width mismatch on inout port per IEEE 1800-2023 section 23.3.3.1");
                }
                alias_pairs.push_back(NetAliasPair(parent_ref.net_id, child_net_id, "", 0));
            } else if (dir == "input") {
                result.slice_bindings.push_back({parent_ref, child_ref});
            } else {
                result.slice_bindings.push_back({child_ref, parent_ref});
            }
        }
    }
}

ElaborationResult Elaborator::elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules,
                                       const std::string& top_module_name,
                                       std::ostream* debug_out) {
    if (debug_out) *debug_out << "[PHASE 3: Elaboration] Synthesizing Netlist Hierarchy...\n";
    result = ElaborationResult();
    alias_pairs.clear();

    for (const auto& kv : Parser::global_packages) {
        const std::string& pkg_name = kv.first;
        const auto& pkg = kv.second;

        for (const auto& p : pkg->param_constants) {
            result.param_map[pkg_name + "::" + p.first] = p.second;
        }

        for (const auto& et : pkg->enum_typedefs) {
            EnumType e;
            e.name = et.first;
            e.width = et.second->base_width;
            e.is_signed = et.second->is_signed;
            for (const auto& m : et.second->members) {
                e.members.push_back({m.first, m.second});
                result.enum_constant_map[pkg_name + "::" + m.first] = m.second;
                result.enum_constant_map[m.first] = m.second;
            }
            result.enum_types[pkg_name + "::" + et.first] = e;
            result.enum_types[et.first] = e;
        }

        for (const auto& st : pkg->struct_typedefs) {
            PackedStructType s;
            s.name = st.first;
            s.is_signed = st.second->is_signed;
            s.total_width = st.second->total_width;

            size_t n = st.second->members.size();
            std::vector<uint32_t> offsets(n, 0);
            uint32_t running_offset = 0;
            for (int i = (int)n - 1; i >= 0; --i) {
                offsets[i] = running_offset;
                running_offset += st.second->members[i].width;
            }
            if (s.total_width == 0) {
                s.total_width = running_offset;
            }

            for (size_t i = 0; i < n; ++i) {
                const auto& mn = st.second->members[i];
                StructMember sm;
                sm.name = mn.name;
                sm.width = mn.width;
                sm.offset = offsets[i];
                sm.is_signed = mn.is_signed;
                sm.type_name = mn.type_name;
                s.members.push_back(sm);
            }

            result.struct_types[pkg_name + "::" + st.first] = s;
            result.struct_types[st.first] = s;
        }

        for (const auto& st : pkg->simple_typedefs) {
            SimpleTypeInfo sinfo;
            sinfo.name = st.second.name;
            sinfo.base_type = st.second.base_type;
            sinfo.width = st.second.width;
            sinfo.is_signed = st.second.is_signed;
            result.simple_types[pkg_name + "::" + st.first] = sinfo;
            result.simple_types[st.first] = sinfo;
        }

        for (const auto& ut : pkg->unpacked_array_typedefs) {
            UnpackedArrayTypeInfo uinfo;
            uinfo.name = ut.second.name;
            uinfo.elem_type = ut.second.elem_type;
            uinfo.elem_width = ut.second.elem_width;
            uinfo.elem_signed = ut.second.elem_signed;
            uinfo.array_size = ut.second.array_size;
            uinfo.start_idx = ut.second.start_idx;
            uinfo.end_idx = ut.second.end_idx;
            result.unpacked_array_types[pkg_name + "::" + ut.first] = uinfo;
            result.unpacked_array_types[ut.first] = uinfo;
        }

        for (const auto& fn : pkg->functions) {
            result.function_map[pkg_name + "::" + fn->name] = fn.get();
            result.function_map[fn->name] = fn.get();
        }

        for (const auto& t : pkg->tasks) {
            result.task_map[pkg_name + "::" + t->name] = t.get();
            result.task_map[t->name] = t.get();
        }

        for (const auto& cls : pkg->classes) {
            result.class_defs[pkg_name + "::" + cls->name] = cls.get();
            result.class_defs[cls->name] = cls.get();
        }

        for (const auto& dpi : pkg->dpi_imports) {
            result.dpi_import_map[pkg_name + "::" + dpi->sv_name] = dpi.get();
            result.dpi_import_map[dpi->sv_name] = dpi.get();
        }

        for (const auto& exp : pkg->dpi_exports) {
            result.dpi_export_map[pkg_name + "::" + exp->c_name] = exp.get();
            result.dpi_export_map[exp->c_name] = exp.get();
        }
    }

    for (auto& mod : modules) {
        for (auto& cls : mod->classes) {
            result.class_defs[cls->name] = cls.get();
        }
        for (auto& cg : mod->covergroups) {
            result.covergroup_defs[cg->name] = cg.get();
        }
        for (auto& dpi : mod->dpi_imports) {
            result.dpi_import_map[dpi->sv_name] = dpi.get();
        }
    }

    for (const auto& gc : Parser::global_classes) {
        if (result.class_defs.find(gc->name) == result.class_defs.end()) {
            result.class_defs[gc->name] = gc.get();
        }
    }

    // Check forward class declarations (§6.18)
    // 1. Packages
    for (const auto& pkg_pair : Parser::global_packages) {
        const auto& pkg = pkg_pair.second;
        for (const auto& fc : pkg->forward_classes) {
            auto it = pkg->class_map.find(fc);
            const ClassNode* target_node = (it != pkg->class_map.end()) ? it->second : nullptr;
            if (!target_node) {
                auto it2 = result.class_defs.find(pkg->name + "::" + fc);
                if (it2 != result.class_defs.end()) target_node = it2->second;
                else {
                    auto it3 = result.class_defs.find(fc);
                    if (it3 != result.class_defs.end()) target_node = it3->second;
                }
            }
            if (!target_node) {
                throw std::runtime_error("[CAS-DTINPCIL] Unresolved forward class declaration: '" + fc + "' was declared but never defined per IEEE 1800-2023 §6.18");
            }
            if (target_node->is_interface_class) {
                throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + fc + "' was declared as 'typedef class' but defined as an interface class per IEEE 1800-2023 §6.18");
            }
        }
        for (const auto& ifc : pkg->forward_interface_classes) {
            auto it = pkg->class_map.find(ifc);
            const ClassNode* target_node = (it != pkg->class_map.end()) ? it->second : nullptr;
            if (!target_node) {
                auto it2 = result.class_defs.find(pkg->name + "::" + ifc);
                if (it2 != result.class_defs.end()) target_node = it2->second;
                else {
                    auto it3 = result.class_defs.find(ifc);
                    if (it3 != result.class_defs.end()) target_node = it3->second;
                }
            }
            if (!target_node) {
                throw std::runtime_error("[CAS-DTINPCIL] Unresolved forward class declaration: '" + ifc + "' was declared but never defined per IEEE 1800-2023 §6.18");
            }
            if (!target_node->is_interface_class) {
                throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + ifc + "' was declared as 'typedef interface class' but defined as a regular class per IEEE 1800-2023 §6.18");
            }
        }
    }

    // 2. Modules
    for (const auto& mod : modules) {
        for (const auto& fc : mod->forward_classes) {
            auto it = mod->class_map.find(fc);
            const ClassNode* target_node = (it != mod->class_map.end()) ? it->second : nullptr;
            if (!target_node) {
                auto it2 = result.class_defs.find(fc);
                if (it2 != result.class_defs.end()) target_node = it2->second;
            }
            if (!target_node) {
                throw std::runtime_error("[CAS-DTINPCIL] Unresolved forward class declaration: '" + fc + "' was declared but never defined per IEEE 1800-2023 §6.18");
            }
            if (target_node->is_interface_class) {
                throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + fc + "' was declared as 'typedef class' but defined as an interface class per IEEE 1800-2023 §6.18");
            }
        }
        for (const auto& ifc : mod->forward_interface_classes) {
            auto it = mod->class_map.find(ifc);
            const ClassNode* target_node = (it != mod->class_map.end()) ? it->second : nullptr;
            if (!target_node) {
                auto it2 = result.class_defs.find(ifc);
                if (it2 != result.class_defs.end()) target_node = it2->second;
            }
            if (!target_node) {
                throw std::runtime_error("[CAS-DTINPCIL] Unresolved forward class declaration: '" + ifc + "' was declared but never defined per IEEE 1800-2023 §6.18");
            }
            if (!target_node->is_interface_class) {
                throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + ifc + "' was declared as 'typedef interface class' but defined as a regular class per IEEE 1800-2023 §6.18");
            }
        }
    }

    // 3. Global $unit
    for (const auto& fc : Parser::global_forward_classes) {
        auto it = result.class_defs.find(fc);
        if (it == result.class_defs.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Unresolved forward class declaration: '" + fc + "' was declared but never defined per IEEE 1800-2023 §6.18");
        }
        if (it->second->is_interface_class) {
            throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + fc + "' was declared as 'typedef class' but defined as an interface class per IEEE 1800-2023 §6.18");
        }
    }
    for (const auto& ifc : Parser::global_forward_interface_classes) {
        auto it = result.class_defs.find(ifc);
        if (it == result.class_defs.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Unresolved forward class declaration: '" + ifc + "' was declared but never defined per IEEE 1800-2023 §6.18");
        }
        if (!it->second->is_interface_class) {
            throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + ifc + "' was declared as 'typedef interface class' but defined as a regular class per IEEE 1800-2023 §6.18");
        }
    }

    // 4. Module name collision check
    for (const auto& mod : modules) {
        if (Parser::global_forward_classes.count(mod->name) ||
            Parser::global_forward_interface_classes.count(mod->name)) {
            throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + mod->name + "' was declared as 'typedef class' but subsequently defined as a module per IEEE 1800-2023 §6.18");
        }
        for (const auto& m2 : modules) {
            if (m2->forward_classes.count(mod->name) || m2->forward_interface_classes.count(mod->name)) {
                throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + mod->name + "' was declared as 'typedef class' but subsequently defined as a module per IEEE 1800-2023 §6.18");
            }
        }
    }

    // 1. Resolve base classes and interface inheritance / implementations
    for (auto& kv : result.class_defs) {
        ClassNode* cls = const_cast<ClassNode*>(kv.second);

        // Validation: if cls is an interface class, it cannot have base_class_name
        if (cls->is_interface_class && !cls->base_class_name.empty()) {
            throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + cls->name + "' cannot extend a non-interface class per IEEE 1800-2023 section 8.26");
        }

        // Base class for normal classes
        if (!cls->base_class_name.empty()) {
            auto it = result.class_defs.find(cls->base_class_name);
            if (it == result.class_defs.end()) {
                throw std::runtime_error("Class '" + cls->name + "' extends unknown base class '" + cls->base_class_name + "'");
            }
            cls->base_class = const_cast<ClassNode*>(it->second);
            if (cls->base_class->is_interface_class) {
                throw std::runtime_error("[CAS-DTINPCIL] Class '" + cls->name + "' cannot extend interface class '" + cls->base_class_name + "' per IEEE 1800-2023 section 8.26 (use 'implements')");
            }

            const ClassNode* cur = cls->base_class;
            while (cur) {
                if (cur == cls) {
                    throw std::runtime_error("Circular inheritance detected for class '" + cls->name + "'");
                }
                cur = cur->base_class;
            }
        }

        // Extended interfaces for interface classes
        for (const auto& parent_iface_name : cls->extended_interfaces) {
            auto it = result.class_defs.find(parent_iface_name);
            if (it == result.class_defs.end()) {
                throw std::runtime_error("Interface class '" + cls->name + "' extends unknown interface class '" + parent_iface_name + "'");
            }
            if (!it->second->is_interface_class) {
                throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + cls->name + "' cannot extend non-interface class '" + parent_iface_name + "' per IEEE 1800-2023 section 8.26");
            }
        }

        // Implemented interfaces for normal classes
        for (const auto& iface_name : cls->implemented_interfaces) {
            auto it = result.class_defs.find(iface_name);
            if (it == result.class_defs.end()) {
                throw std::runtime_error("Class '" + cls->name + "' implements unknown interface class '" + iface_name + "'");
            }
            if (!it->second->is_interface_class) {
                throw std::runtime_error("[CAS-DTINPCIL] Class '" + cls->name + "' cannot implement non-interface class '" + iface_name + "' per IEEE 1800-2023 section 8.26");
            }
        }
    }

    // 2. Compute transitive closure of extended/implemented interfaces
    std::unordered_set<ClassNode*> visited_iface_closure;
    std::function<void(ClassNode*, std::unordered_set<ClassNode*>&)> compute_iface_closure = [&](ClassNode* iface, std::unordered_set<ClassNode*>& active_path) {
        if (!iface) return;
        if (active_path.count(iface)) {
            throw std::runtime_error("Circular interface extension detected for interface class '" + iface->name + "'");
        }
        if (visited_iface_closure.count(iface)) return;
        active_path.insert(iface);
        iface->all_implemented_interfaces.insert(iface->name);
        for (const auto& parent_name : iface->extended_interfaces) {
            auto pit = result.class_defs.find(parent_name);
            if (pit != result.class_defs.end()) {
                ClassNode* pcls = const_cast<ClassNode*>(pit->second);
                compute_iface_closure(pcls, active_path);
                for (const auto& ancestor : pcls->all_implemented_interfaces) {
                    iface->all_implemented_interfaces.insert(ancestor);
                }
            }
        }
        active_path.erase(iface);
        visited_iface_closure.insert(iface);
    };

    for (auto& kv : result.class_defs) {
        ClassNode* cls = const_cast<ClassNode*>(kv.second);
        if (cls->is_interface_class) {
            std::unordered_set<ClassNode*> active_path;
            compute_iface_closure(cls, active_path);
        }
    }

    // For normal classes: collect all implemented interfaces
    std::function<void(ClassNode*)> compute_class_implements = [&](ClassNode* cls) {
        if (!cls) return;
        if (cls->base_class) {
            compute_class_implements(cls->base_class);
            for (const auto& iface : cls->base_class->all_implemented_interfaces) {
                cls->all_implemented_interfaces.insert(iface);
            }
        }
        for (const auto& iface_name : cls->implemented_interfaces) {
            auto it = result.class_defs.find(iface_name);
            if (it != result.class_defs.end()) {
                ClassNode* iface_cls = const_cast<ClassNode*>(it->second);
                for (const auto& anc : iface_cls->all_implemented_interfaces) {
                    cls->all_implemented_interfaces.insert(anc);
                }
            }
        }
    };

    for (auto& kv : result.class_defs) {
        ClassNode* cls = const_cast<ClassNode*>(kv.second);
        if (!cls->is_interface_class) {
            compute_class_implements(cls);
        }
    }

    // 3. Verify that concrete classes implement all pure virtual methods of implemented interfaces
    for (auto& kv : result.class_defs) {
        ClassNode* cls = const_cast<ClassNode*>(kv.second);
        if (cls->is_interface_class) continue;

        for (const auto& iface_name : cls->all_implemented_interfaces) {
            auto it = result.class_defs.find(iface_name);
            if (it == result.class_defs.end()) continue;
            const ClassNode* iface = it->second;

            // Check pure virtual functions
            for (const auto& ifn : iface->functions) {
                if (ifn->is_pure) {
                    FunctionNode* impl = cls->find_function(ifn->name);
                    if (!impl || impl->is_pure) {
                        throw std::runtime_error("[CAS-DTINPCIL] Class '" + cls->name + "' fails to implement pure virtual method '" + ifn->name + "' from interface class '" + iface_name + "' per IEEE 1800-2023 section 8.26");
                    }
                    if (impl->inputs.size() != ifn->inputs.size()) {
                        throw std::runtime_error("[CAS-DTINPCIL] Class '" + cls->name + "' method '" + ifn->name + "' argument count mismatch with interface class '" + iface_name + "' per IEEE 1800-2023 section 8.26");
                    }
                }
            }

            // Check pure virtual tasks
            for (const auto& itk : iface->tasks) {
                if (itk->is_pure) {
                    TaskNode* impl = cls->find_task(itk->name);
                    if (!impl || impl->is_pure) {
                        throw std::runtime_error("[CAS-DTINPCIL] Class '" + cls->name + "' fails to implement pure virtual method '" + itk->name + "' from interface class '" + iface_name + "' per IEEE 1800-2023 section 8.26");
                    }
                    if (impl->ports.size() != itk->ports.size()) {
                        throw std::runtime_error("[CAS-DTINPCIL] Class '" + cls->name + "' method '" + itk->name + "' argument count mismatch with interface class '" + iface_name + "' per IEEE 1800-2023 section 8.26");
                    }
                }
            }
        }

        // Check pure virtual methods inherited from ancestor abstract classes (§8.21)
        if (!cls->is_abstract) {
            const ClassNode* cur_base = cls->base_class;
            while (cur_base) {
                for (const auto& bfn : cur_base->functions) {
                    if (bfn->is_pure) {
                        FunctionNode* impl = cls->find_function(bfn->name);
                        if (!impl || impl->is_pure) {
                            throw std::runtime_error("[CAS-DTINPCIL] Concrete class '" + cls->name + "' fails to implement pure virtual method '" + bfn->name + "' from abstract class '" + cur_base->name + "' per IEEE 1800-2023 section 8.21");
                        }
                        if (impl->inputs.size() != bfn->inputs.size()) {
                            throw std::runtime_error("[CAS-DTINPCIL] Concrete class '" + cls->name + "' method '" + bfn->name + "' argument count mismatch with abstract class '" + cur_base->name + "' per IEEE 1800-2023 section 8.21");
                        }
                    }
                }
                for (const auto& btk : cur_base->tasks) {
                    if (btk->is_pure) {
                        TaskNode* impl = cls->find_task(btk->name);
                        if (!impl || impl->is_pure) {
                            throw std::runtime_error("[CAS-DTINPCIL] Concrete class '" + cls->name + "' fails to implement pure virtual method '" + btk->name + "' from abstract class '" + cur_base->name + "' per IEEE 1800-2023 section 8.21");
                        }
                        if (impl->ports.size() != btk->ports.size()) {
                            throw std::runtime_error("[CAS-DTINPCIL] Concrete class '" + cls->name + "' method '" + btk->name + "' argument count mismatch with abstract class '" + cur_base->name + "' per IEEE 1800-2023 section 8.21");
                        }
                    }
                }
                cur_base = cur_base->base_class;
            }

            for (const auto& fn : cls->functions) {
                if (fn->is_extern && !fn->has_body) {
                    throw std::runtime_error("[CAS-DTINPCIL] Concrete class '" + cls->name + "' has unimplemented extern method '" + fn->name + "' per IEEE 1800-2023 section 8.24");
                }
            }
            for (const auto& tk : cls->tasks) {
                if (tk->is_extern && !tk->has_body) {
                    throw std::runtime_error("[CAS-DTINPCIL] Concrete class '" + cls->name + "' has unimplemented extern task '" + tk->name + "' per IEEE 1800-2023 section 8.24");
                }
            }
        }
    }

    std::unordered_set<ClassNode*> visited_classes;
    std::function<void(ClassNode*)> build_vtable = [&](ClassNode* cls) {
        if (!cls || visited_classes.count(cls)) return;
        if (cls->base_class) {
            build_vtable(cls->base_class);
            cls->virtual_method_names = cls->base_class->virtual_method_names;
            cls->vtable_functions = cls->base_class->vtable_functions;
            cls->vtable_tasks = cls->base_class->vtable_tasks;
        }
        // Inherit virtual method names from implemented interfaces
        for (const auto& iface_name : cls->all_implemented_interfaces) {
            auto it = result.class_defs.find(iface_name);
            if (it != result.class_defs.end()) {
                const ClassNode* iface = it->second;
                for (const auto& fn : iface->functions) {
                    cls->virtual_method_names.insert(fn->name);
                }
                for (const auto& t : iface->tasks) {
                    cls->virtual_method_names.insert(t->name);
                }
            }
        }
        for (auto& fn : cls->functions) {
            if (fn->is_virtual || cls->virtual_method_names.count(fn->name)) {
                cls->virtual_method_names.insert(fn->name);
                cls->vtable_functions[fn->name] = fn.get();
                fn->is_virtual = true;
            }
        }
        for (auto& t : cls->tasks) {
            if (t->is_virtual || cls->virtual_method_names.count(t->name)) {
                cls->virtual_method_names.insert(t->name);
                cls->vtable_tasks[t->name] = t.get();
                t->is_virtual = true;
            }
        }
        visited_classes.insert(cls);
    };

    for (auto& kv : result.class_defs) {
        build_vtable(const_cast<ClassNode*>(kv.second));
    }

    std::unordered_map<std::string, ModuleNode*> module_defs;
    for (auto& mod : modules) {
        module_defs[mod->name] = mod.get();
    }

    std::string effective_top_name = top_module_name;
    active_config = nullptr;
    active_instance_cell_map.clear();
    active_cell_rebind_map.clear();
    valid_instance_paths.clear();

    if (!top_module_name.empty()) {
        auto cfg_it = Parser::global_configs.find(top_module_name);
        if (cfg_it != Parser::global_configs.end()) {
            active_config = cfg_it->second.get();
            effective_top_name = active_config->design_top_module;
            if (debug_out) {
                *debug_out << "  -> Selected Configuration: '" << active_config->name << "'\n"
                           << "     - Design Top Module: '" << effective_top_name << "'\n";
            }
        }
    }

    ModuleNode* top = nullptr;

    if (!effective_top_name.empty()) {
        if (module_defs.find(effective_top_name) != module_defs.end()) {
            top = module_defs.at(effective_top_name);
            if (debug_out) *debug_out << "  -> Using explicit top module: '" << top->name << "'\n";
        } else {
            throw std::runtime_error("Elaboration error: Top module '" + effective_top_name + "' was not found in parsed sources.");
        }
    } else {
        std::unordered_map<std::string, bool> is_instantiated;
        for (auto& mod : modules) {
            if (mod->is_interface || mod->is_program) continue;
            for (auto& inst : mod->instantiations) {
                is_instantiated[inst->module_type] = true;
            }
        }
        for (auto& mod : modules) {
            if (mod->is_interface || mod->is_program) continue;
            if (!is_instantiated[mod->name]) {
                if (mod->name.rfind("tb_", 0) == 0 || mod->name.find("_tb") != std::string::npos || 
                    mod->name.find("testbench") != std::string::npos || mod->name.find("regression") != std::string::npos) {
                    top = mod.get();
                    break;
                }
            }
        }
        if (!top) {
            for (auto& mod : modules) {
                if (mod->is_interface || mod->is_program) continue;
                if (!is_instantiated[mod->name]) {
                    top = mod.get();
                    break;
                }
            }
        }
        if (!top) {
            for (auto& mod : modules) {
                if (!mod->is_interface && !mod->is_program) { top = mod.get(); break; }
            }
        }
        if (!top) top = modules.front().get();
        if (debug_out) *debug_out << "  -> Auto-detected top-level root module: '" << top->name << "'\n";
    }

    // If no explicit configuration was selected, check if a configuration targets this top module
    if (!active_config && top) {
        for (const auto& kv : Parser::global_configs) {
            if (kv.second->design_top_module == top->name) {
                active_config = kv.second.get();
                if (debug_out) {
                    *debug_out << "  -> Auto-applied Configuration: '" << active_config->name 
                               << "' for top module '" << top->name << "'\n";
                }
                break;
            }
        }
    }

    if (active_config) {
        active_instance_cell_map = active_config->instance_cell_map;
        active_cell_rebind_map = active_config->cell_rebind_map;
    }

    defparam_overrides.clear();

    // Map instance hierarchy across the design tree starting at top
    std::unordered_map<std::string, ModuleNode*> instance_to_module;
    std::function<void(ModuleNode*, const std::string&)> map_hierarchy = 
        [&](ModuleNode* cur_mod, const std::string& cur_scope) {
            if (!cur_mod) return;
            instance_to_module[cur_scope] = cur_mod;
            valid_instance_paths.insert(cur_scope);
            for (const auto& inst : cur_mod->instantiations) {
                std::string child_cell = inst->module_type;
                if (active_config) {
                    std::string full_inst_path = cur_scope + "." + inst->instance_name;
                    if (active_instance_cell_map.find(full_inst_path) != active_instance_cell_map.end()) {
                        child_cell = active_instance_cell_map.at(full_inst_path);
                    } else if (active_cell_rebind_map.find(child_cell) != active_cell_rebind_map.end()) {
                        child_cell = active_cell_rebind_map.at(child_cell);
                    }
                }
                std::string child_scope = cur_scope + "." + inst->instance_name;
                auto it = module_defs.find(child_cell);
                if (it != module_defs.end()) {
                    map_hierarchy(it->second, child_scope);
                }
            }
        };
    map_hierarchy(top, top->name);

    // Collect pending defparam statements across all instantiated modules
    struct PendingDefparam {
        std::string declaring_scope;
        ModuleNode* declaring_mod;
        std::string hier_path;
        ExpressionNode* expr;
        std::string file;
        size_t line;
    };
    std::vector<PendingDefparam> pending_defparams;

    for (const auto& kv : instance_to_module) {
        const std::string& inst_scope = kv.first;
        ModuleNode* mod = kv.second;
        for (const auto& dp : mod->defparams) {
            pending_defparams.push_back({inst_scope, mod, dp.hierarchical_path, dp.value_expr.get(), dp.declaring_file, dp.declaring_line});
        }
    }

    struct ResolvedDefparam {
        std::string target_inst_scope;
        std::string target_param;
        std::string declaring_scope;
        ExpressionNode* expr;
        std::string file;
        size_t line;
    };
    std::vector<ResolvedDefparam> resolved_defparams;

    for (const auto& pdef : pending_defparams) {
        size_t last_dot = pdef.hier_path.rfind('.');
        std::string target_param;
        std::string rel_inst_path;
        if (last_dot == std::string::npos) {
            target_param = pdef.hier_path;
            rel_inst_path = "";
        } else {
            rel_inst_path = pdef.hier_path.substr(0, last_dot);
            target_param = pdef.hier_path.substr(last_dot + 1);
        }

        std::string target_inst_scope = "";
        if (rel_inst_path.empty()) {
            target_inst_scope = pdef.declaring_scope;
        } else {
            std::string cand1 = pdef.declaring_scope + "." + rel_inst_path;
            if (instance_to_module.find(cand1) != instance_to_module.end()) {
                target_inst_scope = cand1;
            } else if (instance_to_module.find(rel_inst_path) != instance_to_module.end()) {
                target_inst_scope = rel_inst_path;
            } else {
                std::string cur = pdef.declaring_scope;
                while (!cur.empty()) {
                    std::string cand = cur + "." + rel_inst_path;
                    if (instance_to_module.find(cand) != instance_to_module.end()) {
                        target_inst_scope = cand;
                        break;
                    }
                    auto pdot = cur.rfind('.');
                    if (pdot == std::string::npos) break;
                    cur = cur.substr(0, pdot);
                }
            }
        }

        if (target_inst_scope.empty() || instance_to_module.find(target_inst_scope) == instance_to_module.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] defparam targets non-existent hierarchical path '" + pdef.hier_path + "' per IEEE 1800-2023 section 23.10.1");
        }

        ModuleNode* target_mod = instance_to_module.at(target_inst_scope);
        const ParameterDef* target_pdef = nullptr;
        for (const auto& p : target_mod->parameters) {
            if (p.name == target_param) {
                target_pdef = &p;
                break;
            }
        }

        if (!target_pdef) {
            throw std::runtime_error("[CAS-DTINPCIL] defparam targets non-existent parameter '" + target_param + "' on hierarchical path '" + pdef.hier_path + "' per IEEE 1800-2023 section 23.10.1");
        }

        if (target_pdef->is_localparam) {
            throw std::runtime_error("[CAS-DTINPCIL] defparam cannot override localparam '" + target_param + "' on hierarchical path '" + pdef.hier_path + "' per IEEE 1800-2023 section 6.20.4 & 23.10.1");
        }

        resolved_defparams.push_back({target_inst_scope, target_param, pdef.declaring_scope, pdef.expr, pdef.file, pdef.line});
    }

    bool changed = true;
    for (int pass = 0; pass < 10 && changed; ++pass) {
        changed = false;
        for (const auto& rdp : resolved_defparams) {
            uint64_t val = eval_const_expr(rdp.expr, rdp.declaring_scope);
            auto& inst_map = defparam_overrides[rdp.target_inst_scope];
            auto it = inst_map.find(rdp.target_param);
            if (it == inst_map.end() || it->second != val) {
                inst_map[rdp.target_param] = val;
                result.param_map[rdp.target_inst_scope + "." + rdp.target_param] = val;
                changed = true;
            }
        }
    }

    if (debug_out) {
        for (const auto& kv : defparam_overrides) {
            for (const auto& p : kv.second) {
                *debug_out << "  -> Defparam applied: " << kv.first << "." << p.first << " = " << p.second << "\n";
            }
        }
    }

    elaborate_module(top, top->name, module_defs, nullptr, "", debug_out);
    result.top_scope = top->name;

    // IEEE 1800-2023 §10.6: Net Aliasing & Canonical Net Unification
    struct NetDSU {
        std::vector<size_t> parent;
        void init(size_t n) {
            parent.resize(n);
            for (size_t i = 0; i < n; ++i) parent[i] = i;
        }
        size_t find(size_t i) {
            if (i >= parent.size()) return i;
            if (parent[i] == i) return i;
            return parent[i] = find(parent[i]);
        }
        void unite(size_t i, size_t j) {
            size_t r1 = find(i);
            size_t r2 = find(j);
            if (r1 != r2) {
                if (r1 < r2) parent[r2] = r1;
                else parent[r1] = r2;
            }
        }
    };

    NetDSU dsu;
    dsu.init(result.netlist.size());
    for (const auto& pair : alias_pairs) {
        dsu.unite(pair.net_id1, pair.net_id2);
    }

    result.canonical_net_map.resize(result.netlist.size());
    result.alias_members.resize(result.netlist.size());
    for (size_t i = 0; i < result.netlist.size(); ++i) {
        size_t canon = dsu.find(i);
        result.canonical_net_map[i] = canon;
        result.alias_members[canon].push_back(i);
    }

    // Remap all references in net_map to canonical NetIDs
    for (auto& kv : result.net_map) {
        kv.second = dsu.find(kv.second);
    }

    for (auto& ca : result.continuous_assigns) {
        ca.target.net_id = dsu.find(ca.target.net_id);
    }

    for (auto& g : result.gates) {
        g.out_target.net_id = dsu.find(g.out_target.net_id);
    }

    for (auto& mpd : result.module_path_delays) {
        mpd.src_net_id = dsu.find(mpd.src_net_id);
        mpd.dst_net_id = dsu.find(mpd.dst_net_id);
    }

    for (auto& tc : result.timing_checks) {
        if (tc.ref_net_id != (size_t)-1) tc.ref_net_id = dsu.find(tc.ref_net_id);
        if (tc.data_net_id != (size_t)-1) tc.data_net_id = dsu.find(tc.data_net_id);
        if (tc.notifier_net_id != (size_t)-1) tc.notifier_net_id = dsu.find(tc.notifier_net_id);
    }

    std::vector<SliceBinding> remapped_bindings;
    for (auto& bind : result.slice_bindings) {
        bind.src.net_id = dsu.find(bind.src.net_id);
        bind.dst.net_id = dsu.find(bind.dst.net_id);
        if (bind.src.net_id == bind.dst.net_id && bind.src.offset == bind.dst.offset && bind.src.width == bind.dst.width) {
            continue;
        }
        remapped_bindings.push_back(bind);
    }
    result.slice_bindings = std::move(remapped_bindings);

    for (auto& bp : result.processes) {
        if (bp.wait_target.net_id != (size_t)-1) {
            bp.wait_target.net_id = dsu.find(bp.wait_target.net_id);
        }
    }

    for (auto& cb : result.clocking_blocks) {
        if (cb.clock_net_id != (size_t)-1) {
            cb.clock_net_id = dsu.find(cb.clock_net_id);
        }
        for (auto& sig_kv : cb.signals) {
            if (sig_kv.second.net_id != (size_t)-1) {
                sig_kv.second.net_id = dsu.find(sig_kv.second.net_id);
            }
        }
    }

    // Validate that all instance rules target valid hierarchical paths [CAS-DTINPCIL]
    if (active_config) {
        for (const auto& kv : active_config->instance_cell_map) {
            const std::string& rule_path = kv.first;
            if (valid_instance_paths.find(rule_path) == valid_instance_paths.end()) {
                throw std::runtime_error("error: [CAS-DTINPCIL] Configuration instance rule targets invalid or non-existent hierarchical path '" + rule_path + "'");
            }
        }
    }

    // IEEE Std 1800-2023 §9.2.2.4: Driver Exclusivity & ICPD Check
    struct NetDriverInfo {
        std::unordered_set<size_t> writing_procs;
        bool driven_by_always_ff_comb_latch = false;
        bool targeted_by_pca = false;
        bool has_continuous_assign = false;
        size_t continuous_assign_count = 0;
        std::string var_name = "";
    };
    std::vector<NetDriverInfo> net_drivers(result.netlist.size());

    auto resolve_target_net_id = [&](const std::string& target, const std::string& scope) -> size_t {
        std::string resolved_target = target;
        auto dot_pos_if = target.find('.');
        if (dot_pos_if != std::string::npos) {
            std::string prefix = target.substr(0, dot_pos_if);
            std::string suffix = target.substr(dot_pos_if + 1);
            std::string scoped_prefix = scope.empty() ? prefix : (scope + "." + prefix);
            if (result.interface_port_map.find(scoped_prefix) != result.interface_port_map.end()) {
                resolved_target = result.interface_port_map.at(scoped_prefix) + "." + suffix;
            } else if (result.interface_port_map.find(prefix) != result.interface_port_map.end()) {
                resolved_target = result.interface_port_map.at(prefix) + "." + suffix;
            }
            std::string scoped_cb = scope.empty() ? prefix : (scope + "." + prefix);
            if (result.clocking_block_map.find(scoped_cb) != result.clocking_block_map.end()) {
                size_t cb_idx = result.clocking_block_map.at(scoped_cb);
                const auto& cb = result.clocking_blocks[cb_idx];
                auto it_sig = cb.signals.find(suffix);
                if (it_sig != cb.signals.end()) {
                    return it_sig->second.net_id;
                }
            } else if (result.clocking_block_map.find(prefix) != result.clocking_block_map.end()) {
                size_t cb_idx = result.clocking_block_map.at(prefix);
                const auto& cb = result.clocking_blocks[cb_idx];
                auto it_sig = cb.signals.find(suffix);
                if (it_sig != cb.signals.end()) {
                    return it_sig->second.net_id;
                }
            }
        }
        std::string scoped = scope.empty() ? resolved_target : (scope + "." + resolved_target);
        if (result.net_map.find(scoped) != result.net_map.end()) {
            return result.net_map.at(scoped);
        }
        if (result.net_map.find(resolved_target) != result.net_map.end()) {
            return result.net_map.at(resolved_target);
        }
        for (const auto& kv : result.net_map) {
            if (kv.first.size() > target.size() &&
                kv.first.rfind("." + target) == kv.first.size() - (target.size() + 1)) {
                return kv.second;
            }
        }
        auto dot_pos = target.rfind('.');
        if (dot_pos != std::string::npos) {
            std::string base_target = target.substr(0, dot_pos);
            std::string scoped_base = scope.empty() ? base_target : (scope + "." + base_target);
            if (result.net_map.find(scoped_base) != result.net_map.end()) {
                return result.net_map.at(scoped_base);
            }
            if (result.net_map.find(base_target) != result.net_map.end()) {
                return result.net_map.at(base_target);
            }
        }
        return (size_t)-1;
    };

    auto get_connected_net_ids = [&](size_t start_id) -> std::vector<size_t> {
        std::vector<size_t> res;
        std::unordered_set<size_t> visited;
        std::queue<size_t> q;
        q.push(start_id);
        visited.insert(start_id);
        while (!q.empty()) {
            size_t curr = q.front();
            q.pop();
            res.push_back(curr);
            for (const auto& bind : result.slice_bindings) {
                if (bind.src.net_id == curr && visited.find(bind.dst.net_id) == visited.end()) {
                    visited.insert(bind.dst.net_id);
                    q.push(bind.dst.net_id);
                }
                if (bind.dst.net_id == curr && visited.find(bind.src.net_id) == visited.end()) {
                    visited.insert(bind.src.net_id);
                    q.push(bind.src.net_id);
                }
            }
        }
        return res;
    };

    std::function<void(StatementNode*, std::vector<std::string>&, std::vector<std::string>&)> collect_writes =
        [&](StatementNode* stmt, std::vector<std::string>& norm_targets, std::vector<std::string>& pca_targets) {
            if (!stmt) return;
            if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
                for (auto& s : blk->statements) collect_writes(s.get(), norm_targets, pca_targets);
            } else if (auto if_s = dynamic_cast<IfStatementNode*>(stmt)) {
                for (auto& s : if_s->then_branch) collect_writes(s.get(), norm_targets, pca_targets);
                for (auto& s : if_s->else_branch) collect_writes(s.get(), norm_targets, pca_targets);
            } else if (auto for_s = dynamic_cast<ForStatementNode*>(stmt)) {
                collect_writes(for_s->init_stmt.get(), norm_targets, pca_targets);
                collect_writes(for_s->step_stmt.get(), norm_targets, pca_targets);
                for (auto& s : for_s->body) collect_writes(s.get(), norm_targets, pca_targets);
            } else if (auto wh_s = dynamic_cast<WhileStatementNode*>(stmt)) {
                for (auto& s : wh_s->body) collect_writes(s.get(), norm_targets, pca_targets);
            } else if (auto rep_s = dynamic_cast<RepeatStatementNode*>(stmt)) {
                for (auto& s : rep_s->body) collect_writes(s.get(), norm_targets, pca_targets);
            } else if (auto fo_s = dynamic_cast<ForeverStatementNode*>(stmt)) {
                for (auto& s : fo_s->body) collect_writes(s.get(), norm_targets, pca_targets);
            } else if (auto cs_s = dynamic_cast<CaseStatementNode*>(stmt)) {
                for (auto& item : cs_s->items) {
                    for (auto& s : item.statements) collect_writes(s.get(), norm_targets, pca_targets);
                }
            } else if (auto ba = dynamic_cast<BlockingAssignNode*>(stmt)) {
                for (const auto& t : ba->lhs_targets) norm_targets.push_back(t);
            } else if (auto nba = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
                norm_targets.push_back(nba->target);
            } else if (auto mem = dynamic_cast<MemAssignNode*>(stmt)) {
                norm_targets.push_back(mem->mem_name);
            } else if (auto pa = dynamic_cast<ProceduralAssignNode*>(stmt)) {
                pca_targets.push_back(pa->target);
            } else if (auto pda = dynamic_cast<ProceduralDeassignNode*>(stmt)) {
                pca_targets.push_back(pda->target);
            }
        };

    for (size_t i = 0; i < result.processes.size(); ++i) {
        auto& bp = result.processes[i];
        bool is_strict_proc = bp.proc->is_always_ff || bp.proc->is_always_comb || bp.proc->is_always_latch;
        std::vector<std::string> norm_targets, pca_targets;
        for (auto& s : bp.proc->body) {
            collect_writes(s.get(), norm_targets, pca_targets);
        }

        for (const auto& t : norm_targets) {
            size_t nid = resolve_target_net_id(t, bp.scope);
            if (nid != (size_t)-1) {
                auto conn = get_connected_net_ids(nid);
                for (size_t cid : conn) {
                    net_drivers[cid].writing_procs.insert(i);
                    if (is_strict_proc) {
                        net_drivers[cid].driven_by_always_ff_comb_latch = true;
                        if (net_drivers[cid].var_name.empty()) {
                            std::string v = t;
                            auto dot_pos = v.rfind('.');
                            net_drivers[cid].var_name = (dot_pos != std::string::npos) ? v.substr(dot_pos + 1) : v;
                        }
                    }
                }
            }
        }

        for (const auto& t : pca_targets) {
            size_t nid = resolve_target_net_id(t, bp.scope);
            if (nid != (size_t)-1) {
                auto conn = get_connected_net_ids(nid);
                for (size_t cid : conn) {
                    net_drivers[cid].targeted_by_pca = true;
                    if (net_drivers[cid].var_name.empty()) {
                        std::string v = t;
                        auto dot_pos = v.rfind('.');
                        net_drivers[cid].var_name = (dot_pos != std::string::npos) ? v.substr(dot_pos + 1) : v;
                    }
                }
            }
        }
    }

    for (const auto& ca : result.continuous_assigns) {
        net_drivers[ca.target.net_id].continuous_assign_count++;
        auto conn = get_connected_net_ids(ca.target.net_id);
        for (size_t cid : conn) {
            net_drivers[cid].has_continuous_assign = true;
        }
    }

    for (size_t nid = 0; nid < result.netlist.size(); ++nid) {
        const auto& d = net_drivers[nid];
        const auto& net = result.netlist[nid];
        if (d.driven_by_always_ff_comb_latch) {
            if (d.writing_procs.size() > 1 || d.targeted_by_pca || d.has_continuous_assign) {
                std::string var_name = d.var_name;
                if (var_name.empty()) {
                    var_name = net.name;
                    auto dot_pos = var_name.rfind('.');
                    if (dot_pos != std::string::npos) var_name = var_name.substr(dot_pos + 1);
                }
                throw std::runtime_error("[CAS-ICPD] Illegal combination of procedural drivers: variable '" + 
                                         var_name + "' driven in 'always_ff'/'always_comb' cannot be driven by multiple processes");
            }
        }

        // IEEE 1800-2023 §6.5 & §10.3.2: Variables cannot have multiple continuous drivers
        if (net.kind == NetKind::VAR) {
            if (d.continuous_assign_count > 1) {
                std::string var_name = net.name;
                auto dot_pos = var_name.rfind('.');
                if (dot_pos != std::string::npos) var_name = var_name.substr(dot_pos + 1);
                throw std::runtime_error("[CAS-MCD] Illegal combination of continuous drivers: variable '" + 
                                         var_name + "' cannot be driven by multiple continuous assignments.");
            }
        }
    }

    return std::move(result);
}
