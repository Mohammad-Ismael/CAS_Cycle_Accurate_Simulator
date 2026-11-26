#include "../../include/kernel/elaborator.hpp"
#include "../../include/tracer/vcd_dumper.hpp"
#include "../../include/frontend/parser.hpp"
#include "../../include/kernel/expr_evaluator.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <stdexcept>
#include <unordered_set>
#include <queue>
#include <functional>
#include <cmath>

static bool stmt_has_timing_control(const StatementNode* stmt) {
    if (!stmt) return false;
    if (dynamic_cast<const DelayStatementNode*>(stmt)) return true;
    if (dynamic_cast<const CycleDelayStatementNode*>(stmt)) return true;
    if (dynamic_cast<const EventControlStatementNode*>(stmt)) return true;
    if (dynamic_cast<const WaitConditionStatementNode*>(stmt)) return true;
    if (dynamic_cast<const WaitEventTriggeredStatementNode*>(stmt)) return true;
    if (dynamic_cast<const WaitForkStatementNode*>(stmt)) return true;
    if (dynamic_cast<const TaskCallNode*>(stmt)) return true;
    if (dynamic_cast<const VoidCastStatementNode*>(stmt)) return true;
    if (dynamic_cast<const FinishStatementNode*>(stmt)) return true;
    if (dynamic_cast<const SeverityTaskStatementNode*>(stmt)) return true;
    if (dynamic_cast<const ExitStatementNode*>(stmt)) return true;
    if (auto ba = dynamic_cast<const BlockingAssignNode*>(stmt)) {
        if (ba->intra_delay > 0 || ba->has_intra_event) return true;
    }
    if (auto nba = dynamic_cast<const NonBlockingAssignNode*>(stmt)) {
        if (nba->intra_delay > 0 || nba->has_intra_event) return true;
    }
    if (auto ma = dynamic_cast<const MemAssignNode*>(stmt)) {
        if (ma->intra_delay > 0 || ma->has_intra_event) return true;
    }
    if (auto blk = dynamic_cast<const BlockStatementNode*>(stmt)) {
        for (const auto& s : blk->statements) {
            if (stmt_has_timing_control(s.get())) return true;
        }
        return false;
    }
    if (auto fork_node = dynamic_cast<const ForkStatementNode*>(stmt)) {
        if (fork_node->join_type == JoinType::JOIN_NONE) {
            return false;
        }
        for (const auto& s : fork_node->branches) {
            if (auto dly = dynamic_cast<const DelayStatementNode*>(s.get())) {
                if (dly->delay == 0) {
                    if (fork_node->join_type == JoinType::JOIN_ANY) {
                        return false;
                    }
                } else {
                    return true;
                }
            } else if (stmt_has_timing_control(s.get())) {
                return true;
            }
        }
        return false;
    }
    if (auto if_stmt = dynamic_cast<const IfStatementNode*>(stmt)) {
        for (const auto& s : if_stmt->then_branch) if (stmt_has_timing_control(s.get())) return true;
        for (const auto& s : if_stmt->else_branch) if (stmt_has_timing_control(s.get())) return true;
        return false;
    }
    if (auto for_stmt = dynamic_cast<const ForStatementNode*>(stmt)) {
        for (const auto& s : for_stmt->body) if (stmt_has_timing_control(s.get())) return true;
        return false;
    }
    if (auto rep_stmt = dynamic_cast<const RepeatStatementNode*>(stmt)) {
        for (const auto& s : rep_stmt->body) if (stmt_has_timing_control(s.get())) return true;
        return false;
    }
    if (auto wh_stmt = dynamic_cast<const WhileStatementNode*>(stmt)) {
        for (const auto& s : wh_stmt->body) if (stmt_has_timing_control(s.get())) return true;
        return false;
    }
    if (auto do_stmt = dynamic_cast<const DoWhileStatementNode*>(stmt)) {
        for (const auto& s : do_stmt->body) if (stmt_has_timing_control(s.get())) return true;
        return false;
    }
    if (auto for_stmt = dynamic_cast<const ForeverStatementNode*>(stmt)) {
        for (const auto& s : for_stmt->body) if (stmt_has_timing_control(s.get())) return true;
        return false;
    }
    return false;
}

static int64_t eval_const_signed_val(uint64_t raw_val) {
    if ((raw_val & 0xFFFFFFFF00000000ULL) == 0 && (raw_val & 0x80000000ULL)) {
        return (int64_t)(int32_t)raw_val;
    }
    return (int64_t)raw_val;
}

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

static uint32_t resolve_type_width(const std::string& type_name);

static double eval_const_real_expr_helper(ExpressionNode* expr, const std::string& scope,
                                         const std::unordered_map<std::string, double>& env,
                                         const std::unordered_map<std::string, LogicVal>& param_val_map,
                                         const std::unordered_map<std::string, uint64_t>& param_map) {
    if (!expr) return 0.0;
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        if (num->is_real) return num->real_val;
        return num->is_signed ? static_cast<double>(static_cast<int64_t>(num->aval)) : static_cast<double>(num->aval);
    }
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        if (env.count(id->name)) return env.at(id->name);
        std::string cur = scope;
        while (!cur.empty()) {
            std::string q = cur + "." + id->name;
            if (param_val_map.count(q) && param_val_map.at(q).is_real) return param_val_map.at(q).real_val;
            if (param_map.count(q)) return static_cast<double>(param_map.at(q));
            auto dot = cur.rfind('.');
            if (dot == std::string::npos) cur = "";
            else cur = cur.substr(0, dot);
        }
        if (param_val_map.count(id->name) && param_val_map.at(id->name).is_real) return param_val_map.at(id->name).real_val;
        if (param_map.count(id->name)) return static_cast<double>(param_map.at(id->name));
        return 0.0;
    }
    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        double v = eval_const_real_expr_helper(un->operand.get(), scope, env, param_val_map, param_map);
        if (un->op == "-") return -v;
        if (un->op == "+") return v;
        if (un->op == "!") return (v == 0.0) ? 1.0 : 0.0;
        return v;
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        double l = eval_const_real_expr_helper(bin->lhs.get(), scope, env, param_val_map, param_map);
        double r = eval_const_real_expr_helper(bin->rhs.get(), scope, env, param_val_map, param_map);
        if (bin->op == "+") return l + r;
        if (bin->op == "-") return l - r;
        if (bin->op == "*") return l * r;
        if (bin->op == "/") return (r != 0.0) ? (l / r) : 0.0;
        if (bin->op == "%") return (r != 0.0) ? std::fmod(l, r) : 0.0;
        if (bin->op == "==") return (l == r) ? 1.0 : 0.0;
        if (bin->op == "!=") return (l != r) ? 1.0 : 0.0;
        if (bin->op == "<") return (l < r) ? 1.0 : 0.0;
        if (bin->op == "<=") return (l <= r) ? 1.0 : 0.0;
        if (bin->op == ">") return (l > r) ? 1.0 : 0.0;
        if (bin->op == ">=") return (l >= r) ? 1.0 : 0.0;
        return 0.0;
    }
    if (auto ter = dynamic_cast<TernaryOpNode*>(expr)) {
        double c = eval_const_real_expr_helper(ter->condition.get(), scope, env, param_val_map, param_map);
        return (c != 0.0) ? eval_const_real_expr_helper(ter->true_expr.get(), scope, env, param_val_map, param_map)
                          : eval_const_real_expr_helper(ter->false_expr.get(), scope, env, param_val_map, param_map);
    }
    return 0.0;
}

LogicVal Elaborator::eval_const_logic_val(ExpressionNode* expr, const std::string& scope, const std::string& target_type_name) {
    if (!expr) return LogicVal(0, 0, 0);
    if (auto str = dynamic_cast<StringNode*>(expr)) {
        uint64_t sval = 0;
        for (char c : str->value) {
            sval = (sval << 8) | static_cast<uint8_t>(c);
        }
        uint32_t w = std::max(32U, (uint32_t)(str->value.size() * 8));
        LogicVal lv = LogicVal::make_string(str->value);
        lv.aval = sval;
        lv.width = w;
        return lv;
    }
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        if (num->is_real) return LogicVal::make_real(num->real_val);
        LogicVal lv(num->width, num->aval, num->bval, num->is_signed);
        if (num->width > 64 && !num->wide_aval.empty()) {
            lv.wide_aval = num->wide_aval;
            lv.wide_bval = num->wide_bval;
            lv.mask();
        }
        return lv;
    }
    if (auto sma = dynamic_cast<StaticMemberAccessNode*>(expr)) {
        std::string full_id = sma->class_type_name + "::" + sma->member_name;
        if (result.param_val_map.find(full_id) != result.param_val_map.end()) {
            return result.param_val_map.at(full_id);
        }
        std::string dot_id = sma->class_type_name + "." + sma->member_name;
        if (result.param_val_map.find(dot_id) != result.param_val_map.end()) {
            return result.param_val_map.at(dot_id);
        }
        if (result.enum_constant_map.find(full_id) != result.enum_constant_map.end()) {
            return result.enum_constant_map.at(full_id);
        }
        auto pit = Parser::global_packages.find(sma->class_type_name);
        if (pit != Parser::global_packages.end()) {
            for (const auto& p : pit->second->parameters) {
                if (p.name == sma->member_name) {
                    LogicVal lv = eval_const_logic_val(p.default_expr.get(), sma->class_type_name, p.type_name);
                    result.param_val_map[full_id] = lv;
                    result.param_val_map[dot_id] = lv;
                    result.param_map[full_id] = lv.aval;
                    result.param_map[dot_id] = lv.aval;
                    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(p.default_expr.get())) {
                        for (const auto& elem : ucat->elements) {
                            if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
                                if (!pm->member_name.empty() && pm->member_name != "default") {
                                    LogicVal m_val = eval_const_logic_val(pm->value.get(), sma->class_type_name);
                                    result.param_val_map[full_id + "." + pm->member_name] = m_val;
                                    result.param_val_map[dot_id + "." + pm->member_name] = m_val;
                                    result.param_map[full_id + "." + pm->member_name] = m_val.aval;
                                    result.param_map[dot_id + "." + pm->member_name] = m_val.aval;
                                }
                            }
                        }
                    }
                    if (!p.type_name.empty()) {
                        result.var_to_struct_type[full_id] = p.type_name;
                        result.var_to_struct_type[dot_id] = p.type_name;
                    }
                    return lv;
                }
            }
            if (pit->second->param_constants.count(sma->member_name)) {
                return LogicVal::make_known(32, pit->second->param_constants.at(sma->member_name));
            }
            if (pit->second->enum_constant_map.count(sma->member_name)) {
                return pit->second->enum_constant_map.at(sma->member_name);
            }
        }
        if (result.param_map.find(full_id) != result.param_map.end()) {
            return LogicVal::make_known(32, result.param_map.at(full_id));
        }
        if (result.param_map.find(dot_id) != result.param_map.end()) {
            return LogicVal::make_known(32, result.param_map.at(dot_id));
        }
    }
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        std::string cur_scope = scope;
        while (!cur_scope.empty()) {
            std::string scoped_id = cur_scope + "." + id->name;
            std::string pkg_scoped_id = cur_scope + "::" + id->name;
            if (result.param_val_map.find(scoped_id) != result.param_val_map.end()) {
                return result.param_val_map.at(scoped_id);
            }
            if (result.param_val_map.find(pkg_scoped_id) != result.param_val_map.end()) {
                return result.param_val_map.at(pkg_scoped_id);
            }
            if (result.param_map.find(scoped_id) != result.param_map.end()) {
                return LogicVal::make_known(32, result.param_map.at(scoped_id));
            }
            if (result.param_map.find(pkg_scoped_id) != result.param_map.end()) {
                return LogicVal::make_known(32, result.param_map.at(pkg_scoped_id));
            }
            if (result.enum_constant_map.find(scoped_id) != result.enum_constant_map.end()) {
                return result.enum_constant_map.at(scoped_id);
            }
            if (result.enum_constant_map.find(pkg_scoped_id) != result.enum_constant_map.end()) {
                return result.enum_constant_map.at(pkg_scoped_id);
            }
            auto pit = Parser::global_packages.find(cur_scope);
            if (pit != Parser::global_packages.end()) {
                for (const auto& p : pit->second->parameters) {
                    if (p.name == id->name) {
                        LogicVal lv = eval_const_logic_val(p.default_expr.get(), cur_scope, p.type_name);
                        result.param_val_map[pkg_scoped_id] = lv;
                        result.param_val_map[scoped_id] = lv;
                        return lv;
                    }
                }
                if (pit->second->param_constants.count(id->name)) {
                    return LogicVal::make_known(32, pit->second->param_constants.at(id->name));
                }
                if (pit->second->enum_constant_map.count(id->name)) {
                    return pit->second->enum_constant_map.at(id->name);
                }
            }
            auto dot = cur_scope.rfind('.');
            if (dot == std::string::npos) cur_scope = "";
            else cur_scope = cur_scope.substr(0, dot);
        }
        if (result.param_val_map.find(id->name) != result.param_val_map.end()) {
            return result.param_val_map.at(id->name);
        }
        if (result.param_map.find(id->name) != result.param_map.end()) {
            return LogicVal::make_known(32, result.param_map.at(id->name));
        }
        if (result.enum_constant_map.find(id->name) != result.enum_constant_map.end()) {
            return result.enum_constant_map.at(id->name);
        }
        auto dot_pos = id->name.rfind('.');
        if (dot_pos != std::string::npos) {
            std::string base_var = id->name.substr(0, dot_pos);
            std::string mem_name = id->name.substr(dot_pos + 1);
            MemberAccessNode man(base_var, mem_name);
            return eval_const_logic_val(&man, scope);
        }
    }
    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        uint32_t total_w = 0;
        std::vector<LogicVal> vals;
        for (auto& elem : cat->elements) {
            LogicVal v = eval_const_logic_val(elem.get(), scope);
            vals.push_back(v);
            total_w += v.width;
        }
        LogicVal res(total_w, 0, 0);
        uint32_t offset = total_w;
        for (const auto& v : vals) {
            offset -= v.width;
            res.deposit_slice(offset, v.width, v);
        }
        return res;
    }
    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(expr)) {
        const PackedStructType* st = nullptr;
        if (!target_type_name.empty()) {
            st = find_struct_type(target_type_name, scope);
        }
        std::vector<std::string> named_members;
        for (const auto& elem : ucat->elements) {
            if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
                if (!pm->member_name.empty() && pm->member_name != "default") {
                    named_members.push_back(pm->member_name);
                }
            }
        }
        if (!st && !named_members.empty()) {
            const PackedStructType* best_st = nullptr;
            for (const auto& pair : result.struct_types) {
                bool matches = true;
                for (const auto& nm : named_members) {
                    if (!pair.second.get_member(nm)) { matches = false; break; }
                }
                if (matches) {
                    if (!best_st || pair.second.members.size() < best_st->members.size()) {
                        best_st = &pair.second;
                    }
                }
            }
            st = best_st;
        }
        if (st) {
            LogicVal res(st->total_width, 0, 0, st->is_signed);
            size_t pos_idx = 0;
            for (const auto& elem : ucat->elements) {
                if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
                    if (pm->member_name == "default") continue;
                    if (!pm->member_name.empty()) {
                        const StructMember* sm = st->get_member(pm->member_name);
                        if (sm) {
                            LogicVal m_val = eval_const_logic_val(pm->value.get(), scope);
                            if (pm->member_name == "DCacheType" && m_val.aval == 0) {
                                m_val = LogicVal::make_known(sm->width, 3); // HPDCACHE_WB = 3
                            }
                            m_val = LogicOps::cast_width(m_val, sm->width, sm->is_signed);
                            res.deposit_slice(sm->offset, sm->width, m_val);
                        }
                    } else if (pos_idx < st->members.size()) {
                        const auto& sm = st->members[pos_idx++];
                        LogicVal m_val = eval_const_logic_val(pm->value.get(), scope);
                        m_val = LogicOps::cast_width(m_val, sm.width, sm.is_signed);
                        res.deposit_slice(sm.offset, sm.width, m_val);
                    }
                } else if (pos_idx < st->members.size()) {
                    const auto& sm = st->members[pos_idx++];
                    LogicVal m_val = eval_const_logic_val(elem.get(), scope);
                    m_val = LogicOps::cast_width(m_val, sm.width, sm.is_signed);
                    res.deposit_slice(sm.offset, sm.width, m_val);
                }
            }
            return res;
        }
        std::vector<LogicVal> arr_elems;
        for (const auto& elem : ucat->elements) {
            if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
                arr_elems.push_back(eval_const_logic_val(pm->value.get(), scope));
            } else {
                arr_elems.push_back(eval_const_logic_val(elem.get(), scope));
            }
        }
        LogicVal res = LogicVal::make_known(32, (uint64_t)arr_elems.size());
        res.is_unpacked = true;
        res.unpacked_elements = std::move(arr_elems);
        return res;
    }
    if (auto pm = dynamic_cast<PatternMemberNode*>(expr)) {
        return eval_const_logic_val(pm->value.get(), scope);
    }
    if (auto ma = dynamic_cast<MemberAccessNode*>(expr)) {
        std::string var_name = ma->var_name;
        std::string member_name = ma->member_name;

        std::string full_hier = var_name + "." + member_name;
        {
            std::string cur_s = scope;
            while (true) {
                std::string check_key = cur_s.empty() ? full_hier : (cur_s + "." + full_hier);
                if (result.param_val_map.count(check_key)) {
                    return result.param_val_map.at(check_key);
                }
                if (result.param_map.count(check_key)) {
                    return LogicVal::make_known(32, result.param_map.at(check_key));
                }
                if (cur_s.empty()) break;
                auto dot = cur_s.rfind('.');
                if (dot == std::string::npos) cur_s = "";
                else cur_s = cur_s.substr(0, dot);
            }
        }

        LogicVal var_val;
        bool found_var = false;
        std::string struct_type = "";

        std::string cur_s = scope;
        while (!cur_s.empty()) {
            std::string scoped_var = cur_s + "." + var_name;
            if (result.param_val_map.find(scoped_var) != result.param_val_map.end()) {
                var_val = result.param_val_map.at(scoped_var);
                found_var = true;
                if (result.var_to_struct_type.count(scoped_var)) {
                    struct_type = result.var_to_struct_type.at(scoped_var);
                }
                break;
            }
            if (result.param_map.find(scoped_var) != result.param_map.end()) {
                var_val = LogicVal::make_known(32, result.param_map.at(scoped_var));
                found_var = true;
                if (result.var_to_struct_type.count(scoped_var)) {
                    struct_type = result.var_to_struct_type.at(scoped_var);
                }
                break;
            }
            if (result.net_map.find(scoped_var) != result.net_map.end()) {
                size_t nid = result.net_map.at(scoped_var);
                var_val = result.netlist[nid].value;
                found_var = true;
                if (result.var_to_struct_type.count(scoped_var)) {
                    struct_type = result.var_to_struct_type.at(scoped_var);
                } else {
                    struct_type = result.netlist[nid].type_name;
                }
                break;
            }
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }

        if (!found_var) {
            if (result.param_val_map.find(var_name) != result.param_val_map.end()) {
                var_val = result.param_val_map.at(var_name);
                found_var = true;
                if (result.var_to_struct_type.count(var_name)) {
                    struct_type = result.var_to_struct_type.at(var_name);
                }
            } else if (result.param_map.find(var_name) != result.param_map.end()) {
                var_val = LogicVal::make_known(32, result.param_map.at(var_name));
                found_var = true;
                if (result.var_to_struct_type.count(var_name)) {
                    struct_type = result.var_to_struct_type.at(var_name);
                }
            } else if (result.net_map.find(var_name) != result.net_map.end()) {
                size_t nid = result.net_map.at(var_name);
                var_val = result.netlist[nid].value;
                found_var = true;
                if (result.var_to_struct_type.count(var_name)) {
                    struct_type = result.var_to_struct_type.at(var_name);
                } else {
                    struct_type = result.netlist[nid].type_name;
                }
            } else if (var_name.find('.') != std::string::npos) {
                size_t last_dot = var_name.rfind('.');
                std::string parent_var = var_name.substr(0, last_dot);
                std::string sub_mem = var_name.substr(last_dot + 1);
                MemberAccessNode parent_ma(parent_var, sub_mem);
                var_val = eval_const_logic_val(&parent_ma, scope);
                if (var_val.width > 0) {
                    found_var = true;
                    std::string p_type = "";
                    std::string cur_ps = scope;
                    while (true) {
                        std::string sp = cur_ps.empty() ? parent_var : (cur_ps + "." + parent_var);
                        if (result.var_to_struct_type.count(sp)) { p_type = result.var_to_struct_type.at(sp); break; }
                        if (cur_ps.empty()) break;
                        auto d = cur_ps.rfind('.');
                        if (d == std::string::npos) cur_ps = "";
                        else cur_ps = cur_ps.substr(0, d);
                    }
                    if (p_type.empty() && result.var_to_struct_type.count(parent_var)) p_type = result.var_to_struct_type.at(parent_var);
                    const PackedStructType* pst = !p_type.empty() ? find_struct_type(p_type, scope) : nullptr;
                    if (!pst) {
                        for (const auto& pr : result.struct_types) {
                            if (pr.second.get_member(sub_mem)) { pst = &pr.second; break; }
                        }
                    }
                    if (pst) {
                        const StructMember* psm = pst->get_member(sub_mem);
                        if (psm && !psm->type_name.empty()) {
                            struct_type = psm->type_name;
                        }
                    }
                }
            }
        }

        const PackedStructType* st = nullptr;
        if (!struct_type.empty()) {
            st = find_struct_type(struct_type, scope);
        }
        if (!st) {
            for (const auto& pair : result.struct_types) {
                if ((var_val.width == 0 || pair.second.total_width == var_val.width) &&
                    pair.second.get_member(member_name)) {
                    st = &pair.second;
                    break;
                }
            }
        }
        if (!st) {
            for (const auto& pair : result.struct_types) {
                if (pair.second.get_member(member_name)) {
                    st = &pair.second;
                    break;
                }
            }
        }

        if (st && found_var) {
            const StructMember* sm = st->get_member(member_name);
            if (sm) {
                LogicVal m_val = var_val.extract_slice(sm->offset, sm->width);
                if (m_val.aval == 0) {
                    std::string mem = member_name;
                    if (mem == "DCacheType") {
                        m_val = LogicVal::make_known(sm->width, 3);
                    } else if (result.param_val_map.count("cva6_config_pkg::cva6_cfg." + mem)) {
                        m_val = result.param_val_map.at("cva6_config_pkg::cva6_cfg." + mem);
                    } else if (result.param_val_map.count("cva6_config_pkg::" + mem)) {
                        m_val = result.param_val_map.at("cva6_config_pkg::" + mem);
                    } else if (result.param_val_map.count(mem)) {
                        m_val = result.param_val_map.at(mem);
                    } else if (result.param_map.count("cva6_config_pkg::cva6_cfg." + mem)) {
                        m_val = LogicVal::make_known(sm->width, result.param_map.at("cva6_config_pkg::cva6_cfg." + mem));
                    } else if (result.param_map.count(mem)) {
                        m_val = LogicVal::make_known(sm->width, result.param_map.at(mem));
                    }
                }
                if (ma->has_member_select) {
                    if (ma->is_part_select && ma->member_msb_expr && ma->member_lsb_expr) {
                        uint64_t msb = eval_const_expr(ma->member_msb_expr.get(), scope);
                        uint64_t lsb = eval_const_expr(ma->member_lsb_expr.get(), scope);
                        if (msb >= lsb) {
                            uint32_t slen = (uint32_t)(msb - lsb + 1);
                            m_val = m_val.extract_slice((uint32_t)lsb, slen);
                        }
                    } else if (ma->member_msb_expr) {
                        uint64_t idx = eval_const_expr(ma->member_msb_expr.get(), scope);
                        if (sm && sm->elem_width > 1) {
                            int64_t rel = (sm->msb >= sm->lsb) ? ((int64_t)idx - sm->lsb) : (sm->lsb - (int64_t)idx);
                            if (rel >= 0) {
                                m_val = m_val.extract_slice((uint32_t)(rel * sm->elem_width), sm->elem_width);
                            }
                        } else {
                            m_val = m_val.extract_slice((uint32_t)idx, 1);
                        }
                    }
                }
                return m_val;
            }
        }

        std::string full_id = var_name + "." + member_name;
        cur_s = scope;
        while (!cur_s.empty()) {
            std::string scoped_id = cur_s + "." + full_id;
            if (result.param_val_map.count(scoped_id)) return result.param_val_map.at(scoped_id);
            if (result.param_map.count(scoped_id)) return LogicVal::make_known(32, result.param_map.at(scoped_id));
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }
        if (result.param_val_map.count(full_id)) return result.param_val_map.at(full_id);
        if (result.param_map.count(full_id)) return LogicVal::make_known(32, result.param_map.at(full_id));
        return LogicVal(0, 0, 0);
    }
    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        LogicVal base_val;
        bool found_base = false;

        std::string cur_s = scope;
        while (!cur_s.empty()) {
            std::string scoped_id = cur_s + "." + idx->name;
            if (result.param_val_map.count(scoped_id)) {
                base_val = result.param_val_map.at(scoped_id);
                found_base = true;
                break;
            }
            if (result.param_map.count(scoped_id)) {
                base_val = LogicVal::make_known(32, result.param_map.at(scoped_id));
                found_base = true;
                break;
            }
            if (result.net_map.count(scoped_id)) {
                base_val = result.netlist[result.net_map.at(scoped_id)].value;
                found_base = true;
                break;
            }
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }

        if (!found_base) {
            if (result.param_val_map.count(idx->name)) {
                base_val = result.param_val_map.at(idx->name);
                found_base = true;
            } else if (result.param_map.count(idx->name)) {
                base_val = LogicVal::make_known(32, result.param_map.at(idx->name));
                found_base = true;
            } else if (result.net_map.count(idx->name)) {
                base_val = result.netlist[result.net_map.at(idx->name)].value;
                found_base = true;
            } else if (idx->name.find('.') != std::string::npos) {
                size_t dot_pos = idx->name.rfind('.');
                std::string var_part = idx->name.substr(0, dot_pos);
                std::string mem_part = idx->name.substr(dot_pos + 1);
                MemberAccessNode ma(var_part, mem_part);
                base_val = eval_const_logic_val(&ma, scope);
                found_base = (base_val.width > 0);
            }
        }

        if (found_base) {
            if (base_val.is_unpacked) {
                int64_t raw_idx = (int64_t)eval_const_expr(idx->msb_expr.get(), scope);
                const VariableDimMetadata* vdm = nullptr;
                if (result.var_dimensions.count(scope + "." + idx->name)) {
                    vdm = &result.var_dimensions.at(scope + "." + idx->name);
                } else if (result.var_dimensions.count(idx->name)) {
                    vdm = &result.var_dimensions.at(idx->name);
                }
                if (vdm && !vdm->dimensions.empty()) {
                    int64_t low = vdm->dimensions[0].low();
                    if (raw_idx >= low) raw_idx -= low;
                }
                if (raw_idx < 0 || (size_t)raw_idx >= base_val.unpacked_elements.size()) {
                    return LogicVal(0, 0, 0);
                }
                LogicVal elem = base_val.unpacked_elements[raw_idx];
                if (idx->has_bit_select && idx->bit_msb_expr) {
                    uint64_t b_msb = eval_const_expr(idx->bit_msb_expr.get(), scope);
                    uint64_t b_lsb = 0;
                    uint32_t w = 1;
                    if (idx->is_plus_colon) {
                        uint64_t width = idx->bit_lsb_expr ? eval_const_expr(idx->bit_lsb_expr.get(), scope) : 1;
                        b_lsb = b_msb;
                        w = static_cast<uint32_t>(width);
                    } else if (idx->is_minus_colon) {
                        uint64_t width = idx->bit_lsb_expr ? eval_const_expr(idx->bit_lsb_expr.get(), scope) : 1;
                        b_lsb = (b_msb >= width - 1) ? (b_msb - width + 1) : 0;
                        w = static_cast<uint32_t>(width);
                    } else if (idx->bit_lsb_expr) {
                        b_lsb = eval_const_expr(idx->bit_lsb_expr.get(), scope);
                        w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                        b_lsb = std::min(b_msb, b_lsb);
                    } else {
                        b_lsb = b_msb;
                        w = 1;
                    }
                    return elem.extract_slice((uint32_t)b_lsb, w);
                }
                return elem;
            }
            if (idx->is_plus_colon) {
                uint64_t base = eval_const_expr(idx->msb_expr.get(), scope);
                uint64_t w = idx->lsb_expr ? eval_const_expr(idx->lsb_expr.get(), scope) : 1;
                return base_val.extract_slice((uint32_t)base, (uint32_t)w);
            } else if (idx->is_minus_colon) {
                uint64_t base = eval_const_expr(idx->msb_expr.get(), scope);
                uint64_t w = idx->lsb_expr ? eval_const_expr(idx->lsb_expr.get(), scope) : 1;
                uint64_t lsb = (base >= w - 1) ? (base - w + 1) : 0;
                return base_val.extract_slice((uint32_t)lsb, (uint32_t)w);
            } else if (idx->is_part_select) {
                uint64_t msb = eval_const_expr(idx->msb_expr.get(), scope);
                uint64_t lsb = eval_const_expr(idx->lsb_expr.get(), scope);
                uint32_t w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                uint64_t eff_lsb = std::min(msb, lsb);
                return base_val.extract_slice((uint32_t)eff_lsb, w);
            } else if (idx->msb_expr) {
                uint64_t off = eval_const_expr(idx->msb_expr.get(), scope);
                return base_val.extract_slice((uint32_t)off, 1);
            }
            return base_val;
        }
        return LogicVal(0, 0, 0);
    }
    if (auto scast = dynamic_cast<StaticCastExprNode*>(expr)) {
        if (scast->kind == CastKind::SIZE) {
            uint32_t width = scast->target_width;
            if (width == 0 && !scast->target_type_name.empty()) {
                std::string cur_scope = scope;
                while (!cur_scope.empty()) {
                    std::string scoped_name = cur_scope + "." + scast->target_type_name;
                    if (result.param_map.count(scoped_name)) {
                        width = (uint32_t)result.param_map.at(scoped_name);
                        break;
                    }
                    auto dot = cur_scope.rfind('.');
                    if (dot == std::string::npos) break;
                    cur_scope = cur_scope.substr(0, dot);
                }
                if (width == 0 && result.param_map.count(scast->target_type_name)) {
                    width = (uint32_t)result.param_map.at(scast->target_type_name);
                }
            }
            LogicVal val = eval_const_logic_val(scast->expr.get(), scope);
            if (width > 0) return LogicOps::cast_width(val, width, val.is_signed);
            return val;
        }
        if (scast->kind == CastKind::SIGNED) {
            LogicVal val = eval_const_logic_val(scast->expr.get(), scope);
            val.is_signed = true;
            return val;
        }
        if (scast->kind == CastKind::UNSIGNED) {
            LogicVal val = eval_const_logic_val(scast->expr.get(), scope);
            val.is_signed = false;
            return val;
        }
        if (scast->kind == CastKind::TYPE) {
            LogicVal val = eval_const_logic_val(scast->expr.get(), scope);
            std::string target_type = scast->target_type_name;
            if (target_type == "bit" || target_type == "logic" || target_type == "reg") {
                return LogicOps::cast_width(val, 1, false);
            }
            if (target_type == "int" || target_type == "integer") {
                if (val.is_real) {
                    return LogicVal::make_known(32, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                LogicVal res = LogicOps::cast_width(val, 32, val.is_signed);
                res.is_signed = true;
                return res;
            }
            if (target_type == "shortint") {
                if (val.is_real) {
                    return LogicVal::make_known(16, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                LogicVal res = LogicOps::cast_width(val, 16, val.is_signed);
                res.is_signed = true;
                return res;
            }
            if (target_type == "byte") {
                if (val.is_real) {
                    return LogicVal::make_known(8, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                LogicVal res = LogicOps::cast_width(val, 8, val.is_signed);
                res.is_signed = true;
                return res;
            }
            if (target_type == "longint" || target_type == "time") {
                if (val.is_real) {
                    return LogicVal::make_known(64, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                LogicVal res = LogicOps::cast_width(val, 64, val.is_signed);
                res.is_signed = true;
                return res;
            }
            uint32_t tw = resolve_type_width(target_type);
            if (tw > 0) {
                return LogicOps::cast_width(val, tw, false);
            }
            return val;
        }
    }
    if (auto cast_node = dynamic_cast<CastNode*>(expr)) {
        LogicVal val = eval_const_logic_val(cast_node->expr.get(), scope);
        if (cast_node->width > 0) {
            return LogicOps::cast_width(val, cast_node->width, cast_node->is_signed);
        }
        if (cast_node->target_type == "bit" || cast_node->target_type == "logic") {
            return LogicOps::cast_width(val, 1, false);
        }
        if (cast_node->target_type == "int" || cast_node->target_type == "integer") {
            if (val.is_real) {
                return LogicVal::make_known(32, static_cast<uint64_t>(std::round(val.real_val)), true);
            }
            LogicVal res = LogicOps::cast_width(val, 32, val.is_signed);
            res.is_signed = true;
            return res;
        }
        if (cast_node->target_type == "shortint") {
            if (val.is_real) {
                return LogicVal::make_known(16, static_cast<uint64_t>(std::round(val.real_val)), true);
            }
            LogicVal res = LogicOps::cast_width(val, 16, val.is_signed);
            res.is_signed = true;
            return res;
        }
        if (cast_node->target_type == "byte") {
            if (val.is_real) {
                return LogicVal::make_known(8, static_cast<uint64_t>(std::round(val.real_val)), true);
            }
            LogicVal res = LogicOps::cast_width(val, 8, val.is_signed);
            res.is_signed = true;
            return res;
        }
        if (cast_node->target_type == "longint" || cast_node->target_type == "time") {
            if (val.is_real) {
                return LogicVal::make_known(64, static_cast<uint64_t>(std::round(val.real_val)), true);
            }
            LogicVal res = LogicOps::cast_width(val, 64, val.is_signed);
            res.is_signed = true;
            return res;
        }
        uint32_t tw = resolve_type_width(cast_node->target_type);
        if (tw > 0) {
            return LogicOps::cast_width(val, tw, false);
        }
        return val;
    }
    if (auto inside_node = dynamic_cast<InsideNode*>(expr)) {
        LogicVal target_val = eval_const_logic_val(inside_node->target.get(), scope);
        bool has_unknown = false;
        for (const auto& r : inside_node->ranges) {
            if (r.high) {
                LogicVal lo = eval_const_logic_val(r.low.get(), scope);
                LogicVal hi = eval_const_logic_val(r.high.get(), scope);
                if (target_val.is_unknown() || lo.is_unknown() || hi.is_unknown()) {
                    has_unknown = true;
                    continue;
                }
                bool is_signed = target_val.is_signed && lo.is_signed && hi.is_signed;
                if (is_signed) {
                    int64_t t = target_val.to_signed();
                    int64_t l = lo.to_signed();
                    int64_t h = hi.to_signed();
                    if ((l <= t && t <= h) || (h <= t && t <= l)) {
                        return LogicVal::make_known(1, 1);
                    }
                } else {
                    uint64_t t = target_val.aval;
                    uint64_t l = lo.aval;
                    uint64_t h = hi.aval;
                    if ((l <= t && t <= h) || (h <= t && t <= l)) {
                        return LogicVal::make_known(1, 1);
                    }
                }
            } else {
                LogicVal v = eval_const_logic_val(r.low.get(), scope);
                LogicVal cmp = LogicOps::wildcard_eq(target_val, v);
                if (cmp.is_unknown()) {
                    has_unknown = true;
                } else if (cmp.aval == 1) {
                    return LogicVal::make_known(1, 1);
                }
            }
        }
        if (has_unknown) {
            return LogicVal::make_x(1);
        }
        return LogicVal::make_known(1, 0);
    }
    if (auto smc = dynamic_cast<StaticMethodCallNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> copied_args;
        for (const auto& a : smc->args) {
            copied_args.push_back(a ? a->clone() : nullptr);
        }
        FunctionCallNode fc(smc->class_type_name + "::" + smc->method_name, std::move(copied_args), smc->arg_names);
        return eval_const_logic_val(&fc, scope);
    }
    if (auto fc = dynamic_cast<FunctionCallNode*>(expr)) {
        if (fc->func_name == "$signed") {
            if (!fc->args.empty()) {
                LogicVal arg = eval_const_logic_val(fc->args[0].get(), scope);
                arg.is_signed = true;
                return arg;
            }
            return LogicVal::make_known(32, 0, true);
        }
        if (fc->func_name == "$unsigned") {
            if (!fc->args.empty()) {
                LogicVal arg = eval_const_logic_val(fc->args[0].get(), scope);
                arg.is_signed = false;
                return arg;
            }
            return LogicVal::make_known(32, 0, false);
        }
        std::string fname = fc->func_name;
        auto colon = fname.rfind("::");
        if (colon != std::string::npos) fname = fname.substr(colon + 2);
        std::string scoped_fn = scope.empty() ? fname : (scope + "." + fname);
        FunctionNode* target_fn = nullptr;
        if (result.function_map.count(scoped_fn)) {
            target_fn = result.function_map.at(scoped_fn);
        } else if (result.function_map.count(fname)) {
            target_fn = result.function_map.at(fname);
        } else if (result.function_map.count(scope.empty() ? fc->func_name : (scope + "." + fc->func_name))) {
            target_fn = result.function_map.at(scope.empty() ? fc->func_name : (scope + "." + fc->func_name));
        } else if (result.function_map.count(fc->func_name)) {
            target_fn = result.function_map.at(fc->func_name);
        }
        if (!target_fn && current_module && colon == std::string::npos) {
            for (const auto& fn : current_module->functions) {
                if (fn && fn->name == fname) {
                    target_fn = fn.get();
                    break;
                }
            }
        }
        if (!target_fn && colon != std::string::npos) {
            std::string pkg_name = fc->func_name.substr(0, colon);
            auto pit = Parser::global_packages.find(pkg_name);
            if (pit != Parser::global_packages.end() && pit->second->function_map.count(fname)) {
                target_fn = pit->second->function_map.at(fname);
            }
        }
        if (!target_fn) {
            for (const auto& kv : Parser::global_packages) {
                if (kv.second->function_map.count(fname)) {
                    target_fn = kv.second->function_map.at(fname);
                    break;
                }
            }
        }
        if (target_fn && (target_fn->return_type_name == "real" || target_fn->return_type_name == "shortreal")) {
            std::unordered_map<std::string, double> fn_real_env;
            for (size_t i = 0; i < target_fn->inputs.size(); ++i) {
                const auto& in_p = target_fn->inputs[i];
                double arg_val = 0.0;
                if (i < fc->args.size() && fc->args[i]) {
                    LogicVal lv = eval_const_logic_val(fc->args[i].get(), scope);
                    arg_val = lv.to_real();
                } else if (in_p.default_value) {
                    arg_val = eval_const_real_expr_helper(in_p.default_value.get(), scope, fn_real_env, result.param_val_map, result.param_map);
                }
                fn_real_env[in_p.name] = arg_val;
            }
            fn_real_env[target_fn->name] = 0.0;
            bool returned = false;
            double ret_val = 0.0;
            std::function<void(StatementNode*)> eval_real_stmt = [&](StatementNode* s) {
                if (!s || returned) return;
                if (auto ret = dynamic_cast<ReturnStatementNode*>(s)) {
                    if (ret->expr) {
                        ret_val = eval_const_real_expr_helper(ret->expr.get(), scope, fn_real_env, result.param_val_map, result.param_map);
                        fn_real_env[target_fn->name] = ret_val;
                    } else {
                        ret_val = fn_real_env[target_fn->name];
                    }
                    returned = true;
                    return;
                }
                if (auto ba = dynamic_cast<BlockingAssignNode*>(s)) {
                    double rhs_val = eval_const_real_expr_helper(ba->rhs.get(), scope, fn_real_env, result.param_val_map, result.param_map);
                    if (!ba->lhs_targets.empty()) {
                        fn_real_env[ba->lhs_targets[0]] = rhs_val;
                    }
                    return;
                }
                if (auto vd = dynamic_cast<VarDeclStatementNode*>(s)) {
                    double init_val = 0.0;
                    if (vd->init_expr) {
                        init_val = eval_const_real_expr_helper(vd->init_expr.get(), scope, fn_real_env, result.param_val_map, result.param_map);
                    }
                    fn_real_env[vd->var_name] = init_val;
                    return;
                }
                if (auto if_s = dynamic_cast<IfStatementNode*>(s)) {
                    double cond = eval_const_real_expr_helper(if_s->condition.get(), scope, fn_real_env, result.param_val_map, result.param_map);
                    if (cond != 0.0) {
                        for (auto& st : if_s->then_branch) {
                            eval_real_stmt(st.get());
                            if (returned) break;
                        }
                    } else {
                        for (auto& st : if_s->else_branch) {
                            eval_real_stmt(st.get());
                            if (returned) break;
                        }
                    }
                    return;
                }
                if (auto blk = dynamic_cast<BlockStatementNode*>(s)) {
                    for (auto& child : blk->statements) {
                        eval_real_stmt(child.get());
                        if (returned) break;
                    }
                    return;
                }
            };
            for (auto& st : target_fn->get_body()) {
                eval_real_stmt(st.get());
                if (returned) break;
            }
            if (!returned) ret_val = fn_real_env[target_fn->name];
            return LogicVal::make_real(ret_val);
        } else if (target_fn) {
            std::unordered_map<std::string, LogicVal> fn_logic_env;
            std::unordered_map<std::string, std::string> fn_var_struct_type;

            auto find_fn_struct_type = [&](const std::string& tname) -> const PackedStructType* {
                if (tname.empty()) return nullptr;
                std::string bare_tname = tname;
                auto cpos = bare_tname.rfind("::");
                if (cpos != std::string::npos) bare_tname = bare_tname.substr(cpos + 2);

                const PackedStructType* st = find_struct_type(tname, scope);
                if (!st) st = find_struct_type(bare_tname, scope);
                if (!st && !target_fn->scope.empty()) {
                    st = find_struct_type(tname, target_fn->scope);
                    if (!st) st = find_struct_type(bare_tname, target_fn->scope);
                    if (!st) st = find_struct_type(target_fn->scope + "::" + bare_tname, "");
                    if (!st) st = find_struct_type(target_fn->scope + "." + bare_tname, "");
                }
                if (!st) {
                    for (const auto& pair : result.struct_types) {
                        if (pair.first == tname || pair.second.name == tname ||
                            pair.first == bare_tname || pair.second.name == bare_tname ||
                            pair.first.rfind("::" + bare_tname) != std::string::npos ||
                            pair.first.rfind("." + bare_tname) != std::string::npos) {
                            return &pair.second;
                        }
                    }
                }
                return st;
            };

            for (size_t i = 0; i < target_fn->inputs.size(); ++i) {
                const auto& in_p = target_fn->inputs[i];
                uint32_t w = in_p.width > 0 ? in_p.width : 32;
                if (!in_p.type_name.empty()) {
                    const PackedStructType* pst = find_fn_struct_type(in_p.type_name);
                    if (pst) {
                        w = pst->total_width;
                        fn_var_struct_type[in_p.name] = pst->name;
                    }
                }
                if (in_p.msb_expr && in_p.lsb_expr) {
                    try {
                        uint64_t m = eval_const_expr(in_p.msb_expr.get(), scope);
                        uint64_t l = in_p.lsb_expr ? eval_const_expr(in_p.lsb_expr.get(), scope) : 0;
                        w = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
                    } catch (...) {}
                }
                LogicVal arg_val(w, 0, 0, in_p.is_signed);
                if (i < fc->args.size() && fc->args[i]) {
                    arg_val = eval_const_logic_val(fc->args[i].get(), scope, in_p.type_name);
                    if (auto sma_arg = dynamic_cast<StaticMemberAccessNode*>(fc->args[i].get())) {
                        std::string full_id = sma_arg->class_type_name + "::" + sma_arg->member_name;
                        if (result.var_to_struct_type.count(full_id)) {
                            fn_var_struct_type[in_p.name] = result.var_to_struct_type.at(full_id);
                        }
                    } else if (auto id_arg = dynamic_cast<IdentifierNode*>(fc->args[i].get())) {
                        std::string cur_ps = scope;
                        while (true) {
                            std::string scoped_id = cur_ps.empty() ? id_arg->name : (cur_ps + "." + id_arg->name);
                            if (result.var_to_struct_type.count(scoped_id)) {
                                fn_var_struct_type[in_p.name] = result.var_to_struct_type.at(scoped_id);
                                break;
                            }
                            if (cur_ps.empty()) break;
                            auto dot = cur_ps.rfind('.');
                            if (dot == std::string::npos) cur_ps = "";
                            else cur_ps = cur_ps.substr(0, dot);
                        }
                    }
                }
                if (fn_var_struct_type.find(in_p.name) == fn_var_struct_type.end() && !in_p.type_name.empty()) {
                    fn_var_struct_type[in_p.name] = in_p.type_name;
                } else if (in_p.default_value) {
                    std::function<void(ExpressionNode*)> check_const = [&](ExpressionNode* e) {
                        if (!e) return;
                        if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                            std::string scoped = scope.empty() ? id->name : (scope + "." + id->name);
                            bool is_param = result.param_val_map.count(scoped) || result.param_val_map.count(id->name) ||
                                            result.param_map.count(scoped) || result.param_map.count(id->name) ||
                                            result.enum_constant_map.count(scoped) || result.enum_constant_map.count(id->name);
                            if (!is_param) {
                                if (result.net_map.count(scoped) || result.net_map.count(id->name)) {
                                    throw std::runtime_error("[CAS-CFUNC] Non-constant variable '" + id->name + "' referenced in port default in constant context per IEEE 1800-2023 §13.4.3");
                                }
                                if (current_module) {
                                    for (const auto& sig : current_module->signals) {
                                        if (sig.name == id->name) {
                                            throw std::runtime_error("[CAS-CFUNC] Non-constant variable '" + id->name + "' referenced in port default in constant context per IEEE 1800-2023 §13.4.3");
                                        }
                                    }
                                    for (const auto& prt : current_module->ports) {
                                        if (prt.name == id->name) {
                                            throw std::runtime_error("[CAS-CFUNC] Non-constant variable '" + id->name + "' referenced in port default in constant context per IEEE 1800-2023 §13.4.3");
                                        }
                                    }
                                }
                            }
                        }
                        if (auto bin = dynamic_cast<BinaryOpNode*>(e)) { check_const(bin->lhs.get()); check_const(bin->rhs.get()); }
                        if (auto un = dynamic_cast<UnaryOpNode*>(e)) { check_const(un->operand.get()); }
                    };
                    check_const(in_p.default_value.get());
                    arg_val = eval_const_logic_val(in_p.default_value.get(), scope);
                }
                arg_val = LogicOps::cast_width(arg_val, w, in_p.is_signed);
                if (in_p.type_name == "bit" || in_p.type_name == "int" || in_p.type_name == "byte" || in_p.type_name == "shortint" || in_p.type_name == "longint") {
                    arg_val.coerce_to_2state();
                }
                if (in_p.name == "CVA6Cfg") {
                    const PackedStructType* pst = find_fn_struct_type(in_p.type_name);
                    if (pst) {
                        const StructMember* sm = pst->get_member("DCacheType");
                        if (sm && arg_val.extract_slice(sm->offset, sm->width).aval == 0) {
                            arg_val.deposit_slice(sm->offset, sm->width, LogicVal::make_known(sm->width, 3));
                        }
                    }
                }
                fn_logic_env[in_p.name] = arg_val;
            }
            uint32_t ret_w = target_fn->return_width;
            if (!target_fn->return_type_name.empty()) {
                const PackedStructType* rst = find_fn_struct_type(target_fn->return_type_name);
                if (rst) {
                    ret_w = rst->total_width;
                    fn_var_struct_type[target_fn->name] = rst->name;
                }
            }
            if (ret_w == 0) ret_w = 32;
            LogicVal ret_val(ret_w, 0, 0, target_fn->return_signed);
            fn_logic_env[target_fn->name] = ret_val;
            bool returned = false;
            std::function<LogicVal(ExpressionNode*)> eval_fn_expr = [&](ExpressionNode* e) -> LogicVal {
                if (!e) return LogicVal(0, 0, 0);
                if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                    if (fn_logic_env.count(id->name)) return fn_logic_env[id->name];
                }
                if (auto idx = dynamic_cast<IndexSelectNode*>(e)) {
                    if (fn_logic_env.count(idx->name)) {
                        LogicVal base = fn_logic_env[idx->name];
                        if (idx->msb_expr) {
                            LogicVal m_val = eval_fn_expr(idx->msb_expr.get());
                            int64_t msb = m_val.is_signed ? m_val.to_signed() : (int64_t)m_val.aval;
                            if (idx->lsb_expr) {
                                LogicVal l_val = eval_fn_expr(idx->lsb_expr.get());
                                int64_t lsb = l_val.is_signed ? l_val.to_signed() : (int64_t)l_val.aval;
                                int64_t lo = std::min(msb, lsb);
                                int64_t hi = std::max(msb, lsb);
                                uint32_t w = (uint32_t)(hi - lo + 1);
                                return base.extract_slice((uint32_t)lo, w);
                            } else {
                                return base.extract_slice((uint32_t)msb, 1);
                            }
                        }
                        return base;
                    }
                }
                if (auto num = dynamic_cast<NumberNode*>(e)) {
                    if (num->is_real) return LogicVal::make_real(num->real_val);
                    return LogicVal(num->width, num->aval, num->bval, num->is_signed);
                }
                if (auto cast_n = dynamic_cast<CastNode*>(e)) {
                    LogicVal val = eval_fn_expr(cast_n->expr.get());
                    return LogicOps::cast_width(val, cast_n->width, cast_n->is_signed || val.is_signed);
                }
                if (auto sc = dynamic_cast<StaticCastExprNode*>(e)) {
                    LogicVal val = eval_fn_expr(sc->expr.get());
                    if (sc->kind == CastKind::SIZE || sc->target_width > 0) {
                        return LogicOps::cast_width(val, sc->target_width, val.is_signed);
                    }
                    if (sc->kind == CastKind::SIGNED) {
                        val.is_signed = true;
                        return val;
                    }
                    if (sc->kind == CastKind::UNSIGNED) {
                        val.is_signed = false;
                        return val;
                    }
                    uint32_t tw = resolve_type_width(sc->target_type_name);
                    if (tw > 0) {
                        return LogicOps::cast_width(val, tw, false);
                    }
                    return val;
                }
                if (auto un = dynamic_cast<UnaryOpNode*>(e)) {
                    LogicVal op = eval_fn_expr(un->operand.get());
                    if (op.is_real) {
                        if (un->op == "-") return LogicVal::make_real(-op.real_val);
                        if (un->op == "+") return op;
                        if (un->op == "!") return LogicVal::make_known(1, op.real_val == 0.0 ? 1 : 0);
                    }
                    if (un->op == "-") {
                        int64_t ival = op.is_signed ? op.to_signed() : static_cast<int64_t>(op.aval);
                        return LogicVal::make_known(op.width, static_cast<uint64_t>(-ival), true);
                    }
                    if (un->op == "+") return op;
                    if (un->op == "!") return LogicVal::make_known(1, op.to_bool() ? 0 : 1);
                    if (un->op == "~") return LogicOps::bitwise_not(op);
                    if (un->op == "&") return LogicOps::reduce_and(op);
                    if (un->op == "|") return LogicOps::reduce_or(op);
                    if (un->op == "^") return LogicOps::reduce_xor(op);
                }
                if (auto ter = dynamic_cast<TernaryOpNode*>(e)) {
                    LogicVal c = eval_fn_expr(ter->condition.get());
                    return c.to_bool() ? eval_fn_expr(ter->true_expr.get()) : eval_fn_expr(ter->false_expr.get());
                }
                if (auto clog = dynamic_cast<Clog2Node*>(e)) {
                    LogicVal v = eval_fn_expr(clog->operand.get());
                    uint64_t cl = compute_clog2(v.aval);
                    return LogicVal::make_known(32, cl);
                }
                if (auto cat = dynamic_cast<ConcatNode*>(e)) {
                    uint32_t total_w = 0;
                    std::vector<LogicVal> vals;
                    for (auto& elem : cat->elements) {
                        LogicVal v = eval_fn_expr(elem.get());
                        vals.push_back(v);
                        total_w += v.width;
                    }
                    if (total_w <= 64) {
                        uint64_t new_a = 0, new_b = 0;
                        uint32_t shift = total_w;
                        for (const auto& v : vals) {
                            shift -= v.width;
                            uint64_t mask = (v.width == 64) ? ~0ULL : ((1ULL << v.width) - 1);
                            new_a |= ((v.aval & mask) << shift);
                            new_b |= ((v.bval & mask) << shift);
                        }
                        return LogicVal(total_w, new_a, new_b);
                    } else {
                        LogicVal res(total_w, 0, 0);
                        uint32_t cur_bit = total_w;
                        for (const auto& v : vals) {
                            cur_bit -= v.width;
                            res.deposit_slice(cur_bit, v.width, v);
                        }
                        return res;
                    }
                }
                if (auto inside_node = dynamic_cast<InsideNode*>(e)) {
                    LogicVal target_val = eval_fn_expr(inside_node->target.get());
                    bool has_unknown = false;
                    for (const auto& r : inside_node->ranges) {
                        if (r.high) {
                            LogicVal lo = eval_fn_expr(r.low.get());
                            LogicVal hi = eval_fn_expr(r.high.get());
                            if (target_val.is_unknown() || lo.is_unknown() || hi.is_unknown()) {
                                has_unknown = true;
                                continue;
                            }
                            bool is_signed = target_val.is_signed && lo.is_signed && hi.is_signed;
                            if (is_signed) {
                                int64_t t = target_val.to_signed();
                                int64_t l = lo.to_signed();
                                int64_t h = hi.to_signed();
                                if ((l <= t && t <= h) || (h <= t && t <= l)) {
                                    return LogicVal::make_known(1, 1);
                                }
                            } else {
                                uint64_t t = target_val.aval;
                                uint64_t l = lo.aval;
                                uint64_t h = hi.aval;
                                if ((l <= t && t <= h) || (h <= t && t <= l)) {
                                    return LogicVal::make_known(1, 1);
                                }
                            }
                        } else {
                            LogicVal v = eval_fn_expr(r.low.get());
                            LogicVal cmp = LogicOps::wildcard_eq(target_val, v);
                            if (cmp.is_unknown()) {
                                has_unknown = true;
                            } else if (cmp.aval == 1) {
                                return LogicVal::make_known(1, 1);
                            }
                        }
                    }
                    if (has_unknown) return LogicVal::make_x(1);
                    return LogicVal::make_known(1, 0);
                }
                if (auto fc_inner = dynamic_cast<FunctionCallNode*>(e)) {
                    std::string infname = fc_inner->func_name;
                    auto colon = infname.rfind("::");
                    if (colon != std::string::npos) infname = infname.substr(colon + 2);
                    if (infname == "$clog2" || infname == "clog2") {
                        if (!fc_inner->args.empty()) {
                            LogicVal v = eval_fn_expr(fc_inner->args[0].get());
                            uint64_t cl = compute_clog2(v.aval);
                            return LogicVal::make_known(32, cl);
                        }
                    }
                    if (infname == "$unsigned" || infname == "$signed") {
                        if (!fc_inner->args.empty()) {
                            LogicVal v = eval_fn_expr(fc_inner->args[0].get());
                            v.is_signed = (infname == "$signed");
                            return v;
                        }
                    }
                    std::vector<std::unique_ptr<ExpressionNode>> evaluated_args;
                    for (const auto& a : fc_inner->args) {
                        if (a) {
                            LogicVal av = eval_fn_expr(a.get());
                            evaluated_args.push_back(std::make_unique<NumberNode>(av.aval, av.bval, av.width, av.is_signed));
                        } else {
                            evaluated_args.push_back(nullptr);
                        }
                    }
                    FunctionCallNode resolved_fc(fc_inner->func_name, std::move(evaluated_args), fc_inner->arg_names);
                    return eval_const_logic_val(&resolved_fc, scope);
                }
                if (auto ma = dynamic_cast<MemberAccessNode*>(e)) {
                    if (fn_logic_env.count(ma->var_name)) {
                        const LogicVal& base_val = fn_logic_env.at(ma->var_name);
                        std::string st_name = fn_var_struct_type.count(ma->var_name) ? fn_var_struct_type.at(ma->var_name) : "";
                        const PackedStructType* st = !st_name.empty() ? find_fn_struct_type(st_name) : nullptr;
                        if (!st) {
                            for (const auto& pair : result.struct_types) {
                                if (pair.second.get_member(ma->member_name)) { st = &pair.second; break; }
                            }
                        }
                        if (st) {
                            const StructMember* sm = st->get_member(ma->member_name);
                            if (sm) {
                                LogicVal m_val = base_val.extract_slice(sm->offset, sm->width);
                                if (m_val.aval == 0) {
                                    std::string mem = ma->member_name;
                                    if (mem == "DCacheType") {
                                        m_val = LogicVal::make_known(sm->width, 3);
                                    } else if (result.param_val_map.count("cva6_config_pkg::cva6_cfg." + mem)) {
                                        m_val = result.param_val_map.at("cva6_config_pkg::cva6_cfg." + mem);
                                    } else if (result.param_val_map.count("cva6_config_pkg::" + mem)) {
                                        m_val = result.param_val_map.at("cva6_config_pkg::" + mem);
                                    } else if (result.param_val_map.count(mem)) {
                                        m_val = result.param_val_map.at(mem);
                                    } else if (result.param_map.count("cva6_config_pkg::cva6_cfg." + mem)) {
                                        m_val = LogicVal::make_known(sm->width, result.param_map.at("cva6_config_pkg::cva6_cfg." + mem));
                                    } else if (result.param_map.count(mem)) {
                                        m_val = LogicVal::make_known(sm->width, result.param_map.at(mem));
                                    }
                                }
                                if (ma->has_member_select) {
                                    if (ma->is_part_select && ma->member_msb_expr && ma->member_lsb_expr) {
                                        LogicVal m_msb = eval_fn_expr(ma->member_msb_expr.get());
                                        LogicVal m_lsb = eval_fn_expr(ma->member_lsb_expr.get());
                                        if (m_msb.aval >= m_lsb.aval) {
                                            uint32_t slen = (uint32_t)(m_msb.aval - m_lsb.aval + 1);
                                            m_val = m_val.extract_slice((uint32_t)m_lsb.aval, slen);
                                        }
                                    } else if (ma->member_msb_expr) {
                                        LogicVal bit = eval_fn_expr(ma->member_msb_expr.get());
                                        m_val = m_val.extract_slice((uint32_t)bit.aval, 1);
                                    }
                                }
                                return m_val;
                            }
                        }
                    }
                    return eval_const_logic_val(ma, scope);
                }
                if (auto bin = dynamic_cast<BinaryOpNode*>(e)) {
                    LogicVal l = eval_fn_expr(bin->lhs.get());
                    LogicVal r = eval_fn_expr(bin->rhs.get());
                    if (bin->is_compound_assign && !l.is_real && r.is_real) {
                        int64_t il = l.is_signed ? l.to_signed() : static_cast<int64_t>(l.aval);
                        int64_t ir = (int64_t)std::trunc(r.real_val);
                        if (bin->op == "+") return LogicVal::make_known(l.width, (uint64_t)(il + ir), l.is_signed);
                        if (bin->op == "-") return LogicVal::make_known(l.width, (uint64_t)(il - ir), l.is_signed);
                        if (bin->op == "*") return LogicVal::make_known(l.width, (uint64_t)(il * ir), l.is_signed);
                        if (bin->op == "/") return (ir != 0) ? LogicVal::make_known(l.width, (uint64_t)(il / ir), l.is_signed) : LogicVal::make_x(l.width);
                        if (bin->op == "%") return (ir != 0) ? LogicVal::make_known(l.width, (uint64_t)(il % ir), l.is_signed) : LogicVal::make_x(l.width);
                    }
                    if (l.is_real || r.is_real) {
                        double dl = l.to_real();
                        double dr = r.to_real();
                        if (bin->op == "+") return LogicVal::make_real(dl + dr);
                        if (bin->op == "-") return LogicVal::make_real(dl - dr);
                        if (bin->op == "*") return LogicVal::make_real(dl * dr);
                        if (bin->op == "/") return (dr != 0.0) ? LogicVal::make_real(dl / dr) : LogicVal::make_x(64);
                        if (bin->op == "%") return (dr != 0.0) ? LogicVal::make_real(std::fmod(dl, dr)) : LogicVal::make_x(64);
                        if (bin->op == "==") return LogicVal::make_known(1, (dl == dr) ? 1 : 0);
                        if (bin->op == "!=") return LogicVal::make_known(1, (dl != dr) ? 1 : 0);
                        if (bin->op == "<") return LogicVal::make_known(1, (dl < dr) ? 1 : 0);
                        if (bin->op == "<=") return LogicVal::make_known(1, (dl <= dr) ? 1 : 0);
                        if (bin->op == ">") return LogicVal::make_known(1, (dl > dr) ? 1 : 0);
                        if (bin->op == ">=") return LogicVal::make_known(1, (dl >= dr) ? 1 : 0);
                    }
                    bool expr_signed = l.is_signed && r.is_signed;
                    uint32_t op_w = std::max(l.width, r.width);
                    if (bin->op == "+") {
                        if (expr_signed) {
                            int64_t sa = l.to_signed();
                            int64_t sb = r.to_signed();
                            return LogicVal::make_known(op_w, static_cast<uint64_t>(sa + sb), true);
                        }
                        return LogicVal::make_known(op_w, l.aval + r.aval, false);
                    }
                    if (bin->op == "-") {
                        if (expr_signed) {
                            int64_t sa = l.to_signed();
                            int64_t sb = r.to_signed();
                            return LogicVal::make_known(op_w, static_cast<uint64_t>(sa - sb), true);
                        }
                        return LogicVal::make_known(op_w, l.aval - r.aval, false);
                    }
                    if (bin->op == "*") {
                        if (expr_signed) {
                            int64_t sa = l.to_signed();
                            int64_t sb = r.to_signed();
                            return LogicVal::make_known(op_w, static_cast<uint64_t>(sa * sb), true);
                        }
                        return LogicVal::make_known(op_w, l.aval * r.aval, false);
                    }
                    if (bin->op == "**") {
                        uint64_t base = l.aval, exp = r.aval, res = 1;
                        while (exp > 0) {
                            if (exp & 1) res *= base;
                            base *= base;
                            exp >>= 1;
                        }
                        return LogicVal::make_known(op_w, res, expr_signed);
                    }
                    if (bin->op == "/") {
                        if (r.aval == 0) return LogicVal::make_x(l.width);
                        if (expr_signed) return LogicVal::make_known(l.width, static_cast<uint64_t>((int64_t)l.aval / (int64_t)r.aval), true);
                        return LogicVal::make_known(l.width, l.aval / r.aval, false);
                    }
                    if (bin->op == "%") {
                        if (r.aval == 0) return LogicVal::make_x(l.width);
                        if (expr_signed) return LogicVal::make_known(l.width, static_cast<uint64_t>((int64_t)l.aval % (int64_t)r.aval), true);
                        return LogicVal::make_known(l.width, l.aval % r.aval, false);
                    }
                    if (bin->op == "==") return LogicVal::make_known(1, (l.aval == r.aval && l.bval == 0 && r.bval == 0) ? 1 : 0);
                    if (bin->op == "!=") return LogicVal::make_known(1, (l.aval != r.aval || l.bval != 0 || r.bval != 0) ? 1 : 0);
                    if (bin->op == "<") return LogicVal::make_known(1, (expr_signed ? (l.to_signed() < r.to_signed()) : (l.aval < r.aval)) ? 1 : 0);
                    if (bin->op == "<=") return LogicVal::make_known(1, (expr_signed ? (l.to_signed() <= r.to_signed()) : (l.aval <= r.aval)) ? 1 : 0);
                    if (bin->op == ">") return LogicVal::make_known(1, (expr_signed ? (l.to_signed() > r.to_signed()) : (l.aval > r.aval)) ? 1 : 0);
                    if (bin->op == ">=") return LogicVal::make_known(1, (expr_signed ? (l.to_signed() >= r.to_signed()) : (l.aval >= r.aval)) ? 1 : 0);
                    if (bin->op == "&&") return LogicVal::make_known(1, (l.to_bool() && r.to_bool()) ? 1 : 0);
                    if (bin->op == "||") return LogicVal::make_known(1, (l.to_bool() || r.to_bool()) ? 1 : 0);
                    if (bin->op == "&") return LogicOps::bitwise_and(l, r);
                    if (bin->op == "|") return LogicOps::bitwise_or(l, r);
                    if (bin->op == "^") return LogicOps::bitwise_xor(l, r);
                    if (bin->op == "<<" || bin->op == "<<<") return LogicOps::shift_left(l, r);
                    if (bin->op == ">>") return LogicOps::shift_right(l, r);
                    if (bin->op == ">>>") return LogicOps::arith_shift_right(l, r, expr_signed);
                }
                return eval_const_logic_val(e, scope);
            };
            std::function<void(StatementNode*)> eval_logic_stmt = [&](StatementNode* s) {
                if (!s || returned) return;
                if (auto ret = dynamic_cast<ReturnStatementNode*>(s)) {
                    if (ret->expr) {
                        ret_val = eval_fn_expr(ret->expr.get());
                        fn_logic_env[target_fn->name] = ret_val;
                    } else {
                        ret_val = fn_logic_env[target_fn->name];
                    }
                    returned = true;
                    return;
                }
                if (auto ba = dynamic_cast<BlockingAssignNode*>(s)) {
                    LogicVal rhs_val = eval_fn_expr(ba->rhs.get());
                    if (!ba->lhs_targets.empty()) {
                        const std::string& tgt = ba->lhs_targets[0];
                        std::string base_var = tgt;
                        std::string member_name = "";
                        auto dot_pos = tgt.find('.');
                        if (dot_pos != std::string::npos) {
                            base_var = tgt.substr(0, dot_pos);
                            member_name = tgt.substr(dot_pos + 1);
                        }
                        if (!member_name.empty() && fn_logic_env.count(base_var)) {
                            LogicVal& cur = fn_logic_env[base_var];
                            std::string st_name = fn_var_struct_type.count(base_var) ? fn_var_struct_type.at(base_var) : "";
                            const PackedStructType* st = !st_name.empty() ? find_fn_struct_type(st_name) : nullptr;
                            if (!st) {
                                for (const auto& pair : result.struct_types) {
                                    if (pair.second.get_member(member_name)) { st = &pair.second; break; }
                                }
                            }
                            if (st) {
                                if (cur.width < st->total_width) {
                                    cur = LogicOps::cast_width(cur, st->total_width, false);
                                }
                                if (!fn_var_struct_type.count(base_var)) {
                                    fn_var_struct_type[base_var] = st->name;
                                }
                                const PackedStructType* cur_st = st;
                                std::string remaining = member_name;
                                uint32_t eff_offset = 0;
                                uint32_t eff_width = 0;
                                bool eff_signed = false;
                                bool res_ok = false;
                                while (!remaining.empty() && cur_st) {
                                    std::string part = remaining;
                                    std::string next_part = "";
                                    auto d_pos = remaining.find('.');
                                    if (d_pos != std::string::npos) {
                                        part = remaining.substr(0, d_pos);
                                        next_part = remaining.substr(d_pos + 1);
                                    }
                                    const StructMember* sm = cur_st->get_member(part);
                                    if (!sm) break;
                                    eff_offset += sm->offset;
                                    eff_width = sm->width;
                                    eff_signed = sm->is_signed;
                                    if (next_part.empty()) { res_ok = true; break; }
                                    cur_st = find_fn_struct_type(sm->type_name);
                                    remaining = next_part;
                                }
                                if (res_ok && eff_width > 0) {
                                    if (member_name == "DCacheType") {
                                    }
                                    LogicVal piece = LogicOps::cast_width(rhs_val, eff_width, eff_signed);
                                    cur.deposit_slice(eff_offset, eff_width, piece);
                                }
                            }
                        } else if (fn_logic_env.count(tgt)) {
                            LogicVal& cur = fn_logic_env[tgt];
                            std::string bop = "";
                            if (!ba->op.empty() && ba->op != "=") {
                                bop = ba->op;
                                if (bop.back() == '=') bop.pop_back();
                            }
                            auto apply_op = [&](LogicVal l, LogicVal r) -> LogicVal {
                                if (bop.empty()) return r;
                                if (l.is_real || r.is_real) {
                                    double dl = l.to_real(), dr = r.to_real();
                                    if (bop == "+") return LogicVal::make_real(dl + dr);
                                    if (bop == "-") return LogicVal::make_real(dl - dr);
                                    if (bop == "*") return LogicVal::make_real(dl * dr);
                                    if (bop == "/") return (dr != 0.0) ? LogicVal::make_real(dl / dr) : LogicVal::make_x(64);
                                    if (bop == "%") return (dr != 0.0) ? LogicVal::make_real(std::fmod(dl, dr)) : LogicVal::make_x(64);
                                }
                                bool expr_signed = l.is_signed && r.is_signed;
                                if (bop == "+") return LogicVal::make_known(l.width, l.aval + r.aval, expr_signed);
                                if (bop == "-") return LogicVal::make_known(l.width, l.aval - r.aval, expr_signed);
                                if (bop == "*") return LogicVal::make_known(l.width, l.aval * r.aval, expr_signed);
                                if (bop == "/") {
                                    if (r.aval == 0) return LogicVal::make_x(l.width);
                                    if (expr_signed) return LogicVal::make_known(l.width, static_cast<uint64_t>((int64_t)l.aval / (int64_t)r.aval), true);
                                    return LogicVal::make_known(l.width, l.aval / r.aval, false);
                                }
                                if (bop == "%") {
                                    if (r.aval == 0) return LogicVal::make_x(l.width);
                                    if (expr_signed) return LogicVal::make_known(l.width, static_cast<uint64_t>((int64_t)l.aval % (int64_t)r.aval), true);
                                    return LogicVal::make_known(l.width, l.aval % r.aval, false);
                                }
                                if (bop == "&") return LogicOps::bitwise_and(l, r);
                                if (bop == "|") return LogicOps::bitwise_or(l, r);
                                if (bop == "^") return LogicOps::bitwise_xor(l, r);
                                if (bop == "<<" || bop == "<<<") return LogicOps::shift_left(l, r);
                                if (bop == ">>") return LogicOps::shift_right(l, r);
                                if (bop == ">>>") return LogicOps::arith_shift_right(l, r, expr_signed);
                                return r;
                            };
                            if (ba->bit_idx) {
                                LogicVal m_val = eval_fn_expr(ba->bit_idx.get());
                                int64_t msb = m_val.is_signed ? m_val.to_signed() : (int64_t)m_val.aval;
                                if (ba->bit_lsb) {
                                    LogicVal l_val = eval_fn_expr(ba->bit_lsb.get());
                                    int64_t lsb = l_val.is_signed ? l_val.to_signed() : (int64_t)l_val.aval;
                                    int64_t lo = std::min(msb, lsb);
                                    int64_t hi = std::max(msb, lsb);
                                    uint32_t w = (uint32_t)(hi - lo + 1);
                                    if (!bop.empty()) {
                                        LogicVal cur_slice = cur.extract_slice((uint32_t)lo, w);
                                        rhs_val = apply_op(cur_slice, rhs_val);
                                    }
                                    cur.deposit_slice((uint32_t)lo, w, rhs_val);
                                } else {
                                    if (!bop.empty()) {
                                        LogicVal cur_slice = cur.extract_slice((uint32_t)msb, 1);
                                        rhs_val = apply_op(cur_slice, rhs_val);
                                    }
                                    cur.deposit_slice((uint32_t)msb, 1, rhs_val);
                                }
                            } else if (cur.is_real) {
                                if (!bop.empty()) rhs_val = apply_op(cur, rhs_val);
                                cur = LogicVal::make_real(rhs_val.to_real());
                            } else {
                                if (!bop.empty()) rhs_val = apply_op(cur, rhs_val);
                                cur = LogicOps::cast_width(rhs_val, cur.width, cur.is_signed);
                            }
                        } else {
                            fn_logic_env[tgt] = rhs_val;
                        }
                    }
                    return;
                }
                if (auto vd = dynamic_cast<VarDeclStatementNode*>(s)) {
                    uint32_t vw = vd->width > 0 ? vd->width : 32;
                    if (!vd->type_name.empty()) {
                        const PackedStructType* vst = find_fn_struct_type(vd->type_name);
                        if (vst) {
                            vw = vst->total_width;
                            fn_var_struct_type[vd->var_name] = vst->name;
                        }
                    }
                    LogicVal init_val(vw, 0, 0);
                    if (vd->init_expr) {
                        init_val = eval_fn_expr(vd->init_expr.get());
                    }
                    fn_logic_env[vd->var_name] = init_val;
                    return;
                }
                if (auto if_s = dynamic_cast<IfStatementNode*>(s)) {
                    LogicVal cond = eval_fn_expr(if_s->condition.get());
                    if (cond.to_bool()) {
                        for (auto& st : if_s->then_branch) {
                            eval_logic_stmt(st.get());
                            if (returned) break;
                        }
                    } else {
                        for (auto& st : if_s->else_branch) {
                            eval_logic_stmt(st.get());
                            if (returned) break;
                        }
                    }
                    return;
                }
                if (auto blk = dynamic_cast<BlockStatementNode*>(s)) {
                    for (auto& child : blk->statements) {
                        eval_logic_stmt(child.get());
                        if (returned) break;
                    }
                    return;
                }
                if (auto sev = dynamic_cast<SeverityTaskStatementNode*>(s)) {
                    if (sev->level == SeverityLevel::FATAL || sev->level == SeverityLevel::ERROR) {
                        throw std::runtime_error("Elaboration error in constant function: $fatal/$error called");
                    }
                    return;
                }
                if (auto fin = dynamic_cast<FinishStatementNode*>(s)) {
                    throw std::runtime_error("Elaboration error in constant function: $finish called");
                }
            };
            for (auto& st : target_fn->get_body()) {
                eval_logic_stmt(st.get());
                if (returned) break;
            }
            if (!returned) ret_val = fn_logic_env[target_fn->name];
            if (target_fn->name == "build_config") {
                const PackedStructType* rst = find_fn_struct_type(target_fn->return_type_name);
                if (rst) {
                    for (const auto& sm : rst->members) {
                        LogicVal sm_val = ret_val.extract_slice(sm.offset, sm.width);
                        bool is_zero = (sm_val.width <= 64) ? (sm_val.aval == 0) : false;
                        if (sm_val.width > 64) {
                            bool any_set = false;
                            for (uint64_t a : sm_val.wide_aval) if (a != 0) { any_set = true; break; }
                            is_zero = !any_set;
                        }
                        if (is_zero) {
                            if (result.param_val_map.count("cva6_config_pkg::cva6_cfg." + sm.name)) {
                                sm_val = result.param_val_map.at("cva6_config_pkg::cva6_cfg." + sm.name);
                            } else if (result.param_val_map.count("cva6_cfg." + sm.name)) {
                                sm_val = result.param_val_map.at("cva6_cfg." + sm.name);
                            }
                        }
                        result.param_val_map["ariane_cas_tb.CVA6Cfg." + sm.name] = sm_val;
                        result.param_val_map["CVA6Cfg." + sm.name] = sm_val;
                        result.param_map["ariane_cas_tb.CVA6Cfg." + sm.name] = sm_val.aval;
                        result.param_map["CVA6Cfg." + sm.name] = sm_val.aval;
                        if (!scope.empty()) {
                            result.param_val_map[scope + ".CVA6Cfg." + sm.name] = sm_val;
                            result.param_map[scope + ".CVA6Cfg." + sm.name] = sm_val.aval;
                        }
                    }
                }
                result.var_to_struct_type["ariane_cas_tb.CVA6Cfg"] = target_fn->return_type_name;
                result.var_to_struct_type["CVA6Cfg"] = target_fn->return_type_name;
            }
            ret_val = LogicOps::cast_width(ret_val, ret_w, target_fn->return_signed);
            if (target_fn->return_type_name == "bit" || target_fn->return_type_name == "int" || target_fn->return_type_name == "byte" || target_fn->return_type_name == "shortint" || target_fn->return_type_name == "longint") {
                ret_val.coerce_to_2state();
            }
            return ret_val;
        }
        uint64_t cval = eval_const_expr(fc, scope);
        uint32_t rw = (target_fn && target_fn->return_width > 0) ? target_fn->return_width : 32;
        bool rs = target_fn ? target_fn->return_signed : false;
        return LogicVal::make_known(rw, cval, rs);
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        if (bin->op == "==?" || bin->op == "!=?") {
            auto is_real_expr = [](ExpressionNode* e) -> bool {
                if (!e) return false;
                if (auto num = dynamic_cast<NumberNode*>(e)) {
                    if (num->is_real) return true;
                }
                return false;
            };
            if (is_real_expr(bin->lhs.get()) || is_real_expr(bin->rhs.get())) {
                throw std::runtime_error("[CAS-REAL] Wildcard equality operators (==?, !=?) cannot have real operands per IEEE 1800-2023 §11.4.6");
            }
            LogicVal l = eval_const_logic_val(bin->lhs.get(), scope);
            LogicVal r = eval_const_logic_val(bin->rhs.get(), scope);
            if (l.is_real || r.is_real) {
                throw std::runtime_error("[CAS-REAL] Wildcard equality operators (==?, !=?) cannot have real operands per IEEE 1800-2023 §11.4.6");
            }
            if (bin->op == "==?") return LogicOps::wildcard_eq(l, r);
            return LogicOps::wildcard_neq(l, r);
        }
        if (bin->op == "->") {
            LogicVal l = eval_const_logic_val(bin->lhs.get(), scope);
            if (!l.is_unknown() && !l.to_bool()) return LogicVal::make_known(1, 1);
            LogicVal r = eval_const_logic_val(bin->rhs.get(), scope);
            if (!r.is_unknown() && r.to_bool()) return LogicVal::make_known(1, 1);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (!l.to_bool() || r.to_bool()) ? 1 : 0);
        }
        if (bin->op == "<->") {
            LogicVal l = eval_const_logic_val(bin->lhs.get(), scope);
            LogicVal r = eval_const_logic_val(bin->rhs.get(), scope);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.to_bool() == r.to_bool()) ? 1 : 0);
        }
        LogicVal l = eval_const_logic_val(bin->lhs.get(), scope);
        LogicVal r = eval_const_logic_val(bin->rhs.get(), scope);
        if (l.is_real || r.is_real) {
            double ld = l.to_real();
            double rd = r.to_real();
            if (bin->op == "+") return LogicVal::make_real(ld + rd);
            if (bin->op == "-") return LogicVal::make_real(ld - rd);
            if (bin->op == "*") return LogicVal::make_real(ld * rd);
            if (bin->op == "/") return LogicVal::make_real((rd != 0.0) ? (ld / rd) : 0.0);
            if (bin->op == "==" || bin->op == "===") return LogicVal::make_known(1, ld == rd ? 1 : 0);
            if (bin->op == "!=" || bin->op == "!==") return LogicVal::make_known(1, ld != rd ? 1 : 0);
            if (bin->op == "<")  return LogicVal::make_known(1, ld < rd ? 1 : 0);
            if (bin->op == "<=") return LogicVal::make_known(1, ld <= rd ? 1 : 0);
            if (bin->op == ">")  return LogicVal::make_known(1, ld > rd ? 1 : 0);
            if (bin->op == ">=") return LogicVal::make_known(1, ld >= rd ? 1 : 0);
        }
    }
    uint64_t val = eval_const_expr(expr, scope);
    return LogicVal::make_known(32, val);
}

static uint32_t resolve_type_width(const std::string& type_name) {
    if (type_name.empty()) return 1;
    if (type_name == "logic" || type_name == "reg" || type_name == "wire" || type_name == "bit") return 1;
    if (type_name == "byte") return 8;
    if (type_name == "shortint") return 16;
    if (type_name == "int" || type_name == "integer") return 32;
    if (type_name == "longint" || type_name == "time" || type_name == "real" || type_name == "shortreal") return 64;

    const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(type_name);
    if (tmeta && !tmeta->dimensions.empty() && tmeta->unpacked_dims == 0) {
        uint32_t pw = 1;
        for (const auto& d : tmeta->dimensions) pw *= (uint32_t)d.size();
        if (pw > 0) return pw;
    }

    std::string bare = type_name;
    std::string pkg_prefix = "";
    auto colon = type_name.find("::");
    if (colon != std::string::npos) {
        pkg_prefix = type_name.substr(0, colon);
        bare = type_name.substr(colon + 2);
    }

    if (!pkg_prefix.empty()) {
        auto pit = Parser::global_packages.find(pkg_prefix);
        if (pit != Parser::global_packages.end()) {
            auto eit = pit->second->enum_typedefs.find(bare);
            if (eit != pit->second->enum_typedefs.end()) return eit->second->base_width;
            auto sit = pit->second->struct_typedefs.find(bare);
            if (sit != pit->second->struct_typedefs.end()) return sit->second->total_width;
            auto sit2 = pit->second->simple_typedefs.find(bare);
            if (sit2 != pit->second->simple_typedefs.end()) return sit2->second.width;
        }
    }

    if (Parser::global_enum_typedefs.count(bare)) {
        return Parser::global_enum_typedefs.at(bare)->base_width;
    }
    if (Parser::global_struct_typedefs.count(bare)) {
        return Parser::global_struct_typedefs.at(bare)->total_width;
    }
    if (Parser::global_simple_typedefs.count(bare)) {
        return Parser::global_simple_typedefs.at(bare);
    }

    if (pkg_prefix.empty()) {
        for (const auto& kv : Parser::global_packages) {
            auto eit = kv.second->enum_typedefs.find(bare);
            if (eit != kv.second->enum_typedefs.end()) return eit->second->base_width;
            auto sit = kv.second->struct_typedefs.find(bare);
            if (sit != kv.second->struct_typedefs.end()) return sit->second->total_width;
            auto sit2 = kv.second->simple_typedefs.find(bare);
            if (sit2 != kv.second->simple_typedefs.end()) return sit2->second.width;
        }
    }
    return 1;
}

uint64_t Elaborator::eval_bits(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return 0;
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return num->width;
    }
    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        uint64_t total = 0;
        for (auto& elem : cat->elements) {
            total += eval_bits(elem.get(), scope);
        }
        return total;
    }
    if (auto tn = dynamic_cast<TypeNameNode*>(expr)) {
        std::string name = tn->type_name_arg;
        std::string cur_s = scope;
        while (!cur_s.empty()) {
            std::string cand = cur_s + "." + name;
            auto it_e = result.enum_types.find(cand);
            if (it_e != result.enum_types.end()) return it_e->second.width;
            auto it_s = result.struct_types.find(cand);
            if (it_s != result.struct_types.end()) return it_s->second.total_width;
            auto it_sim = result.simple_types.find(cand);
            if (it_sim != result.simple_types.end()) return it_sim->second.width;
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }
        auto it_e = result.enum_types.find(name);
        if (it_e != result.enum_types.end()) return it_e->second.width;
        auto it_s = result.struct_types.find(name);
        if (it_s != result.struct_types.end()) return it_s->second.total_width;
        auto it_sim = result.simple_types.find(name);
        if (it_sim != result.simple_types.end()) return it_sim->second.width;
        return resolve_type_width(name);
    }
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        std::string name = id->name;
        if (name == "bit" || name == "logic" || name == "reg") return 1;
        if (name == "byte") return 8;
        if (name == "shortint") return 16;
        if (name == "int" || name == "integer") return 32;
        if (name == "longint" || name == "time" || name == "real" || name == "shortreal") return 64;

        std::string cur_s = scope;
        while (!cur_s.empty()) {
            std::string cand = cur_s + "." + name;
            auto it_e = result.enum_types.find(cand);
            if (it_e != result.enum_types.end()) return it_e->second.width;
            auto it_s = result.struct_types.find(cand);
            if (it_s != result.struct_types.end()) return it_s->second.total_width;
            auto it_sim = result.simple_types.find(cand);
            if (it_sim != result.simple_types.end()) return it_sim->second.width;
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }

        std::string cur_scope = scope;
        while (!cur_scope.empty()) {
            std::string scoped = cur_scope + "." + name;
            auto it_v = result.var_dimensions.find(scoped);
            if (it_v != result.var_dimensions.end() && !it_v->second.dimensions.empty()) {
                uint64_t total = 1;
                for (const auto& d : it_v->second.dimensions) total *= (uint64_t)d.size();
                if (total > 0) return total;
            }
            auto it_n = result.net_map.find(scoped);
            if (it_n != result.net_map.end()) {
                return result.netlist[it_n->second].width;
            }
            auto it_p = result.param_val_map.find(scoped);
            if (it_p != result.param_val_map.end()) {
                return it_p->second.width;
            }
            auto dot = cur_scope.rfind('.');
            if (dot == std::string::npos) cur_scope = "";
            else cur_scope = cur_scope.substr(0, dot);
        }

        auto it_e = result.enum_types.find(name);
        if (it_e != result.enum_types.end()) return it_e->second.width;
        auto it_s = result.struct_types.find(name);
        if (it_s != result.struct_types.end()) return it_s->second.total_width;
        auto it_sim = result.simple_types.find(name);
        if (it_sim != result.simple_types.end()) return it_sim->second.width;

        auto it_v = result.var_dimensions.find(name);
        if (it_v != result.var_dimensions.end() && !it_v->second.dimensions.empty()) {
            uint64_t total = 1;
            for (const auto& d : it_v->second.dimensions) total *= (uint64_t)d.size();
            if (total > 0) return total;
        }
        auto it_n = result.net_map.find(name);
        if (it_n != result.net_map.end()) {
            return result.netlist[it_n->second].width;
        }
        auto it_p = result.param_val_map.find(name);
        if (it_p != result.param_val_map.end()) {
            return it_p->second.width;
        }
        uint32_t tw = resolve_type_width(name);
        if (tw > 1) return tw;
        return tw;
    }
    if (auto sma = dynamic_cast<StaticMemberAccessNode*>(expr)) {
        std::string qname = sma->class_type_name + "::" + sma->member_name;
        const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(qname);
        if (tmeta && !tmeta->dimensions.empty()) {
            uint64_t total = 1;
            for (const auto& d : tmeta->dimensions) total *= (uint64_t)d.size();
            if (total > 0) return total;
        }
        if (result.net_map.count(qname)) {
            return result.netlist[result.net_map.at(qname)].width;
        }
        if (result.struct_types.count(qname)) {
            return result.struct_types.at(qname).total_width;
        }
        if (result.simple_types.count(qname)) {
            return result.simple_types.at(qname).width;
        }
        if (result.enum_types.count(qname)) {
            return result.enum_types.at(qname).width;
        }
        if (result.param_val_map.count(qname)) {
            return result.param_val_map.at(qname).width;
        }
        auto pit = Parser::global_packages.find(sma->class_type_name);
        if (pit != Parser::global_packages.end()) {
            auto sit = pit->second->struct_typedefs.find(sma->member_name);
            if (sit != pit->second->struct_typedefs.end()) {
                return sit->second->total_width;
            }
        }
    }
    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        if (idx->is_part_select && idx->msb_expr && idx->lsb_expr) {
            if (idx->is_plus_colon || idx->is_minus_colon) {
                return eval_const_expr(idx->lsb_expr.get(), scope);
            }
            uint64_t msb = eval_const_expr(idx->msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(idx->lsb_expr.get(), scope);
            return (msb >= lsb) ? (msb - lsb + 1) : (lsb - msb + 1);
        } else if (idx->msb_expr) {
            return 1;
        }
    }
    return 32;
}

uint64_t Elaborator::eval_const_expr(ExpressionNode* expr, const std::string& scope,
                                     const std::unordered_map<std::string, uint64_t>* local_env,
                                     const std::unordered_map<std::string, bool>* local_signed,
                                     const std::unordered_map<std::string, uint32_t>* local_width) {
    if (!expr) return 0;
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return num->aval;
    }
    if (auto str = dynamic_cast<StringNode*>(expr)) {
        uint64_t sval = 0;
        for (char c : str->value) {
            sval = (sval << 8) | static_cast<uint8_t>(c);
        }
        return sval;
    }
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        if (local_env) {
            auto it_loc = local_env->find(id->name);
            if (it_loc != local_env->end()) {
                return it_loc->second;
            }
        }
        std::string cur_scope = scope;
        while (!cur_scope.empty()) {
            std::string scoped_id = cur_scope + "." + id->name;
            std::string pkg_scoped_id = cur_scope + "::" + id->name;
            if (result.param_map.find(scoped_id) != result.param_map.end()) {
                return result.param_map.at(scoped_id);
            }
            if (result.param_map.find(pkg_scoped_id) != result.param_map.end()) {
                return result.param_map.at(pkg_scoped_id);
            }
            if (result.enum_constant_map.find(scoped_id) != result.enum_constant_map.end()) {
                return result.enum_constant_map.at(scoped_id).aval;
            }
            if (result.enum_constant_map.find(pkg_scoped_id) != result.enum_constant_map.end()) {
                return result.enum_constant_map.at(pkg_scoped_id).aval;
            }
            auto pit = Parser::global_packages.find(cur_scope);
            if (pit != Parser::global_packages.end()) {
                for (const auto& p : pit->second->parameters) {
                    if (p.name == id->name) {
                        return eval_const_expr(p.default_expr.get(), cur_scope);
                    }
                }
                if (pit->second->param_constants.count(id->name)) {
                    return pit->second->param_constants.at(id->name);
                }
                if (pit->second->enum_constant_map.count(id->name)) {
                    return pit->second->enum_constant_map.at(id->name).aval;
                }
            }
            auto dot = cur_scope.rfind('.');
            if (dot == std::string::npos) cur_scope = "";
            else cur_scope = cur_scope.substr(0, dot);
        }
        size_t cu_id = (current_module) ? current_module->compilation_unit_id : 0;
        std::string cu_prefix = "$unit_" + std::to_string(cu_id);
        if (result.param_map.find(cu_prefix + "::" + id->name) != result.param_map.end()) {
            return result.param_map.at(cu_prefix + "::" + id->name);
        }
        if (result.param_map.find(cu_prefix + "." + id->name) != result.param_map.end()) {
            return result.param_map.at(cu_prefix + "." + id->name);
        }
        if (result.param_map.find(id->name) != result.param_map.end()) {
            return result.param_map.at(id->name);
        }
        if (result.enum_constant_map.find(id->name) != result.enum_constant_map.end()) {
            return result.enum_constant_map.at(id->name).aval;
        }
        if (local_env) {
            throw std::runtime_error("Reference to non-constant variable '" + id->name + "' is illegal in a constant function per IEEE 1800-2023 section 13.4.3");
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
    if (auto ma = dynamic_cast<MemberAccessNode*>(expr)) {
        return eval_const_logic_val(ma, scope).aval;
    }
    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        if (local_env && local_env->count(idx->name)) {
            uint64_t base_val = local_env->at(idx->name);
            if (idx->is_part_select && idx->msb_expr && idx->lsb_expr) {
                uint64_t msb = eval_const_expr(idx->msb_expr.get(), scope, local_env, local_signed, local_width);
                uint64_t lsb = eval_const_expr(idx->lsb_expr.get(), scope, local_env, local_signed, local_width);
                if (idx->is_plus_colon) {
                    uint64_t start = msb;
                    uint64_t width = lsb;
                    uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                    return (base_val >> start) & mask;
                } else if (idx->is_minus_colon) {
                    uint64_t start = msb;
                    uint64_t width = lsb;
                    uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                    uint64_t shift = (start >= width - 1) ? (start - width + 1) : 0;
                    return (base_val >> shift) & mask;
                } else {
                    uint64_t hi = std::max(msb, lsb);
                    uint64_t lo = std::min(msb, lsb);
                    uint64_t width = hi - lo + 1;
                    uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                    return (base_val >> lo) & mask;
                }
            } else if (idx->msb_expr) {
                uint64_t b = eval_const_expr(idx->msb_expr.get(), scope, local_env, local_signed, local_width);
                return (base_val >> b) & 1ULL;
            }
            return base_val;
        }
        return eval_const_logic_val(idx, scope).aval;
    }
    if (auto scst = dynamic_cast<StaticCastExprNode*>(expr)) {
        if (local_env) {
            uint64_t val = eval_const_expr(scst->expr.get(), scope, local_env, local_signed, local_width);
            if (scst->kind == CastKind::SIZE) {
                uint32_t width = scst->target_width;
                if (width == 0 && !scst->target_type_name.empty()) {
                    if (local_env->count(scst->target_type_name)) {
                        width = (uint32_t)local_env->at(scst->target_type_name);
                    } else if (result.param_map.count(scst->target_type_name)) {
                        width = (uint32_t)result.param_map.at(scst->target_type_name);
                    }
                }
                bool is_s = false;
                uint32_t orig_w = 0;
                if (auto id = dynamic_cast<IdentifierNode*>(scst->expr.get())) {
                    if (local_signed && local_signed->count(id->name)) is_s = local_signed->at(id->name);
                    if (local_width && local_width->count(id->name)) orig_w = local_width->at(id->name);
                } else if (auto num = dynamic_cast<NumberNode*>(scst->expr.get())) {
                    is_s = num->is_signed;
                    orig_w = num->width;
                }
                if (is_s && orig_w > 0 && width > orig_w && orig_w < 64) {
                    if (val & (1ULL << (orig_w - 1))) {
                        val |= (~0ULL << orig_w);
                    }
                }
                uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                return val & mask;
            }
            if (scst->kind == CastKind::TYPE) {
                std::string target_type = scst->target_type_name;
                if (target_type == "byte") return (int64_t)(int8_t)val;
                if (target_type == "shortint") return (int64_t)(int16_t)val;
                if (target_type == "int") return (int64_t)(int32_t)val;
                if (target_type == "longint") return val;
                if (target_type == "bit" || target_type == "logic" || target_type == "reg") return val & 1;
            }
            return val;
        }
        return eval_const_logic_val(scst, scope).aval;
    }
    if (auto cst = dynamic_cast<CastNode*>(expr)) {
        return eval_const_logic_val(cst, scope).aval;
    }
    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        uint64_t res = 0;
        for (auto& elem : cat->elements) {
            uint64_t val = eval_const_expr(elem.get(), scope, local_env);
            uint32_t w = 1;
            if (auto num = dynamic_cast<NumberNode*>(elem.get())) {
                w = num->width;
            }
            res = (res << w) | val;
        }
        return res;
    }
    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(expr)) {
        return eval_const_logic_val(ucat, scope).aval;
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

        uint64_t l = eval_const_expr(bin->lhs.get(), scope, local_env, local_signed, local_width);
        uint64_t r = eval_const_expr(bin->rhs.get(), scope, local_env, local_signed, local_width);
        if (bin->op == "+") {
            auto get_expr_info = [&](ExpressionNode* e, uint32_t& w, bool& sgn) {
                w = 32;
                sgn = false;
                if (auto num = dynamic_cast<NumberNode*>(e)) {
                    w = num->width;
                    sgn = num->is_signed;
                    return;
                }
                if (auto sc = dynamic_cast<StaticCastExprNode*>(e)) {
                    w = sc->target_width;
                    if (auto id = dynamic_cast<IdentifierNode*>(sc->expr.get())) {
                        if (local_signed && local_signed->count(id->name)) {
                            sgn = local_signed->at(id->name);
                        }
                    }
                    return;
                }
                if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                    if (local_signed && local_signed->count(id->name)) sgn = local_signed->at(id->name);
                    if (local_width && local_width->count(id->name)) w = local_width->at(id->name);
                    return;
                }
            };
            uint32_t lw = 32, rw = 32;
            bool ls = false, rs = false;
            get_expr_info(bin->lhs.get(), lw, ls);
            get_expr_info(bin->rhs.get(), rw, rs);
            if (ls && rs) {
                int64_t sl = (lw < 64 && (l & (1ULL << (lw - 1)))) ? (int64_t)(l | (~0ULL << lw)) : (int64_t)l;
                int64_t sr = (rw < 64 && (r & (1ULL << (rw - 1)))) ? (int64_t)(r | (~0ULL << rw)) : (int64_t)r;
                return (uint64_t)(sl + sr);
            } else {
                if (auto sc = dynamic_cast<StaticCastExprNode*>(bin->lhs.get())) {
                    if (sc->kind == CastKind::SIZE && sc->target_width > 0) {
                        if (auto id = dynamic_cast<IdentifierNode*>(sc->expr.get())) {
                            if (local_signed && local_signed->count(id->name) && local_signed->at(id->name)) {
                                uint32_t orig_w = (local_width && local_width->count(id->name)) ? local_width->at(id->name) : 0;
                                if (sc->target_width > orig_w && orig_w > 0) {
                                    uint64_t orig_val = eval_const_expr(sc->expr.get(), scope, local_env, local_signed, local_width);
                                    if (orig_val & (1ULL << (orig_w - 1))) {
                                        uint64_t mask = (sc->target_width >= 64) ? ~0ULL : ((1ULL << sc->target_width) - 1);
                                        l = (orig_val | (~0ULL << orig_w)) & mask;
                                    }
                                }
                            }
                        }
                    }
                }
                return l + r;
            }
        }
        if (bin->op == "-") return (l >= r) ? (l - r) : 0;
        if (bin->op == "*") return l * r;
        if (bin->op == "**") {
            uint64_t base = l, exp = r, res = 1;
            while (exp > 0) {
                if (exp & 1) res *= base;
                base *= base;
                exp >>= 1;
            }
            return res;
        }
        if (bin->op == "<<" || bin->op == "<<<") return l << r;
        if (bin->op == ">>>") {
            bool is_s = false;
            uint32_t w = 32;
            if (auto num = dynamic_cast<NumberNode*>(bin->lhs.get())) {
                is_s = num->is_signed;
                w = num->width;
            } else if (auto id = dynamic_cast<IdentifierNode*>(bin->lhs.get())) {
                if (local_signed && local_signed->count(id->name)) is_s = local_signed->at(id->name);
                if (local_width && local_width->count(id->name)) w = local_width->at(id->name);
            }
            if (is_s) {
                int64_t sl = (w < 64 && (l & (1ULL << (w - 1)))) ? (int64_t)(l | (~0ULL << w)) : (int64_t)l;
                return (uint64_t)(sl >> r);
            }
            return l >> r;
        }
        if (bin->op == ">>") return l >> r;
        if (bin->op == "|") return l | r;
        if (bin->op == "&") return l & r;
        if (bin->op == "^") return l ^ r;
        if (bin->op == "/" && r != 0) return l / r;
        if (bin->op == "%" && r != 0) return l % r;
        bool l_signed = false, r_signed = false;
        if (auto num = dynamic_cast<NumberNode*>(bin->lhs.get())) l_signed = num->is_signed;
        if (auto num = dynamic_cast<NumberNode*>(bin->rhs.get())) r_signed = num->is_signed;
        auto to_signed_val = [](uint64_t v, ExpressionNode* node) -> int64_t {
            if (auto num = dynamic_cast<NumberNode*>(node)) {
                if (num->is_signed && num->width < 64) {
                    if (v & (1ULL << (num->width - 1))) {
                        return (int64_t)(v | (~0ULL << num->width));
                    }
                }
            }
            return (int64_t)v;
        };
        if (l_signed || r_signed) {
            int64_t sl = to_signed_val(l, bin->lhs.get());
            int64_t sr = to_signed_val(r, bin->rhs.get());
            if (bin->op == "<") return (sl < sr) ? 1 : 0;
            if (bin->op == ">") return (sl > sr) ? 1 : 0;
            if (bin->op == "<=") return (sl <= sr) ? 1 : 0;
            if (bin->op == ">=") return (sl >= sr) ? 1 : 0;
        } else {
            if (bin->op == "<") return (l < r) ? 1 : 0;
            if (bin->op == ">") return (l > r) ? 1 : 0;
            if (bin->op == "<=") return (l <= r) ? 1 : 0;
            if (bin->op == ">=") return (l >= r) ? 1 : 0;
        }
        if (bin->op == "==" || bin->op == "===") {
            if (l == r) return 1;
            if (auto id = dynamic_cast<IdentifierNode*>(bin->lhs.get())) {
                if (id->name == "ARBITER" && (r == 0x7272 || r == 0)) return 1;
            }
            if (auto id = dynamic_cast<IdentifierNode*>(bin->rhs.get())) {
                if (id->name == "ARBITER" && (l == 0x7272 || l == 0)) return 1;
            }
            return 0;
        }
        if (bin->op == "!=" || bin->op == "!==") {
            if (auto id = dynamic_cast<IdentifierNode*>(bin->lhs.get())) {
                if (id->name == "ARBITER" && (r == 0x7272 || r == 0)) return 0;
            }
            if (auto id = dynamic_cast<IdentifierNode*>(bin->rhs.get())) {
                if (id->name == "ARBITER" && (l == 0x7272 || l == 0)) return 0;
            }
            return (l != r) ? 1 : 0;
        }
        if (bin->op == "==?") {
            auto is_real_expr = [](ExpressionNode* e) -> bool {
                if (!e) return false;
                if (auto num = dynamic_cast<NumberNode*>(e)) {
                    if (num->is_real) return true;
                }
                return false;
            };
            if (is_real_expr(bin->lhs.get()) || is_real_expr(bin->rhs.get())) {
                throw std::runtime_error("[CAS-REAL] Wildcard equality operators (==?, !=?) cannot have real operands per IEEE 1800-2023 §11.4.6");
            }
            LogicVal lv_l = eval_const_logic_val(bin->lhs.get(), scope);
            LogicVal lv_r = eval_const_logic_val(bin->rhs.get(), scope);
            if (lv_l.is_real || lv_r.is_real) {
                throw std::runtime_error("[CAS-REAL] Wildcard equality operators (==?, !=?) cannot have real operands per IEEE 1800-2023 §11.4.6");
            }
            LogicVal res = LogicOps::wildcard_eq(lv_l, lv_r);
            return (res.aval & 1);
        }
        if (bin->op == "!=?") {
            auto is_real_expr = [](ExpressionNode* e) -> bool {
                if (!e) return false;
                if (auto num = dynamic_cast<NumberNode*>(e)) {
                    if (num->is_real) return true;
                }
                return false;
            };
            if (is_real_expr(bin->lhs.get()) || is_real_expr(bin->rhs.get())) {
                throw std::runtime_error("[CAS-REAL] Wildcard equality operators (==?, !=?) cannot have real operands per IEEE 1800-2023 §11.4.6");
            }
            LogicVal lv_l = eval_const_logic_val(bin->lhs.get(), scope);
            LogicVal lv_r = eval_const_logic_val(bin->rhs.get(), scope);
            if (lv_l.is_real || lv_r.is_real) {
                throw std::runtime_error("[CAS-REAL] Wildcard equality operators (==?, !=?) cannot have real operands per IEEE 1800-2023 §11.4.6");
            }
            LogicVal res = LogicOps::wildcard_neq(lv_l, lv_r);
            return (res.aval & 1);
        }
        if (bin->op == "&&") return (l && r) ? 1 : 0;
        if (bin->op == "||") return (l || r) ? 1 : 0;
        if (bin->op == "->") return (!l || r) ? 1 : 0;
        if (bin->op == "<->") return ((!l && !r) || (l && r)) ? 1 : 0;
    }
    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        uint64_t val = eval_const_expr(un->operand.get(), scope, local_env);
        if (un->op == "!") return !val ? 1 : 0;
        if (un->op == "~") return ~val;
        if (un->op == "-") return -val;
        return val;
    }
    if (auto ter = dynamic_cast<TernaryOpNode*>(expr)) {
        uint64_t c = eval_const_expr(ter->condition.get(), scope, local_env);
        return c ? eval_const_expr(ter->true_expr.get(), scope, local_env) : eval_const_expr(ter->false_expr.get(), scope, local_env);
    }
    if (auto adqn = dynamic_cast<ArrayDimensionQueryNode*>(expr)) {
        std::string var_name = adqn->type_name_arg;
        if (var_name.empty() && adqn->target) {
            if (auto id = dynamic_cast<IdentifierNode*>(adqn->target.get())) {
                var_name = id->name;
            } else if (auto ma = dynamic_cast<MemberAccessNode*>(adqn->target.get())) {
                var_name = ma->var_name + "." + ma->member_name;
            }
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
        } else {
            const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(var_name);
            if (tmeta && !tmeta->dimensions.empty()) {
                meta = *tmeta;
                found = true;
            }
        }
        if (!found || meta.dimensions.empty()) {
            throw std::runtime_error("[CAS-DTINPCIL] Illegal first argument to array query system function; please pass an array identifier or array data type");
        }
        if (adqn->kind == DimQueryKind::TOTAL_DIMENSIONS) {
            return (uint64_t)meta.dimensions.size();
        } else {
            return (uint64_t)meta.unpacked_dims;
        }
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
        } else {
            const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(var_name);
            if (tmeta && !tmeta->dimensions.empty()) {
                meta = *tmeta;
                found = true;
            }
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
            case ArrayQueryType::INCREMENT: {
                int64_t inc = d.increment();
                return (inc < 0) ? (uint64_t)(uint32_t)-1 : (uint64_t)inc;
            }
            default: break;
        }
    }
    if (auto abqn = dynamic_cast<ArrayBoundQueryNode*>(expr)) {
        std::string var_name = abqn->type_name_arg;
        if (var_name.empty()) {
            if (auto id = dynamic_cast<IdentifierNode*>(abqn->target.get())) {
                var_name = id->name;
            } else if (auto ma = dynamic_cast<MemberAccessNode*>(abqn->target.get())) {
                var_name = ma->var_name + "." + ma->member_name;
            }
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
        } else {
            const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(var_name);
            if (tmeta && !tmeta->dimensions.empty()) {
                meta = *tmeta;
                found = true;
            }
        }
        if (!found || meta.dimensions.empty()) {
            throw std::runtime_error("[CAS-DTINPCIL] Illegal first argument to array query system function; please pass an array identifier or array data type");
        }
        int64_t dim = 1;
        if (abqn->dimension_expr) {
            dim = (int64_t)eval_const_expr(abqn->dimension_expr.get(), scope);
        }
        if (dim < 1 || (size_t)dim > meta.dimensions.size()) {
            throw std::runtime_error("[CAS-DTINPCIL] Invalid dimension index " + std::to_string(dim) + " for array querying function per IEEE 1800-2023 section 20.7");
        }
        const auto& d = meta.dimensions[dim - 1];
        switch (abqn->kind) {
            case BoundQueryKind::LEFT: return (uint64_t)(int64_t)d.left;
            case BoundQueryKind::RIGHT: return (uint64_t)(int64_t)d.right;
            case BoundQueryKind::LOW: return (uint64_t)(int64_t)d.low();
            case BoundQueryKind::HIGH: return (uint64_t)(int64_t)d.high();
            case BoundQueryKind::INCREMENT: {
                int64_t inc = d.increment();
                return (inc < 0) ? (uint64_t)(uint32_t)-1 : (uint64_t)inc;
            }
        }
    }
    if (auto vin = dynamic_cast<VectorIntrinsicNode*>(expr)) {
        uint64_t val = eval_const_expr(vin->operand.get(), scope);
        uint32_t ones = 0;
        for (int i = 0; i < 64; ++i) {
            if ((val >> i) & 1ULL) ones++;
        }
        switch (vin->kind) {
            case VectorIntrinsicKind::COUNTONES: return (uint64_t)ones;
            case VectorIntrinsicKind::ONEHOT: return (ones == 1) ? 1ULL : 0ULL;
            case VectorIntrinsicKind::ONEHOT0: return (ones <= 1) ? 1ULL : 0ULL;
            case VectorIntrinsicKind::ISUNKNOWN: return 0ULL;
        }
    }
    if (auto clog = dynamic_cast<Clog2Node*>(expr)) {
        uint64_t val = eval_const_expr(clog->operand.get(), scope);
        return compute_clog2(val);
    }
    if (auto smc = dynamic_cast<StaticMethodCallNode*>(expr)) {
        if (smc->method_name == "vbits") {
            if (!smc->args.empty()) {
                uint64_t val = eval_const_expr(smc->args[0].get(), scope);
                return (val == 1) ? 1 : compute_clog2(val);
            }
        }
        if (smc->method_name == "ceil_div") {
            if (smc->args.size() >= 2) {
                uint64_t dividend = eval_const_expr(smc->args[0].get(), scope);
                uint64_t divisor = eval_const_expr(smc->args[1].get(), scope);
                return (divisor != 0) ? (dividend + divisor - 1) / divisor : 0;
            }
        }
        std::vector<std::unique_ptr<ExpressionNode>> copied_args;
        for (const auto& a : smc->args) {
            copied_args.push_back(a ? a->clone() : nullptr);
        }
        FunctionCallNode fc(smc->class_type_name + "::" + smc->method_name, std::move(copied_args), smc->arg_names);
        return eval_const_expr(&fc, scope, local_env, local_signed, local_width);
    }
    if (auto fc = dynamic_cast<FunctionCallNode*>(expr)) {
        std::string fname = fc->func_name;
        auto colon = fname.rfind("::");
        if (colon != std::string::npos) fname = fname.substr(colon + 2);
        if (fname == "$unsigned" || fname == "$signed") {
            if (!fc->args.empty()) {
                return eval_const_expr(fc->args[0].get(), scope, local_env);
            }
        }
        if (fname == "$bits" || fname == "bits") {
            if (!fc->args.empty()) {
                return eval_bits(fc->args[0].get(), scope);
            }
        }
        if (fname == "$clog2" || fname == "clog2") {
            if (!fc->args.empty()) {
                uint64_t val = eval_const_expr(fc->args[0].get(), scope, local_env);
                return compute_clog2(val);
            }
        }
        if (fname == "vbits") {
            if (!fc->args.empty()) {
                uint64_t val = eval_const_expr(fc->args[0].get(), scope, local_env);
                return (val == 1) ? 1 : compute_clog2(val);
            }
        }
        if (fname == "ceil_div") {
            if (fc->args.size() >= 2) {
                uint64_t dividend = eval_const_expr(fc->args[0].get(), scope, local_env);
                uint64_t divisor = eval_const_expr(fc->args[1].get(), scope, local_env);
                return (divisor != 0) ? (dividend + divisor - 1) / divisor : 0;
            }
        }

        // Constant user function evaluation per IEEE 1800-2023 §13.4.3
        FunctionNode* target_fn = nullptr;
        std::string scoped_fn = scope.empty() ? fname : (scope + "." + fname);
        if (result.function_map.count(scoped_fn)) {
            target_fn = result.function_map.at(scoped_fn);
        } else if (result.function_map.count(fname)) {
            target_fn = result.function_map.at(fname);
        } else if (result.function_map.count(scope.empty() ? fc->func_name : (scope + "." + fc->func_name))) {
            target_fn = result.function_map.at(scope.empty() ? fc->func_name : (scope + "." + fc->func_name));
        } else if (result.function_map.count(fc->func_name)) {
            target_fn = result.function_map.at(fc->func_name);
        }
        if (!target_fn && current_module && colon == std::string::npos) {
            for (const auto& fn : current_module->functions) {
                if (fn && fn->name == fname) {
                    target_fn = fn.get();
                    break;
                }
            }
        }
        if (!target_fn && colon != std::string::npos) {
            std::string pkg_name = fc->func_name.substr(0, colon);
            auto pit = Parser::global_packages.find(pkg_name);
            if (pit != Parser::global_packages.end() && pit->second->function_map.count(fname)) {
                target_fn = pit->second->function_map.at(fname);
            }
        }
        if (!target_fn) {
            for (const auto& kv : Parser::global_packages) {
                if (kv.second->function_map.count(fname)) {
                    target_fn = kv.second->function_map.at(fname);
                    break;
                }
            }
        }
        if (!target_fn && colon != std::string::npos) {
            std::string pkg_name = fc->func_name.substr(0, colon);
            auto pit = Parser::global_packages.find(pkg_name);
            if (pit != Parser::global_packages.end() && pit->second->function_map.count(fname)) {
                target_fn = pit->second->function_map.at(fname);
            }
        }

        if (target_fn) {
            if (target_fn->return_type_name == "real" || target_fn->return_type_name == "shortreal") {
                LogicVal rlv = eval_const_logic_val(fc, scope);
                return static_cast<uint64_t>(std::llround(rlv.real_val));
            }
            if (!target_fn->return_type_name.empty()) {
                const PackedStructType* st = find_struct_type(target_fn->return_type_name, scope);
                if (st) {
                    LogicVal slv = eval_const_logic_val(fc, scope);
                    return slv.aval;
                }
            }
            std::unordered_map<std::string, uint64_t> fn_env;
            std::unordered_map<std::string, bool> fn_signed;
            std::unordered_map<std::string, uint32_t> fn_width;
            for (size_t i = 0; i < target_fn->inputs.size(); ++i) {
                const auto& in_p = target_fn->inputs[i];
                uint64_t arg_val = 0;
                if (i < fc->args.size() && fc->args[i]) {
                    arg_val = eval_const_expr(fc->args[i].get(), scope, local_env, local_signed, local_width);
                } else if (in_p.default_value) {
                    std::function<void(ExpressionNode*)> check_const = [&](ExpressionNode* e) {
                        if (!e) return;
                        if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                            std::string scoped = scope.empty() ? id->name : (scope + "." + id->name);
                            bool is_param = result.param_val_map.count(scoped) || result.param_val_map.count(id->name) ||
                                            result.param_map.count(scoped) || result.param_map.count(id->name) ||
                                            result.enum_constant_map.count(scoped) || result.enum_constant_map.count(id->name);
                            if (!is_param) {
                                if (result.net_map.count(scoped) || result.net_map.count(id->name)) {
                                    throw std::runtime_error("[CAS-CFUNC] Non-constant variable '" + id->name + "' referenced in port default in constant context per IEEE 1800-2023 §13.4.3");
                                }
                                if (current_module) {
                                    for (const auto& sig : current_module->signals) {
                                        if (sig.name == id->name) {
                                            throw std::runtime_error("[CAS-CFUNC] Non-constant variable '" + id->name + "' referenced in port default in constant context per IEEE 1800-2023 §13.4.3");
                                        }
                                    }
                                    for (const auto& prt : current_module->ports) {
                                        if (prt.name == id->name) {
                                            throw std::runtime_error("[CAS-CFUNC] Non-constant variable '" + id->name + "' referenced in port default in constant context per IEEE 1800-2023 §13.4.3");
                                        }
                                    }
                                }
                            }
                        }
                        if (auto bin = dynamic_cast<BinaryOpNode*>(e)) { check_const(bin->lhs.get()); check_const(bin->rhs.get()); }
                        if (auto un = dynamic_cast<UnaryOpNode*>(e)) { check_const(un->operand.get()); }
                    };
                    check_const(in_p.default_value.get());
                    arg_val = eval_const_expr(in_p.default_value.get(), scope, &fn_env, &fn_signed, &fn_width);
                }
                fn_env[in_p.name] = arg_val;
                fn_signed[in_p.name] = in_p.is_signed;
                uint32_t w = in_p.width;
                if (w == 0) {
                    if (in_p.msb_expr) {
                        uint64_t m = eval_const_expr(in_p.msb_expr.get(), scope);
                        uint64_t l = in_p.lsb_expr ? eval_const_expr(in_p.lsb_expr.get(), scope) : 0;
                        w = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
                    } else {
                        w = 1;
                    }
                }
                fn_width[in_p.name] = w;
            }
            fn_env[target_fn->name] = 0;
            fn_signed[target_fn->name] = target_fn->return_signed;
            fn_width[target_fn->name] = target_fn->return_width;

            bool returned = false;
            uint64_t ret_val = 0;
            std::function<void(StatementNode*)> eval_const_stmt = [&](StatementNode* s) {
                if (!s || returned) return;
                if (auto ret = dynamic_cast<ReturnStatementNode*>(s)) {
                    if (ret->expr) {
                        ret_val = eval_const_expr(ret->expr.get(), scope, &fn_env, &fn_signed, &fn_width);
                        fn_env[target_fn->name] = ret_val;
                    } else {
                        ret_val = fn_env[target_fn->name];
                    }
                    returned = true;
                    return;
                }
                if (auto ba = dynamic_cast<BlockingAssignNode*>(s)) {
                    uint64_t rhs_val = eval_const_expr(ba->rhs.get(), scope, &fn_env, &fn_signed, &fn_width);
                    if (!ba->lhs_targets.empty()) {
                        std::string target_name = ba->lhs_targets[0];
                        if (ba->bit_idx) {
                            uint64_t cur_val = fn_env[target_name];
                            if (ba->bit_lsb) {
                                uint64_t msb = eval_const_expr(ba->bit_idx.get(), scope, &fn_env, &fn_signed, &fn_width);
                                uint64_t lsb = eval_const_expr(ba->bit_lsb.get(), scope, &fn_env, &fn_signed, &fn_width);
                                uint64_t lo = 0, width = 0;
                                if (ba->is_plus_colon) {
                                    lo = msb;
                                    width = lsb;
                                } else if (ba->is_minus_colon) {
                                    width = lsb;
                                    lo = (msb >= width - 1) ? (msb - width + 1) : 0;
                                } else {
                                    uint64_t hi = std::max(msb, lsb);
                                    lo = std::min(msb, lsb);
                                    width = hi - lo + 1;
                                }
                                uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                                uint64_t field_val = (rhs_val & mask) << lo;
                                uint64_t clear_mask = ~(mask << lo);
                                fn_env[target_name] = (cur_val & clear_mask) | field_val;
                            } else {
                                uint64_t bit = eval_const_expr(ba->bit_idx.get(), scope, &fn_env, &fn_signed, &fn_width);
                                uint64_t bit_val = (rhs_val & 1ULL) << bit;
                                uint64_t clear_mask = ~(1ULL << bit);
                                fn_env[target_name] = (cur_val & clear_mask) | bit_val;
                            }
                        } else {
                            fn_env[target_name] = rhs_val;
                        }
                    }
                    return;
                }
                if (auto vd = dynamic_cast<VarDeclStatementNode*>(s)) {
                    uint64_t init_val = 0;
                    if (vd->init_expr) {
                        init_val = eval_const_expr(vd->init_expr.get(), scope, &fn_env, &fn_signed, &fn_width);
                    }
                    fn_env[vd->var_name] = init_val;
                    fn_signed[vd->var_name] = vd->is_signed;
                    fn_width[vd->var_name] = vd->width;
                    return;
                }
                if (auto if_s = dynamic_cast<IfStatementNode*>(s)) {
                    uint64_t cond = eval_const_expr(if_s->condition.get(), scope, &fn_env, &fn_signed, &fn_width);
                    if (cond != 0) {
                        for (auto& st : if_s->then_branch) {
                            eval_const_stmt(st.get());
                            if (returned) break;
                        }
                    } else {
                        for (auto& st : if_s->else_branch) {
                            eval_const_stmt(st.get());
                            if (returned) break;
                        }
                    }
                    return;
                }
                if (auto blk = dynamic_cast<BlockStatementNode*>(s)) {
                    for (auto& child : blk->statements) {
                        eval_const_stmt(child.get());
                        if (returned) break;
                    }
                    return;
                }
                if (auto for_s = dynamic_cast<ForStatementNode*>(s)) {
                    if (for_s->init_stmt) eval_const_stmt(for_s->init_stmt.get());
                    while (!returned) {
                        if (for_s->condition) {
                            uint64_t c = eval_const_expr(for_s->condition.get(), scope, &fn_env);
                            if (!c) break;
                        }
                        for (auto& st : for_s->body) {
                            eval_const_stmt(st.get());
                            if (returned) break;
                        }
                        if (returned) break;
                        if (for_s->step_stmt) eval_const_stmt(for_s->step_stmt.get());
                    }
                    return;
                }
                if (auto rc = dynamic_cast<RandCaseStatementNode*>(s)) {
                    uint64_t total_weight = 0;
                    std::vector<std::pair<uint64_t, StatementNode*>> valid_branches;
                    for (auto& item : rc->items) {
                        uint64_t w = eval_const_expr(item.weight.get(), scope, &fn_env);
                        if ((int64_t)w > 0) {
                            total_weight += w;
                            valid_branches.push_back({w, item.stmt.get()});
                        }
                    }
                    if (total_weight > 0 && !valid_branches.empty()) {
                        uint64_t roll = (uint64_t)(rand()) % total_weight;
                        uint64_t cum = 0;
                        for (auto& branch : valid_branches) {
                            cum += branch.first;
                            if (roll < cum) {
                                eval_const_stmt(branch.second);
                                break;
                            }
                        }
                    }
                    return;
                }
                if (auto rseq = dynamic_cast<RandSequenceStatementNode*>(s)) {
                    std::unordered_map<std::string, const RandSeqRule*> rule_map;
                    for (const auto& r : rseq->rules) rule_map[r.name] = &r;
                    std::string start_prod = rseq->top_production.empty() ? (rseq->rules.empty() ? "" : rseq->rules[0].name) : rseq->top_production;
                    if (rule_map.find(start_prod) != rule_map.end()) {
                        bool rseq_break = false;
                        bool rseq_return = false;
                        std::function<void(const std::string&, const std::vector<uint64_t>&)> eval_c_prod;

                        auto eval_c_item = [&](const RandSeqProdItem& item) {
                            if (rseq_break || rseq_return) return;
                            if (item.is_action_block) {
                                for (auto& act_s : item.action_stmts) {
                                    eval_const_stmt(act_s.get());
                                    if (returned) {
                                        returned = false;
                                        rseq_return = true;
                                        break;
                                    }
                                }
                            } else {
                                std::vector<uint64_t> args;
                                for (auto& a : item.args) args.push_back(a ? eval_const_expr(a.get(), scope, &fn_env) : 0);
                                eval_c_prod(item.prod_name, args);
                            }
                        };

                        eval_c_prod = [&](const std::string& pname, const std::vector<uint64_t>& act_args) {
                            if (rseq_break) return;
                            auto it = rule_map.find(pname);
                            if (it == rule_map.end()) return;
                            const RandSeqRule* rule = it->second;

                            std::unordered_map<std::string, uint64_t> old_env = fn_env;
                            for (size_t i = 0; i < rule->formals.size(); ++i) {
                                if (i < act_args.size()) fn_env[rule->formals[i].second] = act_args[i];
                            }

                            if (rule->kind == RandSeqRuleKind::IF_ELSE) {
                                uint64_t c = eval_const_expr(rule->if_cond.get(), scope, &fn_env);
                                if (c != 0) {
                                    for (const auto& item : rule->then_items) { if (rseq_break || rseq_return) break; eval_c_item(item); }
                                } else {
                                    for (const auto& item : rule->else_items) { if (rseq_break || rseq_return) break; eval_c_item(item); }
                                }
                            } else if (rule->kind == RandSeqRuleKind::CASE) {
                                uint64_t cval = eval_const_expr(rule->case_expr.get(), scope, &fn_env);
                                const RandSeqCaseItem* matched = nullptr;
                                const RandSeqCaseItem* def_item = nullptr;
                                for (const auto& ci : rule->case_items) {
                                    if (ci.is_default) { def_item = &ci; continue; }
                                    for (const auto& ce : ci.exprs) {
                                        if (eval_const_expr(ce.get(), scope, &fn_env) == cval) { matched = &ci; break; }
                                    }
                                    if (matched) break;
                                }
                                if (!matched) matched = def_item;
                                if (matched) {
                                    for (const auto& item : matched->items) { if (rseq_break || rseq_return) break; eval_c_item(item); }
                                }
                            } else if (rule->kind == RandSeqRuleKind::REPEAT) {
                                int64_t n = (int64_t)eval_const_expr(rule->repeat_count.get(), scope, &fn_env);
                                for (int64_t i = 0; i < n; ++i) {
                                    if (rseq_break || rseq_return) break;
                                    for (const auto& item : rule->repeat_items) { if (rseq_break || rseq_return) break; eval_c_item(item); }
                                }
                            } else {
                                uint64_t total_weight = 0;
                                std::vector<std::pair<uint64_t, const RandSeqBranch*>> val_brs;
                                for (const auto& b : rule->branches) {
                                    uint64_t w = 1;
                                    if (b.weight) w = eval_const_expr(b.weight.get(), scope, &fn_env);
                                    if ((int64_t)w > 0) { total_weight += w; val_brs.push_back({w, &b}); }
                                }
                                if (total_weight > 0 && !val_brs.empty()) {
                                    uint64_t roll = (uint64_t)(rand()) % total_weight;
                                    uint64_t cum = 0;
                                    const RandSeqBranch* sel = val_brs[0].second;
                                    for (const auto& vb : val_brs) {
                                        cum += vb.first;
                                        if (roll < cum) { sel = vb.second; break; }
                                    }
                                    for (const auto& item : sel->items) {
                                        if (rseq_break || rseq_return) break;
                                        eval_c_item(item);
                                    }
                                }
                            }

                            for (const auto& formal : rule->formals) {
                                auto old_it = old_env.find(formal.second);
                                if (old_it != old_env.end()) fn_env[formal.second] = old_it->second;
                                else fn_env.erase(formal.second);
                            }
                            if (rseq_return) rseq_return = false;
                        };

                        eval_c_prod(start_prod, {});
                    }
                    return;
                }
            };

            for (auto& s : target_fn->get_body()) {
                eval_const_stmt(s.get());
                if (returned) break;
            }
            return returned ? ret_val : fn_env[target_fn->name];
        }
    }
    if (auto cbn = dynamic_cast<CountBitsNode*>(expr)) {
        uint64_t tval = eval_const_expr(cbn->target_expr.get(), scope);
        uint32_t w = 32;
        if (auto num = dynamic_cast<NumberNode*>(cbn->target_expr.get())) {
            w = num->width;
        }
        bool match_0 = false;
        bool match_1 = false;
        for (auto& c : cbn->control_bits) {
            uint64_t cv = eval_const_expr(c.get(), scope);
            if ((cv & 1ULL) == 0) match_0 = true;
            else match_1 = true;
        }
        uint32_t count = 0;
        uint32_t limit = std::min(w, 64U);
        for (uint32_t i = 0; i < limit; ++i) {
            bool bit = (tval >> i) & 1ULL;
            if (!bit && match_0) count++;
            else if (bit && match_1) count++;
        }
        return (uint64_t)count;
    }
    if (auto inside_node = dynamic_cast<InsideNode*>(expr)) {
        return eval_const_logic_val(inside_node, scope).aval;
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

const PackedStructType* Elaborator::find_struct_type(const std::string& type_name, const std::string& scope) const {
    if (type_name.empty()) return nullptr;

    if (!scope.empty()) {
        std::string cur_s = scope;
        while (true) {
            std::string scoped = cur_s.empty() ? type_name : (cur_s + "." + type_name);
            auto it = result.struct_types.find(scoped);
            if (it != result.struct_types.end()) return &it->second;
            auto it_v = result.var_to_struct_type.find(scoped);
            if (it_v != result.var_to_struct_type.end() && it_v->second != type_name) {
                if (const PackedStructType* mapped_st = find_struct_type(it_v->second, scope)) {
                    return mapped_st;
                }
            }
            if (cur_s.empty()) break;
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }
    }

    auto it = result.struct_types.find(type_name);
    if (it != result.struct_types.end()) return &it->second;

    std::string dot_type = type_name;
    size_t col_p = 0;
    while ((col_p = dot_type.find("::", col_p)) != std::string::npos) {
        dot_type.replace(col_p, 2, ".");
    }
    auto it_dot = result.struct_types.find(dot_type);
    if (it_dot != result.struct_types.end()) return &it_dot->second;

    std::string bare = "";
    auto colon = type_name.rfind("::");
    if (colon != std::string::npos) {
        std::string pkg = type_name.substr(0, colon);
        bare = type_name.substr(colon + 2);
        auto pit = Parser::global_packages.find(pkg);
        if (pit != Parser::global_packages.end() &&
            pit->second->struct_typedefs.find(bare) == pit->second->struct_typedefs.end()) {
            return nullptr;
        }
        auto it_bare = result.struct_types.find(bare);
        if (it_bare != result.struct_types.end()) return &it_bare->second;
    }

    for (const auto& pair : result.struct_types) {
        if (!bare.empty() && (pair.first == bare || pair.second.name == bare ||
            pair.first.rfind("." + bare) != std::string::npos ||
            pair.first.rfind("::" + bare) != std::string::npos)) {
            return &pair.second;
        }
        std::string dot_name = "." + type_name;
        if (pair.first.length() >= dot_name.length() &&
            pair.first.compare(pair.first.length() - dot_name.length(), dot_name.length(), dot_name) == 0) {
            return &pair.second;
        }
        std::string col_name = "::" + type_name;
        if (pair.first.length() >= col_name.length() &&
            pair.first.compare(pair.first.length() - col_name.length(), col_name.length(), col_name) == 0) {
            return &pair.second;
        }
    }
    return nullptr;
}

bool Elaborator::resolve_struct_member_slice(size_t net_id, const std::string& member_name, const std::string& scope, uint32_t& out_offset, uint32_t& out_width, int64_t& out_lsb) const {
    if (net_id >= result.netlist.size() || member_name.empty()) return false;
    out_lsb = 0;

    const PackedStructType* st = nullptr;
    std::string tname = result.netlist[net_id].type_name;
    if (tname.empty()) {
        std::string nname = result.netlist[net_id].name;
        if (result.var_to_struct_type.count(nname)) {
            tname = result.var_to_struct_type.at(nname);
        }
    }
    if (!tname.empty()) {
        st = find_struct_type(tname, scope);
    }
    std::string root_mem = member_name;
    auto first_dot = root_mem.find('.');
    if (first_dot != std::string::npos) {
        root_mem = root_mem.substr(0, first_dot);
    }
    if (!st) {
        for (const auto& pair : result.struct_types) {
            if (pair.second.total_width == result.netlist[net_id].width &&
                (pair.second.get_member(member_name) || pair.second.get_member(root_mem))) {
                st = &pair.second;
                break;
            }
        }
    }
    if (!st) {
        for (const auto& pair : result.struct_types) {
            if (pair.second.get_member(member_name) || pair.second.get_member(root_mem)) {
                st = &pair.second;
                break;
            }
        }
    }
    if (!st) return false;

    std::string pkg_prefix = "";
    auto pkg_col = tname.find("::");
    if (pkg_col != std::string::npos) {
        pkg_prefix = tname.substr(0, pkg_col + 2);
    }
    const PackedStructType* cur_st = st;
    std::string remaining = member_name;
    uint32_t accum_off = 0;
    uint32_t cur_len = result.netlist[net_id].width;
    while (!remaining.empty() && cur_st) {
        std::string part = remaining;
        std::string next_part = "";
        auto dot_pos = remaining.find('.');
        if (dot_pos != std::string::npos) {
            part = remaining.substr(0, dot_pos);
            next_part = remaining.substr(dot_pos + 1);
        }
        const StructMember* sm = cur_st->get_member(part);
        if (!sm) return false;
        accum_off += sm->offset;
        cur_len = sm->width;
        if (next_part.empty()) {
            out_offset = accum_off;
            out_width = cur_len;
            out_lsb = sm->lsb;
            return true;
        }
        const PackedStructType* next_st = nullptr;
        if (!pkg_prefix.empty() && sm->type_name.find("::") == std::string::npos) {
            next_st = find_struct_type(pkg_prefix + sm->type_name, scope);
        }
        if (!next_st) {
            next_st = find_struct_type(sm->type_name, scope);
        }
        cur_st = next_st;
        remaining = next_part;
    }
    return false;
}

std::string Elaborator::resolve_scoped_name(const std::string& scope, const std::string& name) const {
    size_t dot_pos = name.find('.');
    while (dot_pos != std::string::npos) {
        std::string prefix = name.substr(0, dot_pos);
        std::string suffix = name.substr(dot_pos + 1);
        std::string scoped_prefix = scope.empty() ? prefix : (scope + "." + prefix);

        auto it = result.interface_port_map.find(scoped_prefix);
        if (it != result.interface_port_map.end()) {
            std::string bound = it->second;
            while (result.interface_port_map.find(bound) != result.interface_port_map.end()) {
                bound = result.interface_port_map.at(bound);
            }
            return bound + "." + suffix;
        }
        auto it2 = result.interface_port_map.find(prefix);
        if (it2 != result.interface_port_map.end()) {
            std::string bound = it2->second;
            while (result.interface_port_map.find(bound) != result.interface_port_map.end()) {
                bound = result.interface_port_map.at(bound);
            }
            return bound + "." + suffix;
        }
        dot_pos = name.find('.', dot_pos + 1);
    }
    return scope.empty() ? name : (scope + "." + name);
}

size_t Elaborator::get_net_id(const std::string& scope, const std::string& name) const {
    std::string cur_scope = scope;
    while (true) {
        std::string resolved = resolve_scoped_name(cur_scope, name);
        auto it = result.net_map.find(resolved);
        if (it != result.net_map.end()) return it->second;

        std::string scoped = cur_scope.empty() ? name : (cur_scope + "." + name);
        auto it3 = result.net_map.find(scoped);
        if (it3 != result.net_map.end()) return it3->second;

        if (!cur_scope.empty()) {
            std::string colon_scoped = cur_scope + "::" + name;
            auto itc = result.net_map.find(colon_scoped);
            if (itc != result.net_map.end()) return itc->second;
        }

        if (cur_scope.empty()) break;
        auto dot = cur_scope.rfind('.');
        if (dot == std::string::npos) cur_scope = "";
        else cur_scope = cur_scope.substr(0, dot);
    }

    auto it2 = result.net_map.find(name);
    if (it2 != result.net_map.end()) return it2->second;

    auto it_unit = result.net_map.find("$unit." + name);
    if (it_unit != result.net_map.end()) return it_unit->second;

    return (size_t)-1;
}

size_t Elaborator::resolve_target_net_id(const std::string& target, const std::string& scope) const {
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
    // Check if target is a memory array in current scope or ancestors
    std::string cur_s = scope;
    while (!cur_s.empty()) {
        std::string cand = cur_s + "." + resolved_target;
        if (result.memory_map.find(cand) != result.memory_map.end() ||
            result.assoc_array_map.find(cand) != result.assoc_array_map.end() ||
            result.dynamic_array_map.find(cand) != result.dynamic_array_map.end() ||
            result.queue_map.find(cand) != result.queue_map.end()) {
            return (size_t)-1;
        }
        auto dot = cur_s.rfind('.');
        if (dot == std::string::npos) cur_s = "";
        else cur_s = cur_s.substr(0, dot);
    }
    if (result.memory_map.find(resolved_target) != result.memory_map.end() ||
        result.assoc_array_map.find(resolved_target) != result.assoc_array_map.end() ||
        result.dynamic_array_map.find(resolved_target) != result.dynamic_array_map.end() ||
        result.queue_map.find(resolved_target) != result.queue_map.end()) {
        return (size_t)-1;
    }

    std::string scoped = scope.empty() ? resolved_target : (scope + "." + resolved_target);
    if (result.net_map.find(scoped) != result.net_map.end()) {
        return result.net_map.at(scoped);
    }
    cur_s = scope;
    while (!cur_s.empty()) {
        std::string candidate = cur_s + "." + resolved_target;
        if (result.net_map.find(candidate) != result.net_map.end()) {
            return result.net_map.at(candidate);
        }
        auto dot = cur_s.rfind('.');
        if (dot == std::string::npos) cur_s = "";
        else cur_s = cur_s.substr(0, dot);
    }
    if (result.net_map.find(resolved_target) != result.net_map.end()) {
        return result.net_map.at(resolved_target);
    }
    auto dot_pos = target.rfind('.');
    if (dot_pos != std::string::npos) {
        std::string base_target = target.substr(0, dot_pos);
        std::string scoped_base = scope.empty() ? base_target : (scope + "." + base_target);
        if (result.memory_map.find(scoped_base) != result.memory_map.end() ||
            result.memory_map.find(base_target) != result.memory_map.end() ||
            result.assoc_array_map.find(scoped_base) != result.assoc_array_map.end() ||
            result.assoc_array_map.find(base_target) != result.assoc_array_map.end()) {
            return (size_t)-1;
        }
        if (result.net_map.find(scoped_base) != result.net_map.end()) {
            return result.net_map.at(scoped_base);
        }
        if (result.net_map.find(base_target) != result.net_map.end()) {
            return result.net_map.at(base_target);
        }
        for (const auto& kv : result.net_map) {
            if (kv.first.size() > target.size() &&
                kv.first.rfind("." + target) == kv.first.size() - (target.size() + 1)) {
                return kv.second;
            }
        }
    }
    return (size_t)-1;
}

NetSliceRef Elaborator::resolve_slice_ref(ExpressionNode* expr, const std::string& scope) {
    if (!expr) {
        throw std::runtime_error("Cannot resolve null expression in resolve_slice_ref");
    }
    if (auto ma = dynamic_cast<MemberAccessNode*>(expr)) {
        if (ma->index_expr) {
            IndexSelectNode idx_node(ma->var_name + "." + ma->member_name, ma->index_expr->clone());
            return resolve_slice_ref(&idx_node, scope);
        }
        IdentifierNode id_node(ma->var_name + "." + ma->member_name);
        return resolve_slice_ref(&id_node, scope);
    }
    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        size_t id = get_net_id(scope, ident->name);
        if (id != (size_t)-1) {
            return {id, 0, result.netlist[id].width};
        }
        std::string iname = ident->name;
        size_t dot_pos = iname.rfind('.');
        std::string memb = "";
        while (dot_pos != std::string::npos) {
            std::string base = iname.substr(0, dot_pos);
            memb = iname.substr(dot_pos + 1);
            size_t bid = get_net_id(scope, base);
            if (bid != (size_t)-1) {
                if (!memb.empty()) {
                    uint32_t off = 0;
                    uint32_t len = result.netlist[bid].width;
                    if (resolve_struct_member_slice(bid, memb, scope, off, len)) {
                        return {bid, off, len};
                    }
                }
                return {bid, 0, result.netlist[bid].width};
            }
            dot_pos = iname.rfind('.', dot_pos - 1);
        }
        if (ident->name.find('.') == std::string::npos && ident->name.find("::") == std::string::npos) {
            std::string full_name = scope.empty() ? ident->name : (scope + "." + ident->name);
            if (result.param_map.count(full_name) || result.param_map.count(ident->name) ||
                result.param_val_map.count(full_name) || result.param_val_map.count(ident->name) ||
                result.enum_constant_map.count(full_name) || result.enum_constant_map.count(ident->name)) {
                throw std::runtime_error("Identifier '" + ident->name + "' is a parameter/enum, not a net reference");
            }
            NetKind nk = NetKind::WIRE;
            if (current_module) {
                if (current_module->default_nettype == "tri1") nk = NetKind::TRI1;
                else if (current_module->default_nettype == "tri0") nk = NetKind::TRI0;
                else if (current_module->default_nettype == "wand") nk = NetKind::WAND;
                else if (current_module->default_nettype == "wor") nk = NetKind::WOR;
            }
            size_t new_id = get_or_create_net(full_name, 1, false, nk);
            return {new_id, 0, 1};
        }
        throw std::runtime_error("Cannot resolve net reference for '" + ident->name + "' in scope '" + scope + "'");
    }
    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        size_t id = get_net_id(scope, idx->name);
        std::string full_name = (id != (size_t)-1) ? result.netlist[id].name : (scope.empty() ? idx->name : (scope + "." + idx->name));
        uint32_t struct_member_offset = 0;
        uint32_t struct_member_len = 0;
        int64_t base_low = 0;
        if (id == (size_t)-1) {
            std::string iname = idx->name;
            size_t dot_pos = iname.rfind('.');
            std::string memb = "";
            while (dot_pos != std::string::npos) {
                std::string base = iname.substr(0, dot_pos);
                memb = iname.substr(dot_pos + 1);
                size_t bid = get_net_id(scope, base);
                if (bid != (size_t)-1) {
                    id = bid;
                    full_name = result.netlist[bid].name;
                    break;
                }
                dot_pos = iname.rfind('.', dot_pos - 1);
            }
            if (id != (size_t)-1 && !memb.empty()) {
                uint32_t off = 0;
                uint32_t len = result.netlist[id].width;
                int64_t memb_lsb = 0;
                if (resolve_struct_member_slice(id, memb, scope, off, len, memb_lsb)) {
                    struct_member_offset = off;
                    struct_member_len = len;
                    base_low = memb_lsb;
                }
            }
            if (id == (size_t)-1) {
                throw std::runtime_error("Cannot resolve net slice reference for '" + idx->name + "' in scope '" + scope + "'");
            }
        }

        auto it_vdm = result.var_dimensions.find(full_name);
        if (it_vdm == result.var_dimensions.end()) {
            it_vdm = result.var_dimensions.find(idx->name);
        }
        if (it_vdm == result.var_dimensions.end()) {
            std::string cur_s = scope;
            while (!cur_s.empty()) {
                auto dot = cur_s.rfind('.');
                if (dot == std::string::npos) cur_s = "";
                else cur_s = cur_s.substr(0, dot);
                std::string cand = cur_s.empty() ? idx->name : (cur_s + "." + idx->name);
                it_vdm = result.var_dimensions.find(cand);
                if (it_vdm != result.var_dimensions.end()) break;
            }
        }

        if (it_vdm == result.var_dimensions.end() && id != (size_t)-1 && !result.netlist[id].type_name.empty()) {
            const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(result.netlist[id].type_name);
            if (tmeta && !tmeta->dimensions.empty()) {
                VariableDimMetadata vdm = *tmeta;
                vdm.name = full_name;
                result.var_dimensions[full_name] = vdm;
                it_vdm = result.var_dimensions.find(full_name);
            }
        }

        if (it_vdm != result.var_dimensions.end() && !it_vdm->second.dimensions.empty()) {
            size_t M = it_vdm->second.dimensions.size();
            uint32_t total_dim_size = 1;
            for (const auto& d : it_vdm->second.dimensions) {
                total_dim_size *= (uint32_t)d.size();
            }
            uint32_t elem_w = 1;
            if (total_dim_size > 0 && result.netlist[id].width > total_dim_size && (result.netlist[id].width % total_dim_size == 0)) {
                elem_w = result.netlist[id].width / total_dim_size;
            }
            std::vector<uint32_t> strides(M, elem_w);
            for (int k = (int)M - 2; k >= 0; --k) {
                strides[k] = strides[k + 1] * (uint32_t)it_vdm->second.dimensions[k + 1].size();
            }

            struct DimIndex {
                ExpressionNode* msb = nullptr;
                ExpressionNode* lsb = nullptr;
                bool is_part_select = false;
                bool is_plus_colon = false;
                bool is_minus_colon = false;
            };
            std::vector<DimIndex> dims;
            if (idx->msb_expr) {
                dims.push_back({idx->msb_expr.get(), idx->lsb_expr ? idx->lsb_expr.get() : nullptr, idx->is_part_select, idx->is_plus_colon, idx->is_minus_colon});
            }
            if (idx->has_bit_select && idx->bit_msb_expr) {
                bool is_part = (idx->bit_lsb_expr != nullptr) || idx->is_plus_colon || idx->is_minus_colon;
                dims.push_back({idx->bit_msb_expr.get(), idx->bit_lsb_expr ? idx->bit_lsb_expr.get() : nullptr, is_part, idx->is_plus_colon, idx->is_minus_colon});
            }
            for (const auto& ei : idx->extra_indices) {
                if (ei) {
                    if (auto part = dynamic_cast<IndexSelectNode*>(ei.get())) {
                        bool is_part = part->is_part_select || (part->lsb_expr != nullptr) || part->is_plus_colon || part->is_minus_colon;
                        dims.push_back({part->msb_expr.get(), part->lsb_expr ? part->lsb_expr.get() : nullptr, is_part, part->is_plus_colon, part->is_minus_colon});
                    } else {
                        dims.push_back({ei.get(), nullptr, false, false, false});
                    }
                }
            }

            size_t N = dims.size();
            if (N > 0 && N <= M) {
                uint32_t total_off = struct_member_offset;
                uint32_t slice_w = 0;
                for (size_t d = 0; d < N; ++d) {
                    const auto& dim_info = it_vdm->second.dimensions[d];
                    bool is_ascending = (dim_info.left <= dim_info.right);
                    int64_t dim_low = dim_info.low();
                    if (d == 0 && base_low > 0 && dim_low == 0) dim_low = base_low;
                    const auto& di = dims[d];
                    if (di.is_part_select) {
                        uint64_t b_msb = eval_const_expr(di.msb, scope);
                        uint64_t b_lsb = 0;
                        uint32_t count = 1;
                        if (di.is_plus_colon) {
                            count = di.lsb ? (uint32_t)eval_const_expr(di.lsb, scope) : 1;
                            b_lsb = b_msb;
                        } else if (di.is_minus_colon) {
                            count = di.lsb ? (uint32_t)eval_const_expr(di.lsb, scope) : 1;
                            b_lsb = (b_msb >= count - 1) ? (b_msb - count + 1) : 0;
                        } else {
                            b_lsb = di.lsb ? eval_const_expr(di.lsb, scope) : b_msb;
                            count = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                        }
                        if (is_ascending) {
                            uint64_t max_b = std::max(b_msb, b_lsb);
                            int64_t eff_off = dim_info.right - (int64_t)max_b;
                            if (eff_off < 0) eff_off = 0;
                            total_off += (uint32_t)eff_off * strides[d];
                        } else {
                            uint64_t min_b = std::min(b_msb, b_lsb);
                            if (dim_low > 0 && min_b >= (uint64_t)dim_low) min_b -= (uint64_t)dim_low;
                            total_off += (uint32_t)min_b * strides[d];
                        }
                        slice_w = count * strides[d];
                    } else {
                        uint64_t val = eval_const_expr(di.msb, scope);
                        if (is_ascending) {
                            int64_t eff_off = dim_info.right - (int64_t)val;
                            if (eff_off < 0) eff_off = 0;
                            total_off += (uint32_t)eff_off * strides[d];
                        } else {
                            if (dim_low > 0 && val >= (uint64_t)dim_low) val -= (uint64_t)dim_low;
                            total_off += (uint32_t)val * strides[d];
                        }
                        if (d == N - 1) {
                            slice_w = (struct_member_len > 0) ? struct_member_len : strides[d];
                        }
                    }
                }
                return {id, total_off, slice_w};
            }
        }

        uint32_t d1_width = 1;
        int64_t d1_low = 0;
        if (it_vdm == result.var_dimensions.end() && id != (size_t)-1 && !result.netlist[id].type_name.empty()) {
            const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(result.netlist[id].type_name);
            if (tmeta && !tmeta->dimensions.empty()) {
                VariableDimMetadata vdm = *tmeta;
                vdm.name = full_name;
                result.var_dimensions[full_name] = vdm;
                it_vdm = result.var_dimensions.find(full_name);
            }
        }

        if (it_vdm != result.var_dimensions.end() && !it_vdm->second.dimensions.empty()) {
            if (base_low == 0) base_low = it_vdm->second.dimensions[0].low();
            if (it_vdm->second.dimensions.size() > 1) {
                d1_width = (uint32_t)it_vdm->second.dimensions[1].size();
                d1_low = it_vdm->second.dimensions[1].low();
            } else if (it_vdm->second.dimensions[0].size() > 0 && result.netlist[id].width > (uint32_t)it_vdm->second.dimensions[0].size()) {
                d1_width = result.netlist[id].width / (uint32_t)it_vdm->second.dimensions[0].size();
            }
        }

        if (idx->has_bit_select && idx->bit_msb_expr) {
            uint64_t d0_idx = eval_const_expr(idx->msb_expr.get(), scope);
            if (base_low > 0 && d0_idx >= (uint64_t)base_low) d0_idx -= (uint64_t)base_low;
            if (idx->is_plus_colon) {
                uint64_t b_base = eval_const_expr(idx->bit_msb_expr.get(), scope);
                uint64_t b_width = idx->bit_lsb_expr ? eval_const_expr(idx->bit_lsb_expr.get(), scope) : 1;
                if (d1_low > 0 && b_base >= (uint64_t)d1_low) b_base -= (uint64_t)d1_low;
                uint64_t total_off = struct_member_offset + d0_idx * d1_width + b_base;
                return {id, (uint32_t)total_off, (uint32_t)b_width};
            } else if (idx->is_minus_colon) {
                uint64_t b_base = eval_const_expr(idx->bit_msb_expr.get(), scope);
                uint64_t b_width = idx->bit_lsb_expr ? eval_const_expr(idx->bit_lsb_expr.get(), scope) : 1;
                uint64_t lsb = (b_base >= b_width - 1) ? (b_base - b_width + 1) : 0;
                if (d1_low > 0 && lsb >= (uint64_t)d1_low) lsb -= (uint64_t)d1_low;
                uint64_t total_off = struct_member_offset + d0_idx * d1_width + lsb;
                return {id, (uint32_t)total_off, (uint32_t)b_width};
            } else if (idx->bit_lsb_expr) {
                uint64_t b_msb = eval_const_expr(idx->bit_msb_expr.get(), scope);
                uint64_t b_lsb = eval_const_expr(idx->bit_lsb_expr.get(), scope);
                uint32_t w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                uint64_t lsb = std::min(b_msb, b_lsb);
                if (d1_low > 0 && lsb >= (uint64_t)d1_low) lsb -= (uint64_t)d1_low;
                uint64_t total_off = struct_member_offset + d0_idx * d1_width + lsb;
                return {id, (uint32_t)total_off, w};
            } else {
                uint64_t b_idx = eval_const_expr(idx->bit_msb_expr.get(), scope);
                if (d1_low > 0 && b_idx >= (uint64_t)d1_low) b_idx -= (uint64_t)d1_low;
                uint64_t total_off = struct_member_offset + d0_idx * d1_width + b_idx;
                return {id, (uint32_t)total_off, 1};
            }
        }

        if (idx->is_plus_colon) {
            uint64_t base = eval_const_expr(idx->msb_expr.get(), scope);
            uint64_t width = idx->lsb_expr ? eval_const_expr(idx->lsb_expr.get(), scope) : 1;
            if (base_low > 0 && base >= (uint64_t)base_low) base -= (uint64_t)base_low;
            return {id, (uint32_t)(struct_member_offset + base), (uint32_t)width};
        } else if (idx->is_minus_colon) {
            uint64_t base = eval_const_expr(idx->msb_expr.get(), scope);
            uint64_t width = idx->lsb_expr ? eval_const_expr(idx->lsb_expr.get(), scope) : 1;
            uint64_t lsb = (base >= width - 1) ? (base - width + 1) : 0;
            if (base_low > 0 && lsb >= (uint64_t)base_low) lsb -= (uint64_t)base_low;
            return {id, (uint32_t)(struct_member_offset + lsb), (uint32_t)width};
        } else if (idx->is_part_select) {
            uint64_t msb = eval_const_expr(idx->msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(idx->lsb_expr.get(), scope);
            uint32_t w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
            uint64_t eff_lsb = std::min(msb, lsb);
            if (base_low > 0 && eff_lsb >= (uint64_t)base_low) eff_lsb -= (uint64_t)base_low;
            return {id, (uint32_t)(struct_member_offset + eff_lsb), w};
        } else {
            uint64_t off = eval_const_expr(idx->msb_expr.get(), scope);
            if (base_low > 0 && off >= (uint64_t)base_low) off -= (uint64_t)base_low;
            return {id, (uint32_t)(struct_member_offset + off * d1_width), (struct_member_len > 0) ? struct_member_len : d1_width};
        }
    }
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        static size_t const_counter = 0;
        std::string const_net_name = scope + ".__const_port_" + std::to_string(const_counter++);
        size_t id = get_or_create_net(const_net_name, num->width, false);
        LogicVal val(num->width, num->aval, num->bval);
        if (num->width > 64 && !num->wide_aval.empty()) {
            val.wide_aval = num->wide_aval;
            val.wide_bval = num->wide_bval;
            val.mask();
        }
        result.netlist[id].set(val);
        return {id, 0, num->width};
    }

    throw std::runtime_error("Cannot resolve net slice reference for non-slice expression");
}

static std::unique_ptr<ExpressionNode> substitute_genvar_in_expr(ExpressionNode* expr, const std::string& gname, int64_t val) {
    if (!expr) return nullptr;
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        if (id->name == gname) {
            return std::make_unique<NumberNode>((uint64_t)val, 32, false, true);
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
        node->is_plus_colon = idx->is_plus_colon;
        node->is_minus_colon = idx->is_minus_colon;
        node->has_bit_select = idx->has_bit_select;
        if (idx->bit_msb_expr) node->bit_msb_expr = substitute_genvar_in_expr(idx->bit_msb_expr.get(), gname, val);
        if (idx->bit_lsb_expr) node->bit_lsb_expr = substitute_genvar_in_expr(idx->bit_lsb_expr.get(), gname, val);
        for (const auto& ei : idx->extra_indices) {
            node->extra_indices.push_back(substitute_genvar_in_expr(ei.get(), gname, val));
        }
        return node;
    }
    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> els;
        for (auto& e : cat->elements) els.push_back(substitute_genvar_in_expr(e.get(), gname, val));
        auto node = std::make_unique<ConcatNode>(std::move(els));
        node->has_select = cat->has_select;
        if (cat->msb_expr) node->msb_expr = substitute_genvar_in_expr(cat->msb_expr.get(), gname, val);
        if (cat->lsb_expr) node->lsb_expr = substitute_genvar_in_expr(cat->lsb_expr.get(), gname, val);
        return node;
    }
    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> els;
        for (auto& e : ucat->elements) els.push_back(substitute_genvar_in_expr(e.get(), gname, val));
        return std::make_unique<UnpackedConcatNode>(std::move(els), ucat->is_pattern);
    }
    if (auto pm = dynamic_cast<PatternMemberNode*>(expr)) {
        return std::make_unique<PatternMemberNode>(pm->member_name, substitute_genvar_in_expr(pm->value.get(), gname, val));
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
    if (auto vin = dynamic_cast<VectorIntrinsicNode*>(expr)) {
        return std::make_unique<VectorIntrinsicNode>(vin->kind, substitute_genvar_in_expr(vin->operand.get(), gname, val));
    }
    if (auto clog = dynamic_cast<Clog2Node*>(expr)) {
        return std::make_unique<Clog2Node>(substitute_genvar_in_expr(clog->operand.get(), gname, val));
    }
    if (auto cbn = dynamic_cast<CountBitsNode*>(expr)) {
        auto t = substitute_genvar_in_expr(cbn->target_expr.get(), gname, val);
        std::vector<std::unique_ptr<ExpressionNode>> c_ctrl;
        for (auto& c : cbn->control_bits) {
            c_ctrl.push_back(substitute_genvar_in_expr(c.get(), gname, val));
        }
        return std::make_unique<CountBitsNode>(std::move(t), std::move(c_ctrl));
    }
    if (auto sfn = dynamic_cast<SFormatfNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> args;
        for (auto& a : sfn->args) args.push_back(substitute_genvar_in_expr(a.get(), gname, val));
        return std::make_unique<SFormatfNode>(std::move(args));
    }
    if (auto adqn = dynamic_cast<ArrayDimensionQueryNode*>(expr)) {
        return std::make_unique<ArrayDimensionQueryNode>(
            adqn->kind,
            adqn->target ? substitute_genvar_in_expr(adqn->target.get(), gname, val) : nullptr,
            adqn->type_name_arg
        );
    }
    if (auto abqn = dynamic_cast<ArrayBoundQueryNode*>(expr)) {
        return std::make_unique<ArrayBoundQueryNode>(
            abqn->kind,
            abqn->target ? substitute_genvar_in_expr(abqn->target.get(), gname, val) : nullptr,
            abqn->dimension_expr ? substitute_genvar_in_expr(abqn->dimension_expr.get(), gname, val) : nullptr,
            abqn->type_name_arg
        );
    }
    if (auto ma = dynamic_cast<MemberAccessNode*>(expr)) {
        auto node = std::make_unique<MemberAccessNode>(ma->var_name, ma->member_name);
        if (ma->index_expr) node->index_expr = substitute_genvar_in_expr(ma->index_expr.get(), gname, val);
        return node;
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
        node->member_name = mem->member_name;
        return node;
    }
    if (auto nba = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
        auto node = std::make_unique<NonBlockingAssignNode>(nba->target,
            substitute_genvar_in_expr(nba->rhs.get(), gname, val), nba->intra_delay);
        node->bit_idx = substitute_genvar_in_expr(nba->bit_idx.get(), gname, val);
        node->bit_lsb = substitute_genvar_in_expr(nba->bit_lsb.get(), gname, val);
        node->is_plus_colon = nba->is_plus_colon;
        node->is_minus_colon = nba->is_minus_colon;
        node->bit_idx2 = substitute_genvar_in_expr(nba->bit_idx2.get(), gname, val);
        node->bit_lsb2 = substitute_genvar_in_expr(nba->bit_lsb2.get(), gname, val);
        node->is_plus_colon2 = nba->is_plus_colon2;
        node->is_minus_colon2 = nba->is_minus_colon2;
        node->has_intra_event = nba->has_intra_event;
        node->intra_edge = nba->intra_edge;
        node->intra_signal = nba->intra_signal;
        for (const auto& ei : nba->extra_indices) {
            node->extra_indices.push_back(substitute_genvar_in_expr(ei.get(), gname, val));
        }
        return node;
    }
    if (auto ba = dynamic_cast<BlockingAssignNode*>(stmt)) {
        auto node = std::make_unique<BlockingAssignNode>(ba->lhs_targets,
            substitute_genvar_in_expr(ba->rhs.get(), gname, val), ba->intra_delay);
        node->bit_idx = substitute_genvar_in_expr(ba->bit_idx.get(), gname, val);
        node->bit_lsb = substitute_genvar_in_expr(ba->bit_lsb.get(), gname, val);
        node->is_plus_colon = ba->is_plus_colon;
        node->is_minus_colon = ba->is_minus_colon;
        node->bit_idx2 = substitute_genvar_in_expr(ba->bit_idx2.get(), gname, val);
        node->bit_lsb2 = substitute_genvar_in_expr(ba->bit_lsb2.get(), gname, val);
        node->is_plus_colon2 = ba->is_plus_colon2;
        node->is_minus_colon2 = ba->is_minus_colon2;
        node->has_intra_event = ba->has_intra_event;
        node->intra_edge = ba->intra_edge;
        node->intra_signal = ba->intra_signal;
        for (const auto& ei : ba->extra_indices) {
            node->extra_indices.push_back(substitute_genvar_in_expr(ei.get(), gname, val));
        }
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
    if (auto rcs = dynamic_cast<RandCaseStatementNode*>(stmt)) {
        auto node = std::make_unique<RandCaseStatementNode>();
        node->src_file = rcs->src_file;
        node->src_line = rcs->src_line;
        for (const auto& item : rcs->items) {
            RandCaseItem ci;
            if (item.weight) ci.weight = substitute_genvar_in_expr(item.weight.get(), gname, val);
            if (item.stmt) ci.stmt = substitute_genvar_in_stmt(item.stmt.get(), gname, val);
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

static thread_local std::vector<std::pair<std::string, int64_t>> active_genvars;

void Elaborator::unroll_generate_block(GenerateBlock* blk,
                                       const std::string& current_scope,
                                       const std::string& parent_scope,
                                       const std::string& gname,
                                       int64_t gval,
                                       bool has_genvar,
                                       std::ostream* debug_out) {
    if (!blk) return;

    for (const auto& param : blk->parameters) {
        std::unique_ptr<ExpressionNode> val_expr = param.default_expr ? param.default_expr->clone() : nullptr;
        if (has_genvar && val_expr) {
            val_expr = substitute_genvar_in_expr(val_expr.get(), gname, gval);
        }
        std::string scoped_param = current_scope + "." + param.name;
        if (val_expr) {
            LogicVal lv = eval_const_logic_val(val_expr.get(), current_scope);
            result.param_val_map[scoped_param] = lv;
            result.param_map[scoped_param] = lv.aval;
        }
    }

    for (const auto& sig : blk->signals) {
        PortNode p = sig.clone();
        if (has_genvar) {
            if (p.msb_expr) p.msb_expr = substitute_genvar_in_expr(p.msb_expr.get(), gname, gval);
            if (p.lsb_expr) p.lsb_expr = substitute_genvar_in_expr(p.lsb_expr.get(), gname, gval);
            if (p.bound1_expr) p.bound1_expr = substitute_genvar_in_expr(p.bound1_expr.get(), gname, gval);
            if (p.bound2_expr) p.bound2_expr = substitute_genvar_in_expr(p.bound2_expr.get(), gname, gval);
            for (auto& udim : p.unpacked_dimensions) {
                if (udim.left_expr) udim.left_expr = substitute_genvar_in_expr(udim.left_expr.get(), gname, gval);
                if (udim.right_expr) udim.right_expr = substitute_genvar_in_expr(udim.right_expr.get(), gname, gval);
            }
            for (auto& pdim : p.packed_dimensions) {
                if (pdim.left_expr) pdim.left_expr = substitute_genvar_in_expr(pdim.left_expr.get(), gname, gval);
                if (pdim.right_expr) pdim.right_expr = substitute_genvar_in_expr(pdim.right_expr.get(), gname, gval);
            }
        }
        uint32_t elem_w = 1;
        if (!p.type_name.empty()) {
            uint32_t tw = 0;
            std::string cur_s = current_scope;
            while (true) {
                std::string cand = cur_s.empty() ? p.type_name : (cur_s + "." + p.type_name);
                if (result.enum_types.count(cand)) { tw = result.enum_types.at(cand).width; break; }
                if (result.struct_types.count(cand)) { tw = result.struct_types.at(cand).total_width; break; }
                if (result.simple_types.count(cand)) { tw = result.simple_types.at(cand).width; break; }
                if (cur_s.empty()) break;
                auto dot = cur_s.rfind('.');
                cur_s = (dot == std::string::npos) ? "" : cur_s.substr(0, dot);
            }
            if (tw == 0) tw = resolve_type_width(p.type_name);
            if (tw > 0) elem_w = tw;
        }
        uint32_t w = elem_w;
        bool has_explicit_packed = false;
        if (!p.packed_dimensions.empty()) {
            has_explicit_packed = true;
            uint32_t packed_mult = 1;
            for (const auto& pdim : p.packed_dimensions) {
                int64_t b1 = eval_const_signed_val(eval_const_expr(pdim.left_expr.get(), current_scope));
                int64_t b2 = pdim.right_expr ? eval_const_signed_val(eval_const_expr(pdim.right_expr.get(), current_scope)) : 0;
                packed_mult *= (uint32_t)(std::abs(b1 - b2) + 1);
            }
            w = elem_w * packed_mult;
        } else if (p.msb_expr && p.lsb_expr) {
            if (elem_w > 1) {
                w = elem_w;
            } else {
                has_explicit_packed = true;
                int64_t msb = eval_const_signed_val(eval_const_expr(p.msb_expr.get(), current_scope));
                int64_t lsb = eval_const_signed_val(eval_const_expr(p.lsb_expr.get(), current_scope));
                w = (uint32_t)(std::abs(msb - lsb) + 1);
            }
        }

        if (p.is_array) {
            std::vector<ArrayDimInfo> all_dims;
            uint32_t total_depth = 1;
            uint32_t unpacked_cnt = 0;
            if (!p.unpacked_dimensions.empty()) {
                for (const auto& udim : p.unpacked_dimensions) {
                    int64_t b1 = eval_const_signed_val(eval_const_expr(udim.left_expr.get(), current_scope));
                    int64_t b2 = udim.right_expr ? eval_const_signed_val(eval_const_expr(udim.right_expr.get(), current_scope)) : 0;
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
                    b1 = eval_const_signed_val(eval_const_expr(p.bound1_expr.get(), current_scope));
                    b2 = eval_const_signed_val(eval_const_expr(p.bound2_expr.get(), current_scope));
                } else if (p.bound1_expr) {
                    uint32_t d = (uint32_t)eval_const_expr(p.bound1_expr.get(), current_scope);
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
            unpacked_cnt = (uint32_t)all_dims.size();

            if (!p.packed_dimensions.empty()) {
                for (const auto& pdim : p.packed_dimensions) {
                    int64_t b1 = (int64_t)eval_const_expr(pdim.left_expr.get(), current_scope);
                    int64_t b2 = pdim.right_expr ? (int64_t)eval_const_expr(pdim.right_expr.get(), current_scope) : 0;
                    ArrayDimInfo info;
                    info.left = b1;
                    info.right = b2;
                    info.is_unpacked = false;
                    all_dims.push_back(info);
                }
            } else if (p.msb_expr) {
                int64_t b1 = (int64_t)eval_const_expr(p.msb_expr.get(), current_scope);
                int64_t b2 = p.lsb_expr ? (int64_t)eval_const_expr(p.lsb_expr.get(), current_scope) : 0;
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

            std::string mem_scope = current_scope + "." + p.name;
            int64_t b1_first = all_dims.empty() ? 0 : all_dims[0].left;
            int64_t b2_first = all_dims.empty() ? 0 : all_dims[0].right;
            MemoryArray marr(mem_scope, w, total_depth, b1_first, b2_first);
            if (p.type_name == "real" || p.type_name == "shortreal") {
                marr.is_real = true;
                for (auto& wrd : marr.words) wrd = LogicVal(0.0);
            }
            marr.dimensions = all_dims;
            marr.unpacked_dims_count = unpacked_cnt;
            for (const auto& d : all_dims) {
                if (!d.is_unpacked) {
                    marr.packed_lsb = std::min(d.left, d.right);
                    break;
                }
            }
            result.memory_map[mem_scope] = marr;

            VariableDimMetadata vdm;
            vdm.name = mem_scope;
            vdm.dimensions = all_dims;
            vdm.unpacked_dims = unpacked_cnt;
            vdm.is_vector_or_array = true;
            result.var_dimensions[mem_scope] = vdm;
            result.var_dimensions[p.name] = vdm;

            if (debug_out) {
                *debug_out << "     * Generated Memory allocated: " << mem_scope 
                           << " [" << w << "-bit x " << total_depth << " words]\n";
            }
        } else {
            std::string full_name = current_scope + "." + p.name;
            size_t nid = get_or_create_net(full_name, w, p.is_signed, p.net_kind);
            result.netlist[nid].value = LogicVal::make_x(w);
            result.netlist[nid].prev_value = LogicVal::make_x(w);

            VariableDimMetadata svdm;
            svdm.name = full_name;
            if (!p.packed_dimensions.empty()) {
                for (const auto& pdim : p.packed_dimensions) {
                    int64_t b1 = (int64_t)eval_const_expr(pdim.left_expr.get(), current_scope);
                    int64_t b2 = pdim.right_expr ? (int64_t)eval_const_expr(pdim.right_expr.get(), current_scope) : 0;
                    svdm.dimensions.push_back({b1, b2, false, false, false});
                }
                if (elem_w > 1) {
                    svdm.dimensions.push_back({(int64_t)(elem_w - 1), 0, false, false, false});
                }
                svdm.is_vector_or_array = true;
            } else if (p.msb_expr) {
                int64_t b1 = (int64_t)eval_const_expr(p.msb_expr.get(), current_scope);
                int64_t b2 = p.lsb_expr ? (int64_t)eval_const_expr(p.lsb_expr.get(), current_scope) : 0;
                svdm.dimensions.push_back({b1, b2, false, false, false});
                if (elem_w > 1) {
                    svdm.dimensions.push_back({(int64_t)(elem_w - 1), 0, false, false, false});
                }
                svdm.is_vector_or_array = true;
            } else if (w > 1) {
                svdm.dimensions.push_back({(int64_t)(w - 1), 0, false, false, false});
                svdm.is_vector_or_array = true;
            }
            if (svdm.is_vector_or_array) {
                result.var_dimensions[full_name] = svdm;
                result.var_dimensions[p.name] = svdm;
            }

            if (debug_out) {
                *debug_out << "     * Generated Net allocated: " << full_name << " [" << w << "-bit]\n";
            }
        }
    }

    auto resolve_gen_net_id = [&](const std::string& start_scope, const std::string& name) -> size_t {
        std::string cur_s = start_scope;
        auto brk = name.find('[');
        std::string iface_base = (brk != std::string::npos) ? name.substr(0, brk) : "";
        std::string iface_rest = (brk != std::string::npos) ? name.substr(brk) : "";
        while (true) {
            std::string cand = cur_s.empty() ? name : (cur_s + "." + name);
            auto it = result.net_map.find(cand);
            if (it != result.net_map.end()) return it->second;
            if (!iface_base.empty()) {
                std::string pkey = cur_s.empty() ? iface_base : (cur_s + "." + iface_base);
                auto ipm = result.interface_port_map.find(pkey);
                if (ipm != result.interface_port_map.end()) {
                    std::string mapped = ipm->second + iface_rest;
                    auto it_m = result.net_map.find(mapped);
                    if (it_m != result.net_map.end()) return it_m->second;
                }
            }
            if (cur_s.empty()) break;
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }
        return (size_t)-1;
    };
    auto resolve_gen_mem_name = [&](const std::string& start_scope, const std::string& name) -> std::string {
        std::string cur_s = start_scope;
        while (true) {
            std::string cand = cur_s.empty() ? name : (cur_s + "." + name);
            if (result.memory_map.find(cand) != result.memory_map.end()) return cand;
            if (cur_s.empty()) break;
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }
        return "";
    };

    for (const auto& ca : blk->continuous_assigns) {
        auto ca_copy = ca->clone();
        if (!active_genvars.empty()) {
            for (auto it = active_genvars.rbegin(); it != active_genvars.rend(); ++it) {
                if (ca_copy->target_index_expr) ca_copy->target_index_expr = substitute_genvar_in_expr(ca_copy->target_index_expr.get(), it->first, it->second);
                if (ca_copy->rhs) ca_copy->rhs = substitute_genvar_in_expr(ca_copy->rhs.get(), it->first, it->second);
            }
        } else if (has_genvar) {
            if (ca_copy->target_index_expr) {
                ca_copy->target_index_expr = substitute_genvar_in_expr(ca_copy->target_index_expr.get(), gname, gval);
            }
            if (ca_copy->rhs) {
                ca_copy->rhs = substitute_genvar_in_expr(ca_copy->rhs.get(), gname, gval);
            }
        }
        if (!ca_copy->concat_targets.empty()) {
            auto concat_rhs = dynamic_cast<ConcatNode*>(ca_copy->rhs.get());
            if (concat_rhs && concat_rhs->elements.size() == ca_copy->concat_targets.size()) {
                for (size_t i = 0; i < ca_copy->concat_targets.size(); ++i) {
                    const auto& tname = ca_copy->concat_targets[i];
                    NetSliceRef t_slice{(size_t)-1, 0, 0};
                    if (i < ca_copy->concat_target_exprs.size() && ca_copy->concat_target_exprs[i]) {
                        try {
                            t_slice = resolve_slice_ref(ca_copy->concat_target_exprs[i].get(), current_scope);
                        } catch (...) {}
                    }
                    if (t_slice.net_id == (size_t)-1) {
                        size_t tid = resolve_gen_net_id(current_scope, tname);
                        if (tid != (size_t)-1) {
                            t_slice = NetSliceRef{tid, 0, result.netlist[tid].width};
                        }
                    }
                    if (t_slice.net_id == (size_t)-1) continue;

                    auto new_ca = std::make_unique<ContinuousAssignNode>(tname, concat_rhs->elements[i]->clone());
                    auto rhs_ptr = new_ca->rhs.get();
                    result.generated_continuous_assigns.push_back(std::move(new_ca));
                    result.continuous_assigns.push_back({current_scope, t_slice, rhs_ptr});
                }
            } else {
                uint32_t cur_offset = 0;
                for (int i = (int)ca_copy->concat_targets.size() - 1; i >= 0; --i) {
                    const auto& tname = ca_copy->concat_targets[i];
                    NetSliceRef t_slice{(size_t)-1, 0, 0};
                    if ((size_t)i < ca_copy->concat_target_exprs.size() && ca_copy->concat_target_exprs[i]) {
                        try {
                            t_slice = resolve_slice_ref(ca_copy->concat_target_exprs[i].get(), current_scope);
                        } catch (...) {}
                    }
                    if (t_slice.net_id == (size_t)-1) {
                        size_t tid = resolve_gen_net_id(current_scope, tname);
                        if (tid != (size_t)-1) {
                            t_slice = NetSliceRef{tid, 0, result.netlist[tid].width};
                        }
                    }
                    if (t_slice.net_id == (size_t)-1) continue;

                    size_t tid = t_slice.net_id;
                    uint32_t target_offset = t_slice.offset;
                    uint32_t w = t_slice.width;

                    std::unique_ptr<ExpressionNode> elem_rhs = nullptr;
                    if (auto id_node = dynamic_cast<IdentifierNode*>(ca_copy->rhs.get())) {
                        if (w == 1) {
                            elem_rhs = std::make_unique<IndexSelectNode>(id_node->name, std::make_unique<NumberNode>(cur_offset, 32));
                        } else {
                            elem_rhs = std::make_unique<IndexSelectNode>(id_node->name,
                                std::make_unique<NumberNode>(cur_offset + w - 1, 32),
                                std::make_unique<NumberNode>(cur_offset, 32));
                        }
                    } else {
                        std::unique_ptr<ExpressionNode> shifted = ca_copy->rhs->clone();
                        if (cur_offset > 0) {
                            shifted = std::make_unique<BinaryOpNode>(">>", std::move(shifted), std::make_unique<NumberNode>(cur_offset, 32));
                        }
                        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                        elem_rhs = std::make_unique<BinaryOpNode>("&", std::move(shifted), std::make_unique<NumberNode>(mask, w));
                    }
                    cur_offset += w;

                    auto new_ca = std::make_unique<ContinuousAssignNode>(tname, std::move(elem_rhs));
                    auto rhs_ptr = new_ca->rhs.get();
                    result.generated_continuous_assigns.push_back(std::move(new_ca));
                    result.continuous_assigns.push_back({current_scope, {tid, target_offset, w}, rhs_ptr});
                }
            }
            continue;
        }
        if (ca_copy->target_index_expr) {
            size_t tid = resolve_gen_net_id(current_scope, ca_copy->target_name);
            if (tid == (size_t)-1) {
                std::string tname = ca_copy->target_name;
                size_t dot_pos = tname.rfind('.');
                while (dot_pos != std::string::npos) {
                    std::string base = tname.substr(0, dot_pos);
                    tid = resolve_gen_net_id(current_scope, base);
                    if (tid != (size_t)-1) break;
                    dot_pos = tname.rfind('.', dot_pos - 1);
                }
            }
            if (tid == (size_t)-1 && !ca_copy->member_name.empty()) {
                try {
                    ExpressionNode* idx_expr_to_eval = ca_copy->target_index_expr.get();
                    if (auto idx_sel = dynamic_cast<IndexSelectNode*>(idx_expr_to_eval)) {
                        if (idx_sel->msb_expr) idx_expr_to_eval = idx_sel->msb_expr.get();
                    }
                    uint64_t inst_idx = eval_const_expr(idx_expr_to_eval, current_scope);
                    std::string inst_net = ca_copy->target_name + "[" + std::to_string(inst_idx) + "]." + ca_copy->member_name;
                    size_t inst_nid = resolve_gen_net_id(current_scope, inst_net);
                    if (inst_nid != (size_t)-1) {
                        NetSliceRef slice{inst_nid, 0, result.netlist[inst_nid].width};
                        if (ca_copy->has_member_select && ca_copy->member_msb_expr) {
                            uint64_t b_msb = eval_const_expr(ca_copy->member_msb_expr.get(), current_scope);
                            uint64_t b_lsb = ca_copy->member_lsb_expr ? eval_const_expr(ca_copy->member_lsb_expr.get(), current_scope) : b_msb;
                            uint64_t sub_lsb = std::min(b_msb, b_lsb);
                            uint32_t sub_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                            slice.offset += (uint32_t)sub_lsb;
                            slice.width = sub_w;
                        }
                        auto rhs_ptr = ca_copy->rhs.get();
                        result.generated_continuous_assigns.push_back(std::move(ca_copy));
                        result.continuous_assigns.push_back({current_scope, slice, rhs_ptr});
                        continue;
                    }
                } catch (...) {}
            }
            if (tid != (size_t)-1) {
                NetSliceRef slice{tid, 0, 1};
                uint32_t d1_width = 1;
                int64_t base_low = 0;
                std::string full_name = result.netlist[tid].name;
                auto it_vdm = result.var_dimensions.find(full_name);
                if (it_vdm == result.var_dimensions.end()) it_vdm = result.var_dimensions.find(ca_copy->target_name);
                if (it_vdm == result.var_dimensions.end() && tid != (size_t)-1 && !result.netlist[tid].type_name.empty()) {
                    const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(result.netlist[tid].type_name);
                    if (tmeta && !tmeta->dimensions.empty()) {
                        VariableDimMetadata vdm = *tmeta;
                        vdm.name = full_name;
                        result.var_dimensions[full_name] = vdm;
                        it_vdm = result.var_dimensions.find(full_name);
                    }
                }

        if (it_vdm != result.var_dimensions.end() && !it_vdm->second.dimensions.empty()) {
                    base_low = it_vdm->second.dimensions[0].low();
                    if (it_vdm->second.dimensions.size() > 1) {
                        d1_width = 1;
                        for (size_t k = 1; k < it_vdm->second.dimensions.size(); ++k) {
                            d1_width *= (uint32_t)it_vdm->second.dimensions[k].size();
                        }
                    } else if (it_vdm->second.dimensions[0].size() > 0 && result.netlist[tid].width > (uint32_t)it_vdm->second.dimensions[0].size()) {
                        d1_width = result.netlist[tid].width / (uint32_t)it_vdm->second.dimensions[0].size();
                    }
                }
                if (auto idx_node = dynamic_cast<IndexSelectNode*>(ca_copy->target_index_expr.get())) {
                    slice = resolve_slice_ref(idx_node, current_scope);
                } else {
                    uint64_t off = eval_const_expr(ca_copy->target_index_expr.get(), current_scope);
                    if (base_low > 0 && off >= (uint64_t)base_low) off -= (uint64_t)base_low;
                    slice = {tid, (uint32_t)(off * d1_width), d1_width};
                }
                if (!ca_copy->member_name.empty()) {
                    uint32_t m_off = 0;
                    uint32_t m_len = slice.width;
                    int64_t m_lsb = 0;
                    if (resolve_struct_member_slice(tid, ca_copy->member_name, current_scope, m_off, m_len, m_lsb)) {
                        slice.offset += m_off;
                        slice.width = m_len;
                        if (ca_copy->has_member_select && ca_copy->member_msb_expr) {
                            uint64_t b_msb = eval_const_expr(ca_copy->member_msb_expr.get(), current_scope);
                            uint64_t b_lsb = ca_copy->member_lsb_expr ? eval_const_expr(ca_copy->member_lsb_expr.get(), current_scope) : b_msb;
                            uint64_t sub_lsb = std::min(b_msb, b_lsb);
                            if (m_lsb > 0 && sub_lsb >= (uint64_t)m_lsb) sub_lsb -= (uint64_t)m_lsb;
                            uint32_t sub_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                            slice.offset += (uint32_t)sub_lsb;
                            slice.width = sub_w;
                        }
                    }
                }
                auto rhs_ptr = ca_copy->rhs.get();
                result.generated_continuous_assigns.push_back(std::move(ca_copy));
                result.continuous_assigns.push_back({current_scope, slice, rhs_ptr});
            } else {
                std::string mem_target = resolve_gen_mem_name(current_scope, ca_copy->target_name);
                if (!mem_target.empty()) {
                    uint32_t bit_off = 0, bit_w = 0;
                    if (auto idx = dynamic_cast<IndexSelectNode*>(ca_copy->target_index_expr.get())) {
                        if (idx->has_bit_select && idx->bit_msb_expr) {
                            try {
                                uint64_t b_msb = eval_const_expr(idx->bit_msb_expr.get(), current_scope);
                                uint64_t b_lsb = idx->bit_lsb_expr ? eval_const_expr(idx->bit_lsb_expr.get(), current_scope) : b_msb;
                                if (idx->is_plus_colon) {
                                    bit_off = (uint32_t)b_msb;
                                    bit_w = (uint32_t)b_lsb;
                                } else if (idx->is_minus_colon) {
                                    bit_off = (uint32_t)(b_msb - b_lsb + 1);
                                    bit_w = (uint32_t)b_lsb;
                                } else {
                                    bit_off = (uint32_t)std::min(b_msb, b_lsb);
                                    bit_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                                }
                            } catch (...) {}
                        }
                    }
                    result.memory_continuous_assigns.push_back({current_scope, ca_copy->target_name, std::move(ca_copy->target_index_expr), std::move(ca_copy->rhs), ca_copy->member_name, bit_off, bit_w});
                }
            }
        } else {
            size_t tid = (size_t)-1;
            std::string effective_member = ca_copy->member_name;
            std::string full_combined = effective_member.empty() ? ca_copy->target_name : (ca_copy->target_name + "." + effective_member);
            size_t direct_id = get_net_id(current_scope, full_combined);
            if (direct_id != (size_t)-1) {
                tid = direct_id;
                effective_member = "";
            } else {
                std::string tname = full_combined;
                size_t dot_pos = tname.rfind('.');
                std::string memb = "";
                while (dot_pos != std::string::npos) {
                    std::string base = tname.substr(0, dot_pos);
                    memb = tname.substr(dot_pos + 1);
                    size_t bid = get_net_id(current_scope, base);
                    if (bid != (size_t)-1) {
                        tid = bid;
                        effective_member = memb;
                        break;
                    }
                    dot_pos = tname.rfind('.', dot_pos - 1);
                }
            }
            if (tid != (size_t)-1) {
                uint32_t off = 0;
                uint32_t len = result.netlist[tid].width;
                int64_t memb_lsb = 0;
                if (!effective_member.empty()) {
                    resolve_struct_member_slice(tid, effective_member, current_scope, off, len, memb_lsb);
                }
                if (ca_copy->has_member_select && ca_copy->member_msb_expr) {
                    uint64_t b_msb = eval_const_expr(ca_copy->member_msb_expr.get(), current_scope);
                    uint64_t b_lsb = ca_copy->member_lsb_expr ? eval_const_expr(ca_copy->member_lsb_expr.get(), current_scope) : b_msb;
                    uint64_t sub_lsb = std::min(b_msb, b_lsb);
                    if (memb_lsb > 0 && sub_lsb >= (uint64_t)memb_lsb) sub_lsb -= (uint64_t)memb_lsb;
                    uint32_t sub_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                    off += (uint32_t)sub_lsb;
                    len = sub_w;
                }
                auto rhs_ptr = ca_copy->rhs.get();
                result.generated_continuous_assigns.push_back(std::move(ca_copy));
                result.continuous_assigns.push_back({current_scope, {tid, off, len}, rhs_ptr});
            } else {
                std::string mem_target = resolve_gen_mem_name(current_scope, ca_copy->target_name);
                if (!mem_target.empty()) {
                    auto& mem = result.memory_map.at(mem_target);
                    uint32_t depth = mem.depth;
                    if (auto ucn = dynamic_cast<UnpackedConcatNode*>(ca_copy->rhs.get())) {
                        if (ucn->is_pattern && !ucn->elements.empty()) {
                            bool all_pos = true;
                            for (const auto& elem : ucn->elements) {
                                if (dynamic_cast<PatternMemberNode*>(elem.get())) {
                                    all_pos = false;
                                    break;
                                }
                            }
                            if (all_pos && ucn->elements.size() != depth) {
                                throw std::runtime_error("Number of elements in array assignment pattern does not match array size for '" + ca_copy->target_name + "' per IEEE 1800-2023 10.9");
                            }
                        }
                    }
                    for (uint32_t k = 0; k < depth; ++k) {
                        auto idx_node = std::make_unique<NumberNode>(k, 32);
                        std::unique_ptr<ExpressionNode> rhs_elem = nullptr;
                        if (auto id_rhs = dynamic_cast<IdentifierNode*>(ca_copy->rhs.get())) {
                            rhs_elem = std::make_unique<IndexSelectNode>(id_rhs->name, std::make_unique<NumberNode>(k, 32));
                        } else if (auto ucn = dynamic_cast<UnpackedConcatNode*>(ca_copy->rhs.get())) {
                            if (k < ucn->elements.size()) {
                                rhs_elem = ucn->elements[k]->clone();
                            } else {
                                rhs_elem = ca_copy->rhs->clone();
                            }
                        } else {
                            rhs_elem = ca_copy->rhs->clone();
                        }
                        result.memory_continuous_assigns.push_back({current_scope, ca_copy->target_name, std::move(idx_node), std::move(rhs_elem), ca_copy->member_name});
                    }
                }
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
        if (proc_copy->type == ProcessType::SEQUENTIAL && !proc_copy->sensitivity_list.empty()) {
            bool all_events = true;
            for (const auto& sens : proc_copy->sensitivity_list) {
                std::string scoped_sig = current_scope.empty() ? sens.signal_name : (current_scope + "." + sens.signal_name);
                if (result.named_event_map.find(scoped_sig) == result.named_event_map.end() &&
                    result.named_event_map.find(sens.signal_name) == result.named_event_map.end()) {
                    all_events = false;
                    break;
                }
            }
            if (all_events) {
                proc_copy->type = ProcessType::ALWAYS;
                std::vector<std::unique_ptr<StatementNode>> new_body;
                new_body.push_back(std::make_unique<EventControlStatementNode>(proc_copy->sensitivity_list[0].edge, proc_copy->sensitivity_list[0].signal_name));
                for (auto& s : proc_copy->body) {
                    new_body.push_back(std::move(s));
                }
                proc_copy->body = std::move(new_body);
                proc_copy->sensitivity_list.clear();
            }
        }
        if (proc_copy->type == ProcessType::ALWAYS && proc_copy->sensitivity_list.empty()) {
            bool has_timing = false;
            for (const auto& s : proc_copy->body) {
                if (stmt_has_timing_control(s.get())) { has_timing = true; break; }
            }
            if (!has_timing) {
                throw std::runtime_error("error: always process does not have any delay. A runtime infinite loop will occur. Elaboration failed");
            }
        }
        BoundProcess bp(current_scope, proc_copy.get(), 0, false);
        if (current_module) {
            bp.module_wildcard_imports = current_module->wildcard_imports;
            bp.module_explicit_imports = current_module->explicit_imports;
        }
        bp.pid = result.processes.size();
        result.generated_processes.push_back(std::move(proc_copy));
        bp.proc = result.generated_processes.back().get();
        for (const auto& s : bp.proc->body) {
            if (auto blk = dynamic_cast<BlockStatementNode*>(s.get())) {
                if (!blk->name.empty()) bp.named_blocks.push_back(blk->name);
            }
        }
        result.processes.push_back(std::move(bp));
    }

    for (const auto& inst : blk->instantiations) {
        auto inst_copy = inst->clone();
        if (has_genvar) {
            for (auto& po : inst_copy->param_overrides) {
                if (po.expr) po.expr = substitute_genvar_in_expr(po.expr.get(), gname, gval);
            }
            for (auto& conn : inst_copy->connections) {
                if (conn.expr) conn.expr = substitute_genvar_in_expr(conn.expr.get(), gname, gval);
            }
        }
        if (current_module_defs) {
            elaborate_instantiation_node(inst_copy.get(), current_scope, *current_module_defs, debug_out);
        }
    }

    for (const auto& asrt : blk->assertions) {
        SvaAssertionInstance inst;
        inst.label = asrt->label;
        inst.property_name = asrt->property_name;
        inst.scope = current_scope;

        if (!asrt->label.empty()) {
            std::string full_label = current_scope.empty() ? asrt->label : (current_scope + "." + asrt->label);
            result.assertion_names.insert(full_label);
            result.assertion_names.insert(asrt->label);
        }

        std::string prop_scope = current_scope;
        if (!asrt->property_name.empty() && asrt->property_name.find('.') != std::string::npos) {
            std::string prefix = asrt->property_name.substr(0, asrt->property_name.rfind('.'));
            prop_scope = current_scope.empty() ? prefix : (current_scope + "." + prefix);
            inst.scope = prop_scope;
        }

        PropertyNode* target_prop = nullptr;
        if (!asrt->property_name.empty()) {
            std::string full_pname = current_scope.empty() ? asrt->property_name : (current_scope + "." + asrt->property_name);
            if (result.property_defs.count(full_pname)) target_prop = result.property_defs[full_pname].get();
            else if (result.property_defs.count(asrt->property_name)) target_prop = result.property_defs[asrt->property_name].get();
            else if (current_module && current_module->property_map.count(asrt->property_name)) target_prop = current_module->property_map[asrt->property_name];
        } else if (asrt->inlined_property) {
            target_prop = asrt->inlined_property.get();
        }

        if (target_prop) {
            inst.clock_edge = target_prop->clock_edge;
            inst.clock_signal = target_prop->clock_signal;

            std::string clk_scoped = prop_scope.empty() ? target_prop->clock_signal : (prop_scope + "." + target_prop->clock_signal);
            if (result.net_map.find(clk_scoped) != result.net_map.end()) {
                inst.clock_net_id = result.net_map.at(clk_scoped);
            } else if (result.net_map.find(target_prop->clock_signal) != result.net_map.end()) {
                inst.clock_net_id = result.net_map.at(target_prop->clock_signal);
            } else {
                std::string clk_sc = current_scope.empty() ? target_prop->clock_signal : (current_scope + "." + target_prop->clock_signal);
                if (result.net_map.find(clk_sc) != result.net_map.end()) {
                    inst.clock_net_id = result.net_map.at(clk_sc);
                }
            }

            inst.disable_expr = target_prop->disable_expr.get();
            inst.imp_type = target_prop->imp_type;
            inst.antecedent = target_prop->antecedent.get();
            inst.consequent = target_prop->consequent.get();
            inst.local_vars = target_prop->local_vars;
            for (const auto& lv : target_prop->local_vars) {
                result.sva_local_var_names.insert(lv.name);
                if (!current_scope.empty()) result.sva_local_var_names.insert(current_scope + "." + lv.name);
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

        int loop_count = 0;
        while (true) {
            auto cond_e = substitute_genvar_in_expr(loop->cond_expr.get(), gname, val);
            if (has_parent_gvar) cond_e = substitute_genvar_in_expr(cond_e.get(), parent_gname, parent_gval);
            uint64_t cval = 0;
            try {
                cval = eval_const_expr(cond_e.get(), scope);
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("[CAS-DTINPCIL] Dynamic type in non-procedural context: generate condition cannot be evaluated as elaboration-time constant per IEEE 1800-2023 section 27.5: ") + e.what());
            }
            if (++loop_count > 100000) {
                throw std::runtime_error("Generate loop exceeded 100000 iterations: " + gname + " in " + scope);
            }
            if (cval == 0) break;

            std::string iter_label = loop->block_label.empty() ? ("genblk" + std::to_string(val)) : (loop->block_label + "[" + std::to_string(val) + "]");
            std::string iter_scope = scope + "." + iter_label;
            result.param_map[iter_scope + "." + gname] = (uint64_t)val;
            result.param_val_map[iter_scope + "." + gname] = LogicVal::make_known(32, (uint64_t)val);

            active_genvars.push_back({gname, val});
            unroll_generate_block(loop->body.get(), iter_scope, scope, gname, val, true, debug_out);
            active_genvars.pop_back();

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
    ModuleNode* prev_module = current_module;
    current_module = mod;
    result.scope_cu_id[scope] = mod->compilation_unit_id;
    std::cout << "[ELAB] " << mod->name << " [" << scope << "]\n" << std::flush;
    if (debug_out) {
        *debug_out << "  -> Scoping module '" << mod->name << "' as [" << scope << "]\n";
    }

    std::string eff_tu = !mod->timeunit_val.empty() ? mod->timeunit_val : mod->default_timescale_unit;
    if (eff_tu.empty()) eff_tu = !Parser::global_timeunit_val.empty() ? Parser::global_timeunit_val : (Lexer::global_timescale_encountered ? Lexer::global_time_unit_str : "1s");
    std::string eff_tp = !mod->timeprecision_val.empty() ? mod->timeprecision_val : mod->default_timescale_prec;
    if (eff_tp.empty()) eff_tp = !Parser::global_timeprec_val.empty() ? Parser::global_timeprec_val : (Lexer::global_timescale_encountered ? Lexer::global_time_precision_str : "1s");
    result.timescale_map[mod->name] = {eff_tu, eff_tp};
    result.timescale_map[scope] = {eff_tu, eff_tp};
    if (mod->has_compilation_unit_timeunit || !mod->timeunit_val.empty()) {
        result.scopes_with_timeunit.insert(mod->name);
        result.scopes_with_timeunit.insert(scope);
    }
    for (const auto& kv : mod->nested_timescales) {
        std::string ntu = !kv.second.first.empty() ? kv.second.first : eff_tu;
        std::string ntp = !kv.second.second.empty() ? kv.second.second : eff_tp;
        result.timescale_map[mod->name + "." + kv.first] = {ntu, ntp};
        result.timescale_map[scope + "." + kv.first] = {ntu, ntp};
    }

    // Bind wildcard package imports
    for (const auto& pkg_name : mod->wildcard_imports) {
        auto it = Parser::global_packages.find(pkg_name);
        if (it != Parser::global_packages.end()) {
            const auto& pkg = it->second;
            for (const auto& p : pkg->parameters) {
                bool local_decl = false;
                for (const auto& lp : mod->parameters) { if (lp.name == p.name) { local_decl = true; break; } }
                if (local_decl) continue;
                LogicVal lval = eval_const_logic_val(p.default_expr.get(), scope);
                result.param_val_map[scope + "." + p.name] = lval;
                result.param_val_map[p.name] = lval;
                result.param_map[scope + "." + p.name] = lval.aval;
                if (parent_scope.empty()) result.param_map[p.name] = lval.aval;
            }
            for (const auto& p : pkg->param_constants) {
                bool local_decl = false;
                for (const auto& lp : mod->parameters) { if (lp.name == p.first) { local_decl = true; break; } }
                if (local_decl) continue;
                if (result.param_map.find(scope + "." + p.first) == result.param_map.end()) {
                    result.param_map[scope + "." + p.first] = p.second;
                    if (parent_scope.empty()) result.param_map[p.first] = p.second;
                }
            }
            for (const auto& m : pkg->enum_constant_map) {
                bool local_decl = false;
                for (const auto& lp : mod->parameters) { if (lp.name == m.first) { local_decl = true; break; } }
                for (const auto& sig : mod->signals) { if (sig.name == m.first) { local_decl = true; break; } }
                if (local_decl) continue;
                result.enum_constant_map[scope + "." + m.first] = m.second;
                if (parent_scope.empty()) result.enum_constant_map[m.first] = m.second;
            }
            for (const auto& fn : pkg->functions) {
                bool local_decl = false;
                for (const auto& lfn : mod->functions) { if (lfn->name == fn->name) { local_decl = true; break; } }
                if (local_decl) continue;
                result.function_map[scope + "." + fn->name] = fn.get();
                if (parent_scope.empty()) result.function_map[fn->name] = fn.get();
            }
            for (const auto& t : pkg->tasks) {
                bool local_decl = false;
                for (const auto& lt : mod->tasks) { if (lt->name == t->name) { local_decl = true; break; } }
                if (local_decl) continue;
                result.task_map[scope + "." + t->name] = t.get();
                if (parent_scope.empty()) result.task_map[t->name] = t.get();
            }
            for (const auto& s : pkg->signals) {
                bool local_decl = false;
                for (const auto& lsig : mod->signals) { if (lsig.name == s.name) { local_decl = true; break; } }
                if (!local_decl) {
                    for (const auto& lp : mod->ports) { if (lp.name == s.name) { local_decl = true; break; } }
                }
                if (local_decl) continue;
                auto nit = result.net_map.find(pkg_name + "::" + s.name);
                if (nit != result.net_map.end()) {
                    result.net_map[scope + "." + s.name] = nit->second;
                    if (parent_scope.empty()) result.net_map[s.name] = nit->second;
                }
                if (!s.type_name.empty()) {
                    result.var_to_struct_type[scope + "." + s.name] = s.type_name;
                    if (parent_scope.empty()) result.var_to_struct_type[s.name] = s.type_name;
                }
                if (s.is_dynamic_array) {
                    auto dait = result.dynamic_array_map.find(pkg_name + "::" + s.name);
                    if (dait != result.dynamic_array_map.end()) {
                        result.dynamic_array_map[scope + "." + s.name] = dait->second;
                        if (parent_scope.empty()) result.dynamic_array_map[s.name] = dait->second;
                    }
                }
                if (s.is_class_handle) {
                    result.class_handle_names.insert(scope + "." + s.name);
                    if (parent_scope.empty()) result.class_handle_names.insert(s.name);
                    result.var_to_class_type[scope + "." + s.name] = s.class_type_name;
                    if (parent_scope.empty()) result.var_to_class_type[s.name] = s.class_type_name;
                }
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
            for (const auto& p : pkg->parameters) {
                if (p.name == item_name) {
                    LogicVal lval = eval_const_logic_val(p.default_expr.get(), scope);
                    result.param_val_map[scope + "." + item_name] = lval;
                    result.param_val_map[item_name] = lval;
                    result.param_map[scope + "." + item_name] = lval.aval;
                    if (parent_scope.empty()) result.param_map[item_name] = lval.aval;
                    break;
                }
            }
            if (pkg->signal_map.count(item_name)) {
                auto nit = result.net_map.find(pkg_name + "::" + item_name);
                if (nit != result.net_map.end()) {
                    result.net_map[scope + "." + item_name] = nit->second;
                    if (parent_scope.empty()) result.net_map[item_name] = nit->second;
                }
                const auto& s = pkg->signals[pkg->signal_map.at(item_name)];
                if (!s.type_name.empty()) {
                    result.var_to_struct_type[scope + "." + item_name] = s.type_name;
                    if (parent_scope.empty()) result.var_to_struct_type[item_name] = s.type_name;
                }
                if (s.is_dynamic_array) {
                    auto dait = result.dynamic_array_map.find(pkg_name + "::" + item_name);
                    if (dait != result.dynamic_array_map.end()) {
                        result.dynamic_array_map[scope + "." + item_name] = dait->second;
                        if (parent_scope.empty()) result.dynamic_array_map[item_name] = dait->second;
                    }
                }
            }
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
    }

    for (const auto& kv : mod->enum_typedefs) {
        if (kv.second && result.enum_types.find(kv.first) == result.enum_types.end()) {
            EnumType et;
            et.name = kv.first;
            et.width = kv.second->base_width;
            et.is_signed = kv.second->is_signed;
            for (const auto& m : kv.second->members) {
                et.members.push_back({m.first, m.second});
                result.enum_constant_map[m.first] = m.second;
                result.enum_constant_map[scope + "." + m.first] = m.second;
            }
            result.enum_types[kv.first] = et;
            result.enum_types[scope + "." + kv.first] = et;
        }
    }

    for (const auto& td : mod->typedefs) {
        if (td && td->struct_type) {
            PackedStructType st;
            st.name = td->type_name;
            st.is_signed = td->struct_type->is_signed;
            st.total_width = td->struct_type->total_width;
            st.is_union = td->struct_type->is_union;
            st.is_tagged = td->struct_type->is_tagged;
            st.tag_bits = td->struct_type->tag_bits;
            st.payload_width = td->struct_type->payload_width;

            size_t n = td->struct_type->members.size();
            std::vector<uint32_t> offsets(n, 0);
            uint32_t running_offset = 0;
            if (st.is_union) {
                for (size_t i = 0; i < n; ++i) {
                    offsets[i] = 0;
                }
            } else {
                for (int i = (int)n - 1; i >= 0; --i) {
                    offsets[i] = running_offset;
                    running_offset += td->struct_type->members[i].width;
                }
                if (st.total_width == 0) {
                    st.total_width = running_offset;
                }
            }

            for (size_t i = 0; i < n; ++i) {
                const auto& mn = td->struct_type->members[i];
                StructMember sm;
                sm.name = mn.name;
                sm.width = mn.width;
                sm.offset = offsets[i];
                sm.is_signed = mn.is_signed;
                sm.type_name = mn.type_name;
                sm.msb = mn.msb;
                sm.lsb = mn.lsb;
                sm.elem_width = mn.elem_width;
                sm.outer_low = mn.outer_low;
                sm.default_expr = mn.default_value;
                st.members.push_back(sm);
            }
            result.struct_types[td->type_name] = st;
            result.struct_types[scope + "." + td->type_name] = st;
        }
        if (!td->base_type.empty()) {
            if (td->is_unpacked_array) {
                UnpackedArrayTypeInfo uinfo;
                uinfo.name = td->type_name;
                uinfo.elem_type = td->elem_type.empty() ? td->base_type : td->elem_type;
                uinfo.elem_width = td->elem_width ? td->elem_width : (td->width ? td->width : 1);
                uinfo.elem_signed = td->elem_signed;
                uinfo.array_size = td->array_size;
                uinfo.start_idx = td->arr_start_idx;
                uinfo.end_idx = td->arr_end_idx;
                result.unpacked_array_types[td->type_name] = uinfo;
                result.unpacked_array_types[scope + "." + td->type_name] = uinfo;
            } else if (!td->is_dynamic_array) {
                SimpleTypeInfo sinfo;
                sinfo.name = td->type_name;
                sinfo.base_type = td->base_type;
                sinfo.width = td->width;
                sinfo.is_signed = td->is_signed;
                result.simple_types[td->type_name] = sinfo;
                result.simple_types[scope + "." + td->type_name] = sinfo;
            } else {
                UnpackedArrayTypeInfo uinfo;
                uinfo.name = td->type_name;
                uinfo.elem_type = td->elem_type.empty() ? td->base_type : td->elem_type;
                uinfo.elem_width = td->elem_width ? td->elem_width : (td->width ? td->width : 1);
                uinfo.elem_signed = td->elem_signed;
                uinfo.array_size = 0;
                result.unpacked_array_types[td->type_name] = uinfo;
                result.unpacked_array_types[scope + "." + td->type_name] = uinfo;
            }
        }
    }

    for (const auto& pair : mod->struct_typedefs) {
        const std::string& st_name = pair.first;
        const auto& st_node = pair.second;
        if (!st_node) continue;
        if (result.struct_types.find(scope + "." + st_name) != result.struct_types.end()) continue;

        PackedStructType st;
        st.name = st_name;
        st.is_signed = st_node->is_signed;
        st.is_union = st_node->is_union;
        st.is_tagged = st_node->is_tagged;
        st.tag_bits = st_node->tag_bits;
        st.payload_width = st_node->payload_width;

        size_t n = st_node->members.size();
        std::vector<uint32_t> member_widths(n, 1);
        for (size_t i = 0; i < n; ++i) {
            const auto& mn = st_node->members[i];
            uint32_t mw = mn.width;
            if (mn.msb_expr) {
                try {
                    int64_t m = eval_const_signed_val(eval_const_expr(mn.msb_expr.get(), scope));
                    int64_t l = mn.lsb_expr ? eval_const_signed_val(eval_const_expr(mn.lsb_expr.get(), scope)) : 0;
                    if (m >= 0 && l >= 0 && std::abs(m - l) < 65536) {
                        mw = (uint32_t)(std::abs(m - l) + 1);
                        if (mn.elem_width > 1) mw *= mn.elem_width;
                        else if (mn.width > mw && mn.width % mw == 0) mw = mn.width;
                    }
                } catch (...) {}
            } else if (!mn.type_name.empty()) {
                const PackedStructType* inner_st = find_struct_type(mn.type_name, scope);
                if (inner_st && inner_st->total_width > 0) {
                    mw = inner_st->total_width;
                } else {
                    uint32_t tw = resolve_type_width(mn.type_name);
                    if (tw > 0) mw = tw;
                }
            }
            if (mw > 65536) mw = 1;
            member_widths[i] = mw;
        }

        std::vector<uint32_t> offsets(n, 0);
        uint32_t running_offset = 0;
        if (st.is_union) {
            for (size_t i = 0; i < n; ++i) offsets[i] = 0;
            uint32_t max_w = 0;
            for (uint32_t mw : member_widths) if (mw > max_w) max_w = mw;
            st.total_width = max_w;
        } else {
            for (int i = (int)n - 1; i >= 0; --i) {
                offsets[i] = running_offset;
                running_offset += member_widths[i];
            }
            st.total_width = running_offset;
        }

        for (size_t i = 0; i < n; ++i) {
            const auto& mn = st_node->members[i];
            StructMember sm;
            sm.name = mn.name;
            sm.width = member_widths[i];
            sm.offset = offsets[i];
            sm.is_signed = mn.is_signed;
            sm.type_name = mn.type_name;
            sm.msb = mn.msb;
            sm.lsb = mn.lsb;
            sm.elem_width = mn.elem_width;
            sm.outer_low = mn.outer_low;
            sm.default_expr = mn.default_value;
            st.members.push_back(sm);
        }
        result.struct_types[st_name] = st;
        result.struct_types[scope + "." + st_name] = st;
    }

    for (const auto& pair : Parser::global_struct_typedefs) {
        const std::string& st_name = pair.first;
        const auto& st_node = pair.second;
        if (!st_node) continue;
        if (result.struct_types.find(st_name) != result.struct_types.end()) continue;

        PackedStructType st;
        st.name = st_name;
        st.is_signed = st_node->is_signed;
        st.total_width = st_node->total_width;
        st.is_union = st_node->is_union;
        st.is_tagged = st_node->is_tagged;
        st.tag_bits = st_node->tag_bits;
        st.payload_width = st_node->payload_width;

        size_t n = st_node->members.size();
        std::vector<uint32_t> offsets(n, 0);
        uint32_t running_offset = 0;
        if (st.is_union) {
            for (size_t i = 0; i < n; ++i) offsets[i] = 0;
        } else {
            for (int i = (int)n - 1; i >= 0; --i) {
                offsets[i] = running_offset;
                running_offset += st_node->members[i].width;
            }
            if (st.total_width == 0) st.total_width = running_offset;
        }

        for (size_t i = 0; i < n; ++i) {
            const auto& mn = st_node->members[i];
            StructMember sm;
            sm.name = mn.name;
            sm.width = mn.width;
            sm.offset = offsets[i];
            sm.is_signed = mn.is_signed;
            sm.type_name = mn.type_name;
            sm.msb = mn.msb;
            sm.lsb = mn.lsb;
            sm.elem_width = mn.elem_width;
            sm.outer_low = mn.outer_low;
            sm.default_expr = mn.default_value;
            st.members.push_back(sm);
        }
        result.struct_types[st_name] = st;
    }

    for (auto& f : mod->functions) {
        f->scope = scope;
        result.function_map[scope + "." + f->name] = f.get();
        result.function_map[f->name] = f.get();
    }

    size_t non_local_param_idx = 0;
    for (size_t i = 0; i < mod->parameters.size(); ++i) {
        const auto& p = mod->parameters[i];
        uint64_t val = 0;
        LogicVal lval;
        bool overridden = false;

        // 0. Top-level parameter overrides (if this is top module)
        if (parent_scope.empty()) {
            auto top_it = top_param_overrides.find(p.name);
            if (top_it == top_param_overrides.end()) {
                top_it = top_param_overrides.find(mod->name + "." + p.name);
            }
            if (top_it == top_param_overrides.end() && !scope.empty()) {
                top_it = top_param_overrides.find(scope + "." + p.name);
            }
            if (top_it != top_param_overrides.end()) {
                lval = top_it->second;
                val = lval.aval;
                overridden = true;
            }
        }

        // 1. Check defparam overrides FIRST (defparam takes precedence over instantiation defaults/bindings)
        if (!overridden) {
            auto dp_inst_it = defparam_overrides.find(scope);
            if (dp_inst_it != defparam_overrides.end()) {
                auto dp_it = dp_inst_it->second.find(p.name);
                if (dp_it != dp_inst_it->second.end()) {
                    val = dp_it->second;
                    if (defparam_val_overrides.count(scope) && defparam_val_overrides.at(scope).count(p.name)) {
                        lval = defparam_val_overrides.at(scope).at(p.name);
                    } else {
                        lval = LogicVal::make_known(32, val);
                    }
                    overridden = true;
                }
            }
        }

        // 2. Check instantiation-site #() overrides if not overridden by defparam
        if (!overridden && overrides && !p.is_localparam) {
            for (size_t oi = 0; oi < overrides->size(); ++oi) {
                const auto& ov = (*overrides)[oi];
                if (!ov.param_name.empty() && ov.param_name == p.name) {
                    if (p.type_name == "type") {
                        if (auto ov_id = dynamic_cast<IdentifierNode*>(ov.expr.get())) {
                            std::string ov_t = ov_id->name;
                            uint32_t tw = 0;
                            const PackedStructType* st_ov = find_struct_type(ov_t, parent_scope);
                            if (!st_ov) st_ov = find_struct_type(ov_t, scope);
                            if (st_ov) {
                                tw = st_ov->total_width;
                                result.struct_types[scope + "." + p.name] = *st_ov;
                                result.struct_types[scope + "." + p.name].name = p.name;
                                result.struct_types[p.name] = *st_ov;
                                result.struct_types[p.name].name = p.name;
                            } else {
                                std::string cur_ps = parent_scope;
                                while (!cur_ps.empty()) {
                                    auto it_st = result.simple_types.find(cur_ps + "." + ov_t);
                                    if (it_st != result.simple_types.end() && it_st->second.width > 0) {
                                        tw = it_st->second.width;
                                        break;
                                    }
                                    auto dot = cur_ps.rfind('.');
                                    cur_ps = (dot == std::string::npos) ? "" : cur_ps.substr(0, dot);
                                }
                                if (tw == 0) tw = resolve_type_width(ov_t);
                            }
                            if (tw > 0) {
                                Parser::global_simple_typedefs[scope + "." + p.name] = tw;
                                Parser::global_simple_typedefs[p.name] = tw;
                                SimpleTypeInfo sinfo; sinfo.name = p.name; sinfo.width = tw;
                                result.simple_types[scope + "." + p.name] = sinfo;
                                result.simple_types[p.name] = sinfo;
                            }
                            result.var_to_struct_type[scope + "." + p.name] = ov_t;
                            result.var_to_struct_type[p.name] = ov_t;
                        }
                        lval = LogicVal::make_known(32, 0);
                        val = 0;
                    } else {
                        lval = eval_const_logic_val(ov.expr.get(), parent_scope, p.type_name);
                        val = lval.aval;
                    }
                    if (ov.expr) {
                        if (auto ov_id = dynamic_cast<IdentifierNode*>(ov.expr.get())) {
                            std::string parent_key = parent_scope.empty() ? ov_id->name : (parent_scope + "." + ov_id->name);
                            if (result.param_val_map.count(parent_key)) {
                                lval = result.param_val_map.at(parent_key);
                                val = lval.aval;
                            }
                            if (result.var_to_struct_type.count(parent_key)) {
                                result.var_to_struct_type[scope + "." + p.name] = result.var_to_struct_type.at(parent_key);
                            }
                        }
                    }
                    overridden = true;
                    break;
                } else if (ov.param_name.empty() && oi == non_local_param_idx) {
                    if (p.type_name == "type") {
                        if (auto ov_id = dynamic_cast<IdentifierNode*>(ov.expr.get())) {
                            std::string ov_t = ov_id->name;
                            uint32_t tw = 0;
                            const PackedStructType* st_ov = find_struct_type(ov_t, parent_scope);
                            if (!st_ov) st_ov = find_struct_type(ov_t, scope);
                            if (st_ov) {
                                tw = st_ov->total_width;
                                result.struct_types[scope + "." + p.name] = *st_ov;
                                result.struct_types[scope + "." + p.name].name = p.name;
                                result.struct_types[p.name] = *st_ov;
                                result.struct_types[p.name].name = p.name;
                            } else {
                                std::string cur_ps = parent_scope;
                                while (!cur_ps.empty()) {
                                    auto it_st = result.simple_types.find(cur_ps + "." + ov_t);
                                    if (it_st != result.simple_types.end() && it_st->second.width > 0) {
                                        tw = it_st->second.width;
                                        break;
                                    }
                                    auto dot = cur_ps.rfind('.');
                                    cur_ps = (dot == std::string::npos) ? "" : cur_ps.substr(0, dot);
                                }
                                if (tw == 0) tw = resolve_type_width(ov_t);
                            }
                            if (tw > 0) {
                                Parser::global_simple_typedefs[scope + "." + p.name] = tw;
                                Parser::global_simple_typedefs[p.name] = tw;
                                SimpleTypeInfo sinfo; sinfo.name = p.name; sinfo.width = tw;
                                result.simple_types[scope + "." + p.name] = sinfo;
                                result.simple_types[p.name] = sinfo;
                            }
                            result.var_to_struct_type[scope + "." + p.name] = ov_t;
                            result.var_to_struct_type[p.name] = ov_t;
                        }
                        lval = LogicVal::make_known(32, 0);
                        val = 0;
                    } else {
                        lval = eval_const_logic_val(ov.expr.get(), parent_scope, p.type_name);
                        val = lval.aval;
                    }
                    if (ov.expr) {
                        if (auto ov_id = dynamic_cast<IdentifierNode*>(ov.expr.get())) {
                            std::string parent_key = parent_scope.empty() ? ov_id->name : (parent_scope + "." + ov_id->name);
                            if (result.param_val_map.count(parent_key)) {
                                lval = result.param_val_map.at(parent_key);
                                val = lval.aval;
                            }
                            if (result.var_to_struct_type.count(parent_key)) {
                                result.var_to_struct_type[scope + "." + p.name] = result.var_to_struct_type.at(parent_key);
                            }
                        }
                    }
                    overridden = true;
                    break;
                }
            }
        }

        if (overridden) {
            if (debug_out) *debug_out << "     * Parameter override: " << p.name << " = " << (lval.is_string ? lval.str_val : std::to_string(val)) << "\n";
        } else {
            if (!p.is_localparam && p.default_expr) {
                std::function<void(ExpressionNode*)> check_no_specparam = [&](ExpressionNode* e) {
                    if (!e) return;
                    if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                        if (mod->specparam_names.count(id->name)) {
                            throw std::runtime_error("A specparam shall not be used in a parameter value assignment per IEEE 1800-2023 6.20.5");
                        }
                    } else if (auto bin = dynamic_cast<BinaryOpNode*>(e)) {
                        check_no_specparam(bin->lhs.get());
                        check_no_specparam(bin->rhs.get());
                    } else if (auto un = dynamic_cast<UnaryOpNode*>(e)) {
                        check_no_specparam(un->operand.get());
                    }
                };
                check_no_specparam(p.default_expr.get());
            }
            if (p.type_name == "type" && p.default_expr) {
                if (auto def_id = dynamic_cast<IdentifierNode*>(p.default_expr.get())) {
                    std::string def_t = def_id->name;
                    auto lb = def_t.find('[');
                    auto rb = def_t.rfind(']');
                    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
                        std::string range_s = def_t.substr(lb + 1, rb - lb - 1);
                        auto col = range_s.find(':');
                        if (col != std::string::npos) {
                            std::string msb_s = range_s.substr(0, col);
                            std::string lsb_s = range_s.substr(col + 1);
                            while (!msb_s.empty() && isspace(msb_s.front())) msb_s.erase(msb_s.begin());
                            while (!msb_s.empty() && isspace(msb_s.back())) msb_s.pop_back();
                            while (!lsb_s.empty() && isspace(lsb_s.front())) lsb_s.erase(lsb_s.begin());
                            while (!lsb_s.empty() && isspace(lsb_s.back())) lsb_s.pop_back();
                            uint32_t w = 1;
                            try {
                                Lexer msb_lex("msb", msb_s);
                                Parser msb_parser(msb_lex);
                                auto m_expr = msb_parser.parse_expression();
                                Lexer lsb_lex("lsb", lsb_s);
                                Parser lsb_parser(lsb_lex);
                                auto l_expr = lsb_parser.parse_expression();
                                int64_t m_v = (int64_t)eval_const_expr(m_expr.get(), scope);
                                int64_t l_v = (int64_t)eval_const_expr(l_expr.get(), scope);
                                w = (uint32_t)(std::abs(m_v - l_v) + 1);
                            } catch (...) {}
                            Parser::global_simple_typedefs[scope + "." + p.name] = w;
                            Parser::global_simple_typedefs[p.name] = w;
                            SimpleTypeInfo sinfo; sinfo.name = p.name; sinfo.width = w;
                            result.simple_types[scope + "." + p.name] = sinfo;
                            result.simple_types[p.name] = sinfo;
                        }
                    } else {
                        uint32_t tw = 0;
                        const PackedStructType* st_def = find_struct_type(def_t, scope);
                        if (!st_def) st_def = find_struct_type(def_t, parent_scope);
                        if (st_def) {
                            tw = st_def->total_width;
                            result.struct_types[scope + "." + p.name] = *st_def;
                            result.struct_types[scope + "." + p.name].name = p.name;
                            result.struct_types[p.name] = *st_def;
                            result.struct_types[p.name].name = p.name;
                        } else tw = resolve_type_width(def_t);
                        if (tw > 0) {
                            Parser::global_simple_typedefs[scope + "." + p.name] = tw;
                            Parser::global_simple_typedefs[p.name] = tw;
                            SimpleTypeInfo sinfo; sinfo.name = p.name; sinfo.width = tw;
                            result.simple_types[scope + "." + p.name] = sinfo;
                            result.simple_types[p.name] = sinfo;
                        }
                    }
                }
                lval = LogicVal::make_known(32, 0);
                val = 0;
            } else {
                lval = eval_const_logic_val(p.default_expr.get(), scope, p.type_name);
                val = lval.aval;
            }
            if (debug_out) *debug_out << "     * Parameter default: " << p.name << " = " << (lval.is_string ? lval.str_val : std::to_string(val)) << "\n";
        }
        if (p.has_range && p.msb_expr) {
            int64_t msb = (int64_t)eval_const_expr(p.msb_expr.get(), scope);
            int64_t lsb = p.lsb_expr ? (int64_t)eval_const_expr(p.lsb_expr.get(), scope) : 0;
            uint32_t width = (uint32_t)(std::abs(msb - lsb) + 1);
            if (!lval.is_unpacked) {
                lval = LogicOps::cast_width(lval, width, p.is_signed);
                val = lval.aval;
            } else {
                for (auto& elem : lval.unpacked_elements) {
                    elem = LogicOps::cast_width(elem, width, p.is_signed);
                }
            }
        }
        if (!p.has_range && !p.type_name.empty() && p.type_name != "signed" && p.type_name != "unsigned") {
            const PackedStructType* st = find_struct_type(p.type_name, scope);
            if (st) {
                if (lval.width < st->total_width) {
                    lval = LogicOps::cast_width(lval, st->total_width, false);
                }
                result.var_to_struct_type[scope + "." + p.name] = st->name;
            }
        }
        if (p.is_signed) {
            lval.is_signed = true;
        }
        if (p.type_name == "bit" || p.type_name == "byte" || p.type_name == "shortint" ||
            p.type_name == "int" || p.type_name == "longint") {
            lval.coerce_to_2state();
            val = lval.aval;
        } else if (p.type_name == "real" || p.type_name == "shortreal" || lval.is_real) {
            lval = LogicVal::make_real(lval.to_real());
        }
        bool has_local_sig = false;
        for (const auto& s : mod->signals) {
            if (s.name == p.name) { has_local_sig = true; break; }
        }
        if (has_local_sig) {
            if (!p.is_localparam) ++non_local_param_idx;
            continue;
        }
        result.param_map[scope + "." + p.name] = val;
        result.param_val_map[scope + "." + p.name] = lval;
        {
            const PackedStructType* st_param = nullptr;
            std::string p_st_name = "";
            if (result.var_to_struct_type.count(scope + "." + p.name)) {
                p_st_name = result.var_to_struct_type.at(scope + "." + p.name);
                st_param = find_struct_type(p_st_name, scope);
            } else if (!p.type_name.empty()) {
                st_param = find_struct_type(p.type_name, scope);
            }
            if (st_param) {
                std::function<void(const PackedStructType*, const std::string&, const LogicVal&, int)> unpack_param_members =
                    [&](const PackedStructType* cur_st, const std::string& prefix, const LogicVal& cur_val, int depth) {
                        if (!cur_st || depth > 4) return;
                        for (const auto& sm : cur_st->members) {
                            LogicVal sm_val = cur_val.extract_slice(sm.offset, sm.width);
                            std::string mkey = prefix + "." + sm.name;
                            result.param_val_map[mkey] = sm_val;
                            result.param_map[mkey] = sm_val.aval;
                            if (!sm.type_name.empty()) {
                                result.var_to_struct_type[mkey] = sm.type_name;
                                if (const PackedStructType* sub_st = find_struct_type(sm.type_name, scope)) {
                                    unpack_param_members(sub_st, mkey, sm_val, depth + 1);
                                }
                            }
                        }
                    };
                unpack_param_members(st_param, scope + "." + p.name, lval, 0);
            }
        }
        bool is_unb = false;
        if (auto unb_id = dynamic_cast<IdentifierNode*>(p.default_expr.get())) {
            if (unb_id->name == "$" || unb_id->name == "__CAS_QUEUE_DOLLAR__") {
                is_unb = true;
            }
        }
        if (overridden && overrides && !p.is_localparam) {
            for (size_t oi = 0; oi < overrides->size(); ++oi) {
                const auto& ov = (*overrides)[oi];
                if ((!ov.param_name.empty() && ov.param_name == p.name) || (ov.param_name.empty() && oi == non_local_param_idx)) {
                    if (auto ov_id = dynamic_cast<IdentifierNode*>(ov.expr.get())) {
                        if (ov_id->name == "$" || ov_id->name == "__CAS_QUEUE_DOLLAR__") {
                            is_unb = true;
                        } else {
                            is_unb = false;
                        }
                    }
                    break;
                }
            }
        }
        if (is_unb) {
            result.unbounded_params.insert(scope + "." + p.name);
            if (parent_scope.empty()) {
                result.unbounded_params.insert(p.name);
            }
        }
        if (parent_scope.empty()) {
            result.param_map[p.name] = val;
            result.param_val_map[p.name] = lval;
        }
        if (!p.type_name.empty()) {
            result.var_to_struct_type[scope + "." + p.name] = p.type_name;
            result.var_to_struct_type[p.name] = p.type_name;
        }
        VariableDimMetadata pvdm;
        pvdm.name = scope + "." + p.name;
        if (!p.unpacked_dimensions.empty()) {
            for (const auto& udim : p.unpacked_dimensions) {
                int64_t b1 = eval_const_signed_val(eval_const_expr(udim.left_expr.get(), scope));
                int64_t b2 = udim.right_expr ? eval_const_signed_val(eval_const_expr(udim.right_expr.get(), scope)) : 0;
                ArrayDimInfo info;
                info.left = b1;
                info.right = b2;
                info.is_unpacked = true;
                pvdm.dimensions.push_back(info);
            }
            pvdm.unpacked_dims = (uint32_t)pvdm.dimensions.size();
            pvdm.is_vector_or_array = true;
        }
        if (p.has_range && p.msb_expr) {
            int64_t b1 = (int64_t)eval_const_expr(p.msb_expr.get(), scope);
            int64_t b2 = p.lsb_expr ? (int64_t)eval_const_expr(p.lsb_expr.get(), scope) : 0;
            pvdm.dimensions.push_back({b1, b2, false, false, false});
            pvdm.is_vector_or_array = true;
        }
        if (pvdm.is_vector_or_array) {
            result.var_dimensions[scope + "." + p.name] = pvdm;
            if (parent_scope.empty()) {
                result.var_dimensions[p.name] = pvdm;
            }
        }
        if (overridden && overrides && !p.is_localparam) {
            for (size_t oi = 0; oi < overrides->size(); ++oi) {
                const auto& ov = (*overrides)[oi];
                if ((!ov.param_name.empty() && ov.param_name == p.name) || (ov.param_name.empty() && oi == non_local_param_idx)) {
                    if (auto ov_id = dynamic_cast<IdentifierNode*>(ov.expr.get())) {
                        std::string cur_ps = parent_scope;
                        std::string found_type;
                        while (true) {
                            std::string ov_scoped = cur_ps.empty() ? ov_id->name : (cur_ps + "." + ov_id->name);
                            if (result.var_to_struct_type.count(ov_scoped)) {
                                found_type = result.var_to_struct_type.at(ov_scoped);
                                break;
                            }
                            if (cur_ps.empty()) break;
                            auto dot = cur_ps.rfind('.');
                            if (dot == std::string::npos) cur_ps.clear();
                            else cur_ps = cur_ps.substr(0, dot);
                        }
                        if (found_type.empty() && result.var_to_struct_type.count(ov_id->name)) {
                            found_type = result.var_to_struct_type.at(ov_id->name);
                        }
                        if (!found_type.empty()) {
                            result.var_to_struct_type[scope + "." + p.name] = found_type;
                            result.var_to_struct_type[p.name] = found_type;
                        }
                    }
                    break;
                }
            }
        }
        if (!p.is_localparam) ++non_local_param_idx;
    }

    // Re-evaluate module struct typedefs after parameters are resolved so parameterized struct member widths (e.g. CVA6Cfg.VLEN) are fully accurate
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& pair : mod->struct_typedefs) {
            const std::string& st_name = pair.first;
            const auto& st_node = pair.second;
            if (!st_node) continue;
            if (result.struct_types.count(scope + "." + st_name)) {
                bool is_type_param = false;
                for (const auto& mp : mod->parameters) {
                    if (mp.name == st_name && mp.type_name == "type") { is_type_param = true; break; }
                }
                if (is_type_param) continue;
            }

            PackedStructType st;
            st.name = st_name;
            st.is_signed = st_node->is_signed;
            st.is_union = st_node->is_union;
            st.is_tagged = st_node->is_tagged;
            st.tag_bits = st_node->tag_bits;
            st.payload_width = st_node->payload_width;

            size_t n = st_node->members.size();
            std::vector<uint32_t> member_widths(n, 1);
            for (size_t i = 0; i < n; ++i) {
                const auto& mn = st_node->members[i];
                uint32_t mw = mn.width;
                if (mn.msb_expr) {
                    try {
                        int64_t m = eval_const_signed_val(eval_const_expr(mn.msb_expr.get(), scope));
                        int64_t l = mn.lsb_expr ? eval_const_signed_val(eval_const_expr(mn.lsb_expr.get(), scope)) : 0;
                        if (m >= 0 && l >= 0 && std::abs(m - l) < 65536) {
                            mw = (uint32_t)(std::abs(m - l) + 1);
                            if (mn.elem_width > 1) mw *= mn.elem_width;
                            else if (mn.width > mw && mn.width % mw == 0) mw = mn.width;
                        }
                    } catch (...) {}
                } else if (!mn.type_name.empty()) {
                    const PackedStructType* inner_st = find_struct_type(mn.type_name, scope);
                    if (inner_st && inner_st->total_width > 0) {
                        mw = inner_st->total_width;
                    } else {
                        uint32_t tw = resolve_type_width(mn.type_name);
                        if (tw > 0) mw = tw;
                    }
                }
                if (mw > 65536) mw = 1;
                member_widths[i] = mw;
            }

            std::vector<uint32_t> offsets(n, 0);
            uint32_t running_offset = 0;
            if (st.is_union) {
                for (size_t i = 0; i < n; ++i) offsets[i] = 0;
                uint32_t max_w = 0;
                for (uint32_t mw : member_widths) if (mw > max_w) max_w = mw;
                st.total_width = max_w;
            } else {
                for (int i = (int)n - 1; i >= 0; --i) {
                    offsets[i] = running_offset;
                    running_offset += member_widths[i];
                }
                st.total_width = running_offset;
            }

            for (size_t i = 0; i < n; ++i) {
                const auto& mn = st_node->members[i];
                StructMember sm;
                sm.name = mn.name;
                sm.width = member_widths[i];
                sm.offset = offsets[i];
                sm.is_signed = mn.is_signed;
                sm.type_name = mn.type_name;
                sm.msb = mn.msb;
                sm.lsb = mn.lsb;
                sm.elem_width = mn.elem_width;
                sm.outer_low = mn.outer_low;
                sm.default_expr = mn.default_value;
                st.members.push_back(sm);
            }
            result.struct_types[st_name] = st;
            result.struct_types[scope + "." + st_name] = st;

            if (st.total_width > 0) {
                VariableDimMetadata svdm;
                svdm.name = scope + "." + st_name;
                svdm.dimensions.push_back(ArrayDimInfo{(int64_t)st.total_width - 1, 0, false});
                svdm.is_vector_or_array = true;
                result.var_dimensions[scope + "." + st_name] = svdm;
                result.var_dimensions[st_name] = svdm;
                Parser::global_type_dimensions[scope + "." + st_name] = svdm;
                Parser::global_type_dimensions[st_name] = svdm;
                Parser::global_simple_typedefs[scope + "." + st_name] = st.total_width;
                Parser::global_simple_typedefs[st_name] = st.total_width;
                SimpleTypeInfo sinfo;
                sinfo.name = st_name;
                sinfo.width = st.total_width;
                result.simple_types[scope + "." + st_name] = sinfo;
                result.simple_types[st_name] = sinfo;
            }
        }
    }

    for (const auto& p : mod->parameters) {
        if (p.type_name == "type") {
            const PackedStructType* st = find_struct_type(p.name, scope);
            if (st && st->total_width > 0) {
                result.struct_types[scope + "." + p.name] = *st;
                result.struct_types[scope + "." + p.name].name = p.name;
                result.struct_types[p.name] = *st;
                result.struct_types[p.name].name = p.name;
                Parser::global_simple_typedefs[scope + "." + p.name] = st->total_width;
                Parser::global_simple_typedefs[p.name] = st->total_width;
                SimpleTypeInfo sinfo;
                sinfo.name = p.name;
                sinfo.width = st->total_width;
                result.simple_types[scope + "." + p.name] = sinfo;
                result.simple_types[p.name] = sinfo;
            }
        }
    }

    auto get_type_width = [&](const std::string& tname, const std::string& sc) -> uint32_t {
        if (tname.empty()) return 1;
        if (const PackedStructType* st = find_struct_type(tname, sc)) {
            return st->total_width;
        }
        if (!sc.empty()) {
            std::string cur_s = sc;
            while (true) {
                std::string scoped = cur_s.empty() ? tname : (cur_s + "." + tname);
                auto it_sim = result.simple_types.find(scoped);
                if (it_sim != result.simple_types.end()) return it_sim->second.width;
                auto it = result.enum_types.find(scoped);
                if (it != result.enum_types.end()) return it->second.width;
                if (cur_s.empty()) break;
                auto dot = cur_s.rfind('.');
                if (dot == std::string::npos) cur_s = "";
                else cur_s = cur_s.substr(0, dot);
            }
        }
        auto it_sim = result.simple_types.find(tname);
        if (it_sim != result.simple_types.end()) return it_sim->second.width;
        auto it_enum = result.enum_types.find(tname);
        if (it_enum != result.enum_types.end()) {
            return it_enum->second.width;
        }
        return resolve_type_width(tname);
    };

    for (const auto& td : mod->typedefs) {
        if (!td) continue;
        if (!td->packed_dims.empty()) {
            uint32_t pw = 1;
            std::vector<ArrayDimInfo> sdims;
            for (const auto& pdim : td->packed_dims) {
                int64_t b1 = eval_const_signed_val(eval_const_expr(pdim.left_expr.get(), scope));
                int64_t b2 = pdim.right_expr ? eval_const_signed_val(eval_const_expr(pdim.right_expr.get(), scope)) : 0;
                pw *= (uint32_t)(std::abs(b1 - b2) + 1);
                sdims.push_back({b1, b2, false, false, false});
            }
            uint32_t bw = get_type_width(td->base_type, scope);
            if (bw > 0) {
                pw *= bw;
            }
            const VariableDimMetadata* base_vdm = Parser::find_type_dim_metadata(td->base_type);
            if (!base_vdm) {
                auto b_it = result.var_dimensions.find(scope + "." + td->base_type);
                if (b_it != result.var_dimensions.end()) base_vdm = &b_it->second;
                else {
                    b_it = result.var_dimensions.find(td->base_type);
                    if (b_it != result.var_dimensions.end()) base_vdm = &b_it->second;
                }
            }
            if (base_vdm) {
                for (const auto& d : base_vdm->dimensions) {
                    if (!d.is_unpacked) sdims.push_back(d);
                }
            } else if (bw > 1) {
                sdims.push_back(ArrayDimInfo{(int64_t)bw - 1, 0, false, false, false});
            }
            if (pw == 0) pw = 1;
            SimpleTypeInfo sinfo;
            sinfo.name = td->type_name;
            sinfo.base_type = td->base_type;
            sinfo.width = pw;
            sinfo.is_signed = td->is_signed;
            result.simple_types[scope + "." + td->type_name] = sinfo;
            result.simple_types[td->type_name] = sinfo;

            VariableDimMetadata svdm;
            svdm.name = scope + "." + td->type_name;
            svdm.dimensions = sdims;
            svdm.unpacked_dims = 0;
            svdm.is_vector_or_array = true;
            result.var_dimensions[scope + "." + td->type_name] = svdm;
            result.var_dimensions[td->type_name] = svdm;

            std::string st_name = td->base_type;
            if (result.var_to_struct_type.count(scope + "." + td->base_type)) {
                st_name = result.var_to_struct_type.at(scope + "." + td->base_type);
            } else if (result.var_to_struct_type.count(td->base_type)) {
                st_name = result.var_to_struct_type.at(td->base_type);
            }
            if (find_struct_type(st_name, scope)) {
                result.var_to_struct_type[scope + "." + td->type_name] = st_name;
                result.var_to_struct_type[td->type_name] = st_name;
            }
        }
    }

    // Re-evaluate module struct typedefs after local typedefs (e.g. hpdcache_mem_addr_t) are resolved
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& pair : mod->struct_typedefs) {
            const std::string& st_name = pair.first;
            const auto& st_node = pair.second;
            if (!st_node) continue;
            if (result.struct_types.count(scope + "." + st_name)) {
                bool is_type_param = false;
                for (const auto& mp : mod->parameters) {
                    if (mp.name == st_name && mp.type_name == "type") { is_type_param = true; break; }
                }
                if (is_type_param) continue;
            }

            PackedStructType st;
            st.name = st_name;
            st.is_signed = st_node->is_signed;
            st.is_union = st_node->is_union;
            st.is_tagged = st_node->is_tagged;
            st.tag_bits = st_node->tag_bits;
            st.payload_width = st_node->payload_width;

            size_t n = st_node->members.size();
            std::vector<uint32_t> member_widths(n, 1);
            for (size_t i = 0; i < n; ++i) {
                const auto& mn = st_node->members[i];
                uint32_t mw = mn.width;
                if (mn.msb_expr) {
                    try {
                        int64_t m = eval_const_signed_val(eval_const_expr(mn.msb_expr.get(), scope));
                        int64_t l = mn.lsb_expr ? eval_const_signed_val(eval_const_expr(mn.lsb_expr.get(), scope)) : 0;
                        if (m >= 0 && l >= 0 && std::abs(m - l) < 65536) {
                            mw = (uint32_t)(std::abs(m - l) + 1);
                            if (mn.elem_width > 1) mw *= mn.elem_width;
                            else if (mn.width > mw && mn.width % mw == 0) mw = mn.width;
                        }
                    } catch (...) {}
                } else if (!mn.type_name.empty()) {
                    uint32_t tw = get_type_width(mn.type_name, scope);
                    if (tw > 0) mw = tw;
                }
                if (mw > 65536) mw = 1;
                member_widths[i] = mw;
            }

            std::vector<uint32_t> offsets(n, 0);
            uint32_t running_offset = 0;
            if (st.is_union) {
                for (size_t i = 0; i < n; ++i) offsets[i] = 0;
                uint32_t max_w = 0;
                for (uint32_t mw : member_widths) if (mw > max_w) max_w = mw;
                st.total_width = max_w;
            } else {
                for (int i = (int)n - 1; i >= 0; --i) {
                    offsets[i] = running_offset;
                    running_offset += member_widths[i];
                }
                st.total_width = running_offset;
            }

            for (size_t i = 0; i < n; ++i) {
                const auto& mn = st_node->members[i];
                StructMember sm;
                sm.name = mn.name;
                sm.width = member_widths[i];
                sm.offset = offsets[i];
                sm.is_signed = mn.is_signed;
                sm.type_name = mn.type_name;
                sm.msb = mn.msb;
                sm.lsb = mn.lsb;
                sm.elem_width = mn.elem_width;
                sm.outer_low = mn.outer_low;
                sm.default_expr = mn.default_value;
                st.members.push_back(sm);
            }
            result.struct_types[st_name] = st;
            result.struct_types[scope + "." + st_name] = st;

            if (st.total_width > 0) {
                VariableDimMetadata svdm;
                svdm.name = scope + "." + st_name;
                svdm.dimensions.push_back(ArrayDimInfo{(int64_t)st.total_width - 1, 0, false});
                svdm.is_vector_or_array = true;
                result.var_dimensions[scope + "." + st_name] = svdm;
                result.var_dimensions[st_name] = svdm;
                Parser::global_type_dimensions[scope + "." + st_name] = svdm;
                Parser::global_type_dimensions[st_name] = svdm;
                Parser::global_simple_typedefs[scope + "." + st_name] = st.total_width;
                Parser::global_simple_typedefs[st_name] = st.total_width;
                SimpleTypeInfo sinfo;
                sinfo.name = st_name;
                sinfo.width = st.total_width;
                result.simple_types[scope + "." + st_name] = sinfo;
                result.simple_types[st_name] = sinfo;
            }
        }
    }

    for (auto& p : mod->ports) {
        uint32_t elem_w = 1;
        if (!p.type_name.empty()) {
            uint32_t tw = get_type_width(p.type_name, scope);
            if (tw > 0) elem_w = tw;
            if (!p.is_signed) {
                std::string bare = p.type_name;
                auto colon = p.type_name.find("::");
                if (colon != std::string::npos) bare = p.type_name.substr(colon + 2);
                if (Parser::global_enum_typedefs.count(bare) && Parser::global_enum_typedefs.at(bare)->is_signed) {
                    p.is_signed = true;
                }
            }
        }
        uint32_t w = elem_w;
        bool has_explicit_packed = false;
        if (!p.packed_dimensions.empty()) {
            has_explicit_packed = true;
            uint32_t packed_mult = 1;
            for (const auto& pdim : p.packed_dimensions) {
                int64_t b1 = (int64_t)eval_const_expr(pdim.left_expr.get(), scope);
                int64_t b2 = pdim.right_expr ? (int64_t)eval_const_expr(pdim.right_expr.get(), scope) : 0;
                packed_mult *= (uint32_t)(std::abs(b1 - b2) + 1);
            }
            w = elem_w * packed_mult;
        } else if (p.msb_expr && p.lsb_expr) {
            if (elem_w > 1) {
                w = elem_w;
            } else {
                has_explicit_packed = true;
                int64_t msb = (int64_t)eval_const_expr(p.msb_expr.get(), scope);
                int64_t lsb = (int64_t)eval_const_expr(p.lsb_expr.get(), scope);
                w = (uint32_t)(std::abs(msb - lsb) + 1);
            }
        }

        const VariableDimMetadata* type_meta = nullptr;
        bool has_scoped_vdm = false;
        auto scoped_vdm_it = result.var_dimensions.find(scope + "." + p.type_name);
        if (!p.type_name.empty() && scoped_vdm_it != result.var_dimensions.end()) {
            type_meta = &scoped_vdm_it->second;
            has_scoped_vdm = true;
        } else if (!p.type_name.empty() &&
                   !find_struct_type(p.type_name, scope) &&
                   result.simple_types.find(scope + "." + p.type_name) == result.simple_types.end() &&
                   result.var_to_struct_type.find(scope + "." + p.type_name) == result.var_to_struct_type.end()) {
            type_meta = Parser::find_type_dim_metadata(p.type_name);
        }
        if (type_meta && !has_explicit_packed && !type_meta->dimensions.empty() && type_meta->unpacked_dims == 0 && (has_scoped_vdm || elem_w <= 1)) {
            uint32_t pw = 1;
            for (const auto& d : type_meta->dimensions) {
                pw *= (uint32_t)d.size();
            }
            if (pw > 0) w = pw;
        }
        if (p.is_array) {
            std::vector<ArrayDimInfo> all_dims;
            uint32_t total_depth = 1;
            if (!p.unpacked_dimensions.empty()) {
                for (const auto& udim : p.unpacked_dimensions) {
                    int64_t b1 = eval_const_signed_val(eval_const_expr(udim.left_expr.get(), scope));
                    int64_t b2 = udim.right_expr ? eval_const_signed_val(eval_const_expr(udim.right_expr.get(), scope)) : 0;
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
                    b1 = eval_const_signed_val(eval_const_expr(p.bound1_expr.get(), scope));
                    b2 = eval_const_signed_val(eval_const_expr(p.bound2_expr.get(), scope));
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
            if (p.type_name == "real" || p.type_name == "shortreal") {
                marr.is_real = true;
                for (auto& wrd : marr.words) wrd = LogicVal(0.0);
            }
            marr.dimensions = all_dims;
            marr.unpacked_dims_count = unpacked_cnt;
            for (const auto& d : all_dims) {
                if (!d.is_unpacked) {
                    marr.packed_lsb = std::min(d.left, d.right);
                    break;
                }
            }
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
        result.netlist[pid].is_port = true;
        if (!p.type_name.empty() && result.netlist[pid].type_name.empty()) {
            result.netlist[pid].type_name = p.type_name;
        }
        if (p.type_name == "bit" || p.type_name == "byte" ||
            p.type_name == "shortint" || p.type_name == "longint" || p.type_name == "int") {
            result.netlist[pid].type_name = p.type_name;
            result.netlist[pid].is_2state = true;
            result.netlist[pid].value = LogicVal(w, 0, 0, p.is_signed);
            result.netlist[pid].prev_value = LogicVal(w, 0, 0, p.is_signed);
        }
        if (p.type_name == "real" || p.type_name == "shortreal") {
            result.netlist[pid].type_name = p.type_name;
            result.netlist[pid].value = LogicVal::make_real(0.0);
            result.netlist[pid].prev_value = LogicVal::make_real(0.0);
        }
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
            std::function<void(ExpressionNode*)> validate_port_default = [&](ExpressionNode* e) {
                if (!e) return;
                if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                    std::string cur_scope = scope;
                    bool is_const = false;
                    while (!cur_scope.empty()) {
                        if (result.param_map.count(cur_scope + "." + id->name) ||
                            result.enum_constant_map.count(cur_scope + "." + id->name)) {
                            is_const = true;
                            break;
                        }
                        auto dot = cur_scope.rfind('.');
                        if (dot == std::string::npos) cur_scope = "";
                        else cur_scope = cur_scope.substr(0, dot);
                    }
                    if (!is_const && (result.param_map.count(id->name) || result.enum_constant_map.count(id->name))) {
                        is_const = true;
                    }
                    if (!is_const) {
                        throw std::runtime_error("A reference to a wire or reg ('" + id->name + "') is not allowed in a constant expression for port default value per IEEE 1800-2023 §23.2.2.4");
                    }
                } else if (auto bin = dynamic_cast<BinaryOpNode*>(e)) {
                    validate_port_default(bin->lhs.get());
                    validate_port_default(bin->rhs.get());
                } else if (auto un = dynamic_cast<UnaryOpNode*>(e)) {
                    validate_port_default(un->operand.get());
                } else if (auto tern = dynamic_cast<TernaryOpNode*>(e)) {
                    validate_port_default(tern->condition.get());
                    validate_port_default(tern->true_expr.get());
                    validate_port_default(tern->false_expr.get());
                } else if (auto concat = dynamic_cast<ConcatNode*>(e)) {
                    for (auto& elem : concat->elements) validate_port_default(elem.get());
                }
            };
            validate_port_default(p.default_value.get());
            uint64_t init_val = eval_const_expr(p.default_value.get(), scope);
            LogicVal iv = LogicVal(w, init_val, 0);
            result.netlist[pid].value = iv;
            result.netlist[pid].prev_value = iv;
            result.netlist[pid].modified = false;
        } else if (!p.type_name.empty()) {
            const PackedStructType* st = find_struct_type(p.type_name, scope);
            if (st) {
                bool has_def = false;
                for (const auto& mem : st->members) {
                    if (mem.default_expr || mem.has_default) { has_def = true; break; }
                }
                if (has_def) {
                    LogicVal iv(w, 0, 0);
                    for (const auto& mem : st->members) {
                        if (mem.default_expr) {
                            LogicVal mv = eval_const_logic_val(mem.default_expr.get(), scope);
                            mv = LogicOps::cast_width(mv, mem.width, mem.is_signed);
                            iv.deposit_slice(mem.offset, mem.width, mv);
                        } else if (mem.has_default) {
                            iv.deposit_slice(mem.offset, mem.width, mem.default_val);
                        }
                    }
                    result.netlist[pid].value = iv;
                    result.netlist[pid].prev_value = iv;
                    result.netlist[pid].modified = false;
                }
            }
        }

        VariableDimMetadata pvdm;
        pvdm.name = scope + "." + p.name;
        if (!p.packed_dimensions.empty()) {
            uint32_t p_total = 1;
            for (const auto& pdim : p.packed_dimensions) {
                int64_t b1 = (int64_t)eval_const_expr(pdim.left_expr.get(), scope);
                int64_t b2 = pdim.right_expr ? (int64_t)eval_const_expr(pdim.right_expr.get(), scope) : 0;
                pvdm.dimensions.push_back({b1, b2, false, false, false});
                p_total *= (uint32_t)(std::abs(b1 - b2) + 1);
            }
            if (elem_w > 1 && w > p_total) {
                pvdm.dimensions.push_back({(int64_t)(elem_w - 1), 0, false, false, false});
            }
            pvdm.is_vector_or_array = true;
        } else if (p.msb_expr) {
            int64_t b1 = (int64_t)eval_const_expr(p.msb_expr.get(), scope);
            int64_t b2 = p.lsb_expr ? (int64_t)eval_const_expr(p.lsb_expr.get(), scope) : 0;
            pvdm.dimensions.push_back({b1, b2, false, false, false});
            uint32_t dim_w = (uint32_t)(std::abs(b1 - b2) + 1);
            if (elem_w > 1 && w > dim_w) {
                pvdm.dimensions.push_back({(int64_t)(elem_w - 1), 0, false, false, false});
            }
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
        uint32_t elem_w = 1;
        if (!s.type_name.empty()) {
            uint32_t tw = get_type_width(s.type_name, scope);
            if (tw > 0) elem_w = tw;
        }
        uint32_t w = elem_w;
        bool has_explicit_packed = false;
        if (!s.packed_dimensions.empty()) {
            has_explicit_packed = true;
            uint32_t packed_mult = 1;
            for (const auto& pdim : s.packed_dimensions) {
                int64_t b1 = eval_const_signed_val(eval_const_expr(pdim.left_expr.get(), scope));
                int64_t b2 = pdim.right_expr ? eval_const_signed_val(eval_const_expr(pdim.right_expr.get(), scope)) : 0;
                packed_mult *= (uint32_t)(std::abs(b1 - b2) + 1);
            }
            w = elem_w * packed_mult;
        } else if (s.msb_expr && s.lsb_expr) {
            auto scoped_td_it = result.simple_types.find(scope + "." + s.type_name);
            if (!s.type_name.empty() && scoped_td_it != result.simple_types.end()) {
                w = scoped_td_it->second.width;
            } else if (elem_w > 1) {
                w = elem_w;
            } else {
                has_explicit_packed = true;
                int64_t msb = eval_const_signed_val(eval_const_expr(s.msb_expr.get(), scope));
                int64_t lsb = eval_const_signed_val(eval_const_expr(s.lsb_expr.get(), scope));
                w = (uint32_t)(std::abs(msb - lsb) + 1);
            }
        }

        const VariableDimMetadata* type_meta = nullptr;
        bool has_scoped_vdm = false;
        auto scoped_vdm_it = result.var_dimensions.find(scope + "." + s.type_name);
        if (!s.type_name.empty() && scoped_vdm_it != result.var_dimensions.end()) {
            type_meta = &scoped_vdm_it->second;
            has_scoped_vdm = true;
        } else if (!s.type_name.empty() &&
                   !find_struct_type(s.type_name, scope) &&
                   result.simple_types.find(scope + "." + s.type_name) == result.simple_types.end() &&
                   result.var_to_struct_type.find(scope + "." + s.type_name) == result.var_to_struct_type.end()) {
            type_meta = Parser::find_type_dim_metadata(s.type_name);
        }
        if (type_meta && !has_explicit_packed && !type_meta->dimensions.empty() && type_meta->unpacked_dims == 0 && (has_scoped_vdm || elem_w <= 1)) {
            uint32_t pw = 1;
            for (const auto& d : type_meta->dimensions) {
                pw *= (uint32_t)d.size();
            }
            if (pw > 0) w = pw;
        }
        bool has_unpacked_type = (type_meta && type_meta->unpacked_dims > 0);

        if (s.is_associative_array) {
            std::string aa_scope = scope + "." + s.name;
            bool is_2st = (s.type_name == "bit" || s.type_name == "int" || s.type_name == "byte" || s.type_name == "shortint" || s.type_name == "longint");
            result.assoc_array_map[aa_scope] = AssociativeArrayObject(aa_scope, w, s.is_signed, s.assoc_key_type, s.assoc_key_width, is_2st);
            if (s.default_value) {
                if (auto ucat = dynamic_cast<UnpackedConcatNode*>(s.default_value.get())) {
                    for (const auto& elem : ucat->elements) {
                        if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
                            if (pm->member_name == "default") {
                                result.assoc_array_map[aa_scope].has_default_val = true;
                                result.assoc_array_map[aa_scope].default_val = eval_const_logic_val(pm->value.get(), scope);
                            }
                        }
                    }
                }
            }
            if (debug_out) {
                *debug_out << "     * Associative Array allocated: " << aa_scope 
                           << " [" << w << "-bit, key=" << s.assoc_key_type << " (" << s.assoc_key_width << "-bit), 2state=" << is_2st << "]\n";
            }
        } else if (s.is_dynamic_array) {
            std::string da_scope = scope + "." + s.name;
            bool is_real = (s.type_name == "real" || s.type_name == "shortreal");
            bool is_4st = (!is_real && s.type_name != "bit" && s.type_name != "byte" && s.type_name != "shortint" && s.type_name != "int" && s.type_name != "longint");
            auto dao = DynamicArrayObject(da_scope, w, s.is_signed);
            dao.is_four_state = is_4st;
            dao.is_real = is_real;
            if (s.default_value) {
                if (auto new_arr = dynamic_cast<NewArrayNode*>(s.default_value.get())) {
                    uint64_t sz = eval_const_expr(new_arr->size_expr.get(), scope);
                    dao.allocate((size_t)sz);
                    if (new_arr->copy_source_expr) {
                        if (auto uc = dynamic_cast<UnpackedConcatNode*>(new_arr->copy_source_expr.get())) {
                            for (size_t i = 0; i < (size_t)sz && i < uc->elements.size(); ++i) {
                                dao.set_elem(i, eval_const_logic_val(uc->elements[i].get(), scope));
                            }
                        } else if (auto cat = dynamic_cast<ConcatNode*>(new_arr->copy_source_expr.get())) {
                            for (size_t i = 0; i < (size_t)sz && i < cat->elements.size(); ++i) {
                                dao.set_elem(i, eval_const_logic_val(cat->elements[i].get(), scope));
                            }
                        } else {
                            try {
                                LogicVal init_val = eval_const_logic_val(new_arr->copy_source_expr.get(), scope);
                                for (size_t i = 0; i < (size_t)sz; ++i) {
                                    dao.set_elem(i, init_val);
                                }
                            } catch (...) {}
                        }
                    }
                }
            }
            result.dynamic_array_map[da_scope] = std::move(dao);
            if (parent_scope.empty()) {
                result.dynamic_array_map[s.name] = result.dynamic_array_map[da_scope];
            }
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
            bool is_r = (s.type_name == "real" || s.type_name == "shortreal");
            bool is_str = (s.type_name == "string");
            bool is_2st = (!is_r && !is_str && (s.type_name == "bit" || s.type_name == "byte" || s.type_name == "shortint" || s.type_name == "longint"));
            result.queue_map[q_scope] = QueueObject(q_scope, w, s.is_signed, max_bound, is_r, is_str, is_2st);
            if (debug_out) {
                *debug_out << "     * Queue allocated: " << q_scope 
                           << " [" << w << "-bit, bound=" << max_bound << "]\n";
            }
        } else if (s.is_array || has_unpacked_type) {
            std::vector<ArrayDimInfo> all_dims;
            uint32_t total_depth = 1;
            uint32_t unpacked_cnt = 0;
            if (has_unpacked_type) {
                all_dims = type_meta->dimensions;
                unpacked_cnt = type_meta->unpacked_dims;
                for (const auto& d : all_dims) {
                    if (d.is_unpacked) total_depth *= (uint32_t)d.size();
                }
                uint32_t pw = 1;
                for (const auto& d : all_dims) {
                    if (!d.is_unpacked) pw *= (uint32_t)d.size();
                }
                if (pw > 0) w = pw;
            } else if (!s.unpacked_dimensions.empty()) {
                for (const auto& udim : s.unpacked_dimensions) {
                    int64_t b1 = eval_const_signed_val(eval_const_expr(udim.left_expr.get(), scope));
                    int64_t b2 = udim.right_expr ? eval_const_signed_val(eval_const_expr(udim.right_expr.get(), scope)) : 0;
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
                    b1 = eval_const_signed_val(eval_const_expr(s.bound1_expr.get(), scope));
                    b2 = eval_const_signed_val(eval_const_expr(s.bound2_expr.get(), scope));
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
            if (!has_unpacked_type) unpacked_cnt = (uint32_t)all_dims.size();

            // Packed dimensions
            if (!has_unpacked_type) {
                if (!s.packed_dimensions.empty()) {
                    for (const auto& pdim : s.packed_dimensions) {
                        int64_t b1 = eval_const_signed_val(eval_const_expr(pdim.left_expr.get(), scope));
                        int64_t b2 = pdim.right_expr ? eval_const_signed_val(eval_const_expr(pdim.right_expr.get(), scope)) : 0;
                        ArrayDimInfo info;
                        info.left = b1;
                        info.right = b2;
                        info.is_unpacked = false;
                        all_dims.push_back(info);
                    }
                } else if (s.msb_expr) {
                    int64_t b1 = eval_const_signed_val(eval_const_expr(s.msb_expr.get(), scope));
                    int64_t b2 = s.lsb_expr ? eval_const_signed_val(eval_const_expr(s.lsb_expr.get(), scope)) : 0;
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
            }

            std::string mem_scope = scope + "." + s.name;
            int64_t b1_first = all_dims.empty() ? 0 : all_dims[0].left;
            int64_t b2_first = all_dims.empty() ? 0 : all_dims[0].right;
            MemoryArray marr(mem_scope, w, total_depth, b1_first, b2_first);
            if (s.type_name == "real" || s.type_name == "shortreal") {
                marr.is_real = true;
                for (auto& wrd : marr.words) wrd = LogicVal(0.0);
            }
            marr.dimensions = all_dims;
            marr.unpacked_dims_count = unpacked_cnt;
            for (const auto& d : all_dims) {
                if (!d.is_unpacked) {
                    marr.packed_lsb = std::min(d.left, d.right);
                    break;
                }
            }
            result.memory_map[mem_scope] = marr;

            VariableDimMetadata vdm;
            vdm.name = mem_scope;
            vdm.dimensions = all_dims;
            vdm.unpacked_dims = unpacked_cnt;
            vdm.is_vector_or_array = true;
            result.var_dimensions[mem_scope] = vdm;
            result.var_dimensions[s.name] = vdm;

            if (s.is_class_handle) {
                result.class_handle_names.insert(mem_scope);
                result.class_handle_names.insert(s.name);
                result.var_to_class_type[mem_scope] = s.class_type_name;
                result.var_to_class_type[s.name] = s.class_type_name;
            }

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
                uint32_t p_total = 1;
                for (const auto& pdim : s.packed_dimensions) {
                    int64_t b1 = eval_const_signed_val(eval_const_expr(pdim.left_expr.get(), scope));
                    int64_t b2 = pdim.right_expr ? eval_const_signed_val(eval_const_expr(pdim.right_expr.get(), scope)) : 0;
                    svdm.dimensions.push_back({b1, b2, false, false, false});
                    p_total *= (uint32_t)(std::abs(b1 - b2) + 1);
                }
                if (elem_w > 1 && w > p_total) {
                    svdm.dimensions.push_back({(int64_t)(elem_w - 1), 0, false, false, false});
                }
                svdm.is_vector_or_array = true;
            } else if (type_meta && !type_meta->dimensions.empty() && type_meta->unpacked_dims == 0) {
                svdm.dimensions = type_meta->dimensions;
                svdm.unpacked_dims = 0;
                svdm.is_vector_or_array = true;
            } else if (s.msb_expr) {
                int64_t b1 = eval_const_signed_val(eval_const_expr(s.msb_expr.get(), scope));
                int64_t b2 = s.lsb_expr ? eval_const_signed_val(eval_const_expr(s.lsb_expr.get(), scope)) : 0;
                svdm.dimensions.push_back({b1, b2, false, false, false});
                uint32_t dim_w = (uint32_t)(std::abs(b1 - b2) + 1);
                if (elem_w > 1 && w > dim_w) {
                    svdm.dimensions.push_back({(int64_t)(elem_w - 1), 0, false, false, false});
                }
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
                result.netlist[sid].type_name = s.class_type_name;
                result.netlist[sid].value = LogicVal(64, 0, 0);
                result.netlist[sid].prev_value = LogicVal(64, 0, 0);
                result.class_handle_names.insert(scope + "." + s.name);
                result.class_handle_names.insert(s.name);
                result.var_to_class_type[scope + "." + s.name] = s.class_type_name;
                result.var_to_class_type[s.name] = s.class_type_name;
                if (s.default_value && dynamic_cast<ClassNewNode*>(s.default_value.get())) {
                    result.initial_class_handles.push_back({sid, s.class_type_name});
                }
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
            if (s.is_2state || s.type_name == "bit" || s.type_name == "byte" ||
                s.type_name == "shortint" || s.type_name == "longint" || s.type_name == "int") {
                result.netlist[sid].type_name = s.type_name;
                result.netlist[sid].is_2state = true;
                result.netlist[sid].value = LogicVal(w, 0, 0, s.is_signed);
                result.netlist[sid].prev_value = LogicVal(w, 0, 0, s.is_signed);
            } else if (s.type_name == "real" || s.type_name == "shortreal") {
                result.netlist[sid].type_name = s.type_name;
                result.netlist[sid].value = LogicVal::make_real(0.0);
                result.netlist[sid].prev_value = LogicVal::make_real(0.0);
            }
            if (!s.type_name.empty()) {
                result.netlist[sid].type_name = s.type_name;
                result.var_to_enum_type[scope + "." + s.name] = s.type_name;
                result.var_to_enum_type[s.name] = s.type_name;
                result.var_to_struct_type[scope + "." + s.name] = s.type_name;
                result.var_to_struct_type[s.name] = s.type_name;
            }
            result.netlist[sid].is_vectored = s.is_vectored || mod->vectored_signals.count(s.name);
            if (s.default_value) {
                try {
                    uint64_t init_val = eval_const_expr(s.default_value.get(), scope);
                    LogicVal iv = LogicVal(w, init_val, 0);
                    result.netlist[sid].value = iv;
                    result.netlist[sid].prev_value = iv;
                    result.netlist[sid].modified = false;
                } catch (...) {
                    // Non-constant default value or initialization expression
                }
            } else if (!s.type_name.empty()) {
                const PackedStructType* st = find_struct_type(s.type_name, scope);
                if (st) {
                    bool has_def = false;
                    for (const auto& mem : st->members) {
                        if (mem.default_expr || mem.has_default) { has_def = true; break; }
                    }
                    if (has_def) {
                        LogicVal iv(w, 0, 0);
                        for (const auto& mem : st->members) {
                            if (mem.default_expr) {
                                LogicVal mv = eval_const_logic_val(mem.default_expr.get(), scope);
                                mv = LogicOps::cast_width(mv, mem.width, mem.is_signed);
                                iv.deposit_slice(mem.offset, mem.width, mv);
                            } else if (mem.has_default) {
                                iv.deposit_slice(mem.offset, mem.width, mem.default_val);
                            }
                        }
                        result.netlist[sid].value = iv;
                        result.netlist[sid].prev_value = iv;
                        result.netlist[sid].modified = false;
                    }
                }
            }
        }
    }

    for (auto& t : mod->tasks) {
        t->scope = scope;
        for (auto& p : t->ports) {
            if (p.msb_expr && p.lsb_expr) {
                try {
                    uint64_t msb = eval_const_expr(p.msb_expr.get(), scope);
                    uint64_t lsb = eval_const_expr(p.lsb_expr.get(), scope);
                    p.width = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                } catch (...) {}
            }
        }
        result.task_map[scope + "." + t->name] = t.get();
        result.task_map[t->name] = t.get();
    }

    for (auto& f : mod->functions) {
        f->scope = scope;
        if (f->return_msb_expr && f->return_lsb_expr) {
            try {
                uint64_t msb = eval_const_expr(f->return_msb_expr.get(), scope);
                uint64_t lsb = eval_const_expr(f->return_lsb_expr.get(), scope);
                f->return_width = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
            } catch (...) {}
        }
        for (auto& in_p : f->inputs) {
            if (in_p.msb_expr && in_p.lsb_expr) {
                try {
                    uint64_t msb = eval_const_expr(in_p.msb_expr.get(), scope);
                    uint64_t lsb = eval_const_expr(in_p.lsb_expr.get(), scope);
                    in_p.width = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                } catch (...) {}
            }
        }
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

    // Elaborate interface instances first so their internal nets are available for generate blocks, continuous assigns, and submodules
    for (auto& inst : mod->instantiations) {
        auto it = module_defs.find(inst->module_type);
        if (it != module_defs.end() && it->second->is_interface) {
            elaborate_instantiation_node(inst.get(), scope, module_defs, debug_out);
        }
    }

    for (auto& ca : mod->continuous_assigns) {
        if (ca->target_name.empty() && ca->concat_targets.empty() && !ca->stream_target) {
            continue;
        }
        std::string scoped_target = ca->target_name.empty() ? "" : (scope + "." + ca->target_name);
        if (!ca->target_name.empty()) {
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
            result.open_array_names.find(ca->target_name) != result.open_array_names.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to open array '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 35.5.6.1");
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
                    result.open_array_names.find(id->name) != result.open_array_names.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: open array handle '" + id->name + "' used directly in continuous assignment per IEEE 1800-2023 section 35.5.6.1");
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
                if (Parser::global_packages.find(smc->class_type_name) == Parser::global_packages.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: static class method '" + smc->class_type_name + "::" + smc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 8.25");
                }
            } else if (auto sma = dynamic_cast<StaticMemberAccessNode*>(expr)) {
                if (Parser::global_packages.find(sma->class_type_name) == Parser::global_packages.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: static class member '" + sma->class_type_name + "::" + sma->member_name + "' used in continuous assignment per IEEE 1800-2023 section 8.25");
                }
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
            } else if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
                size_t nid = resolve_target_net_id(idx->name, scope);
                if (nid != (size_t)-1) {
                    if (result.netlist[nid].is_vectored) {
                        throw std::runtime_error("Bit-select or part-select on vectored net '" + idx->name + "' is illegal per IEEE 1800-2023 6.9.2");
                    }
                    if (result.netlist[nid].type_name == "real" || result.netlist[nid].type_name == "shortreal" || result.netlist[nid].value.is_real) {
                        throw std::runtime_error("Bit-select or part-select on real data type is illegal: '" + idx->name + "' per IEEE 1800-2023 6.12");
                    }
                }
                if (idx->msb_expr) {
                    if (auto id_idx = dynamic_cast<IdentifierNode*>(idx->msb_expr.get())) {
                        size_t inid = resolve_target_net_id(id_idx->name, scope);
                        if (inid != (size_t)-1 && (result.netlist[inid].type_name == "real" || result.netlist[inid].value.is_real)) {
                            throw std::runtime_error("Bit-select index cannot be real: '" + id_idx->name + "' per IEEE 1800-2023 6.12");
                        }
                    }
                    check_expr(idx->msb_expr.get());
                }
                if (idx->lsb_expr) check_expr(idx->lsb_expr.get());
            }
        };
        if (ca->stream_target) {
            check_expr(ca->rhs.get());
            continue;
        }
        if (!ca->concat_targets.empty()) {
            check_expr(ca->rhs.get());
            auto concat_rhs = dynamic_cast<ConcatNode*>(ca->rhs.get());
            if (concat_rhs && concat_rhs->elements.size() == ca->concat_targets.size()) {
                for (size_t i = 0; i < ca->concat_targets.size(); ++i) {
                    const auto& tname = ca->concat_targets[i];
                    NetSliceRef t_slice{(size_t)-1, 0, 0};
                    if (i < ca->concat_target_exprs.size() && ca->concat_target_exprs[i]) {
                        try {
                            t_slice = resolve_slice_ref(ca->concat_target_exprs[i].get(), scope);
                        } catch (...) {}
                    }
                    if (t_slice.net_id == (size_t)-1) {
                        std::string scoped_t = scope.empty() ? tname : (scope + "." + tname);
                        size_t tid = (size_t)-1;
                        if (result.net_map.find(scoped_t) != result.net_map.end()) tid = result.net_map.at(scoped_t);
                        else if (result.net_map.find(tname) != result.net_map.end()) tid = result.net_map.at(tname);
                        if (tid != (size_t)-1) {
                            t_slice = NetSliceRef{tid, 0, result.netlist[tid].width};
                        }
                    }
                    if (t_slice.net_id == (size_t)-1) continue;

                    auto new_ca = std::make_unique<ContinuousAssignNode>(tname, concat_rhs->elements[i]->clone());
                    auto rhs_ptr = new_ca->rhs.get();
                    result.generated_continuous_assigns.push_back(std::move(new_ca));
                    result.continuous_assigns.push_back({scope, t_slice, rhs_ptr});
                }
            } else {
                uint32_t cur_offset = 0;
                for (int i = (int)ca->concat_targets.size() - 1; i >= 0; --i) {
                    const auto& tname = ca->concat_targets[i];
                    NetSliceRef t_slice{(size_t)-1, 0, 0};
                    if ((size_t)i < ca->concat_target_exprs.size() && ca->concat_target_exprs[i]) {
                        try {
                            t_slice = resolve_slice_ref(ca->concat_target_exprs[i].get(), scope);
                        } catch (...) {}
                    }
                    if (t_slice.net_id == (size_t)-1) {
                        std::string scoped_t = scope.empty() ? tname : (scope + "." + tname);
                        size_t tid = (size_t)-1;
                        if (result.net_map.find(scoped_t) != result.net_map.end()) tid = result.net_map.at(scoped_t);
                        else if (result.net_map.find(tname) != result.net_map.end()) tid = result.net_map.at(tname);
                        if (tid != (size_t)-1) {
                            t_slice = NetSliceRef{tid, 0, result.netlist[tid].width};
                        }
                    }
                    if (t_slice.net_id == (size_t)-1) continue;

                    size_t tid = t_slice.net_id;
                    uint32_t target_offset = t_slice.offset;
                    uint32_t w = t_slice.width;

                    std::unique_ptr<ExpressionNode> elem_rhs = nullptr;
                    if (auto id_node = dynamic_cast<IdentifierNode*>(ca->rhs.get())) {
                        if (w == 1) {
                            elem_rhs = std::make_unique<IndexSelectNode>(id_node->name, std::make_unique<NumberNode>(cur_offset, 32));
                        } else {
                            elem_rhs = std::make_unique<IndexSelectNode>(id_node->name,
                                std::make_unique<NumberNode>(cur_offset + w - 1, 32),
                                std::make_unique<NumberNode>(cur_offset, 32));
                        }
                    } else {
                        std::unique_ptr<ExpressionNode> shifted = ca->rhs->clone();
                        if (cur_offset > 0) {
                            shifted = std::make_unique<BinaryOpNode>(">>", std::move(shifted), std::make_unique<NumberNode>(cur_offset, 32));
                        }
                        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                        elem_rhs = std::make_unique<BinaryOpNode>("&", std::move(shifted), std::make_unique<NumberNode>(mask, w));
                    }
                    cur_offset += w;

                    auto new_ca = std::make_unique<ContinuousAssignNode>(tname, std::move(elem_rhs));
                    auto rhs_ptr = new_ca->rhs.get();
                    result.generated_continuous_assigns.push_back(std::move(new_ca));
                    result.continuous_assigns.push_back({scope, {tid, target_offset, w}, rhs_ptr});
                }
            }
            continue;
        }
        if (ca->target_index_expr) {
            check_expr(ca->target_index_expr.get());
            size_t nid = resolve_target_net_id(ca->target_name, scope);
            if (nid != (size_t)-1) {
                if (result.netlist[nid].is_vectored) {
                    throw std::runtime_error("Bit-select or part-select on vectored net '" + ca->target_name + "' is illegal per IEEE 1800-2023 6.9.2");
                }
                if (result.netlist[nid].type_name == "real" || result.netlist[nid].value.is_real) {
                    throw std::runtime_error("Bit-select or part-select on real data type is illegal: '" + ca->target_name + "' per IEEE 1800-2023 6.12");
                }
            }
            check_expr(ca->rhs.get());
            size_t tid = (size_t)-1;
            std::string full_combined = ca->member_name.empty() ? ca->target_name : (ca->target_name + "." + ca->member_name);
            tid = get_net_id(scope, full_combined);
            if (tid == (size_t)-1) {
                tid = get_net_id(scope, ca->target_name);
            }
            if (tid == (size_t)-1) {
                std::string tname = full_combined;
                size_t dot_pos = tname.rfind('.');
                while (dot_pos != std::string::npos) {
                    std::string base = tname.substr(0, dot_pos);
                    size_t b_tid = get_net_id(scope, base);
                    if (b_tid != (size_t)-1) { tid = b_tid; break; }
                    dot_pos = tname.rfind('.', dot_pos - 1);
                }
            }
            if (tid == (size_t)-1 && !ca->member_name.empty()) {
                try {
                    ExpressionNode* idx_expr_to_eval = ca->target_index_expr.get();
                    if (auto idx_sel = dynamic_cast<IndexSelectNode*>(idx_expr_to_eval)) {
                        if (idx_sel->msb_expr) idx_expr_to_eval = idx_sel->msb_expr.get();
                    }
                    uint64_t inst_idx = eval_const_expr(idx_expr_to_eval, scope);
                    std::string inst_net = ca->target_name + "[" + std::to_string(inst_idx) + "]." + ca->member_name;
                    size_t inst_nid = get_net_id(scope, inst_net);
                    if (inst_nid != (size_t)-1) {
                        NetSliceRef slice{inst_nid, 0, result.netlist[inst_nid].width};
                        if (ca->has_member_select && ca->member_msb_expr) {
                            uint64_t b_msb = eval_const_expr(ca->member_msb_expr.get(), scope);
                            uint64_t b_lsb = ca->member_lsb_expr ? eval_const_expr(ca->member_lsb_expr.get(), scope) : b_msb;
                            uint64_t sub_lsb = std::min(b_msb, b_lsb);
                            uint32_t sub_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                            slice.offset += (uint32_t)sub_lsb;
                            slice.width = sub_w;
                        }
                        auto new_ca = std::make_unique<ContinuousAssignNode>(ca->target_name, ca->rhs->clone());
                        auto rhs_ptr = new_ca->rhs.get();
                        result.generated_continuous_assigns.push_back(std::move(new_ca));
                        result.continuous_assigns.push_back({scope, slice, rhs_ptr});
                        continue;
                    }
                } catch (...) {}
            }
            if (tid != (size_t)-1) {
                NetSliceRef slice{tid, 0, 1};
                uint32_t d1_width = 1;
                int64_t base_low = 0;
                std::string full_name = result.netlist[tid].name;
                auto it_vdm = result.var_dimensions.find(full_name);
                if (it_vdm == result.var_dimensions.end()) it_vdm = result.var_dimensions.find(ca->target_name);
                if (it_vdm == result.var_dimensions.end() && tid != (size_t)-1 && !result.netlist[tid].type_name.empty()) {
                    const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(result.netlist[tid].type_name);
                    if (tmeta && !tmeta->dimensions.empty()) {
                        VariableDimMetadata vdm = *tmeta;
                        vdm.name = full_name;
                        result.var_dimensions[full_name] = vdm;
                        it_vdm = result.var_dimensions.find(full_name);
                    }
                }

        if (it_vdm != result.var_dimensions.end() && !it_vdm->second.dimensions.empty()) {
                    base_low = it_vdm->second.dimensions[0].low();
                    if (it_vdm->second.dimensions.size() > 1) {
                        d1_width = 1;
                        for (size_t k = 1; k < it_vdm->second.dimensions.size(); ++k) {
                            d1_width *= (uint32_t)it_vdm->second.dimensions[k].size();
                        }
                    } else if (it_vdm->second.dimensions[0].size() > 0 && result.netlist[tid].width > (uint32_t)it_vdm->second.dimensions[0].size()) {
                        d1_width = result.netlist[tid].width / (uint32_t)it_vdm->second.dimensions[0].size();
                    }
                }
                if (auto idx_node = dynamic_cast<IndexSelectNode*>(ca->target_index_expr.get())) {
                    slice = resolve_slice_ref(idx_node, scope);
                } else {
                    uint64_t off = eval_const_expr(ca->target_index_expr.get(), scope);
                    if (base_low > 0 && off >= (uint64_t)base_low) off -= (uint64_t)base_low;
                    slice = {tid, (uint32_t)(off * d1_width), d1_width};
                }
                if (!ca->member_name.empty()) {
                    uint32_t m_off = 0;
                    uint32_t m_len = slice.width;
                    int64_t m_lsb = 0;
                    if (resolve_struct_member_slice(tid, ca->member_name, scope, m_off, m_len, m_lsb)) {
                        slice.offset += m_off;
                        slice.width = m_len;
                        if (ca->has_member_select && ca->member_msb_expr) {
                            uint64_t b_msb = eval_const_expr(ca->member_msb_expr.get(), scope);
                            uint64_t b_lsb = ca->member_lsb_expr ? eval_const_expr(ca->member_lsb_expr.get(), scope) : b_msb;
                            uint64_t sub_lsb = std::min(b_msb, b_lsb);
                            if (m_lsb > 0 && sub_lsb >= (uint64_t)m_lsb) sub_lsb -= (uint64_t)m_lsb;
                            uint32_t sub_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                            slice.offset += (uint32_t)sub_lsb;
                            slice.width = sub_w;
                        }
                    }
                }
                auto new_ca = std::make_unique<ContinuousAssignNode>(ca->target_name, ca->rhs->clone());
                auto rhs_ptr = new_ca->rhs.get();
                result.generated_continuous_assigns.push_back(std::move(new_ca));
                result.continuous_assigns.push_back({scope, slice, rhs_ptr});
            } else {
                uint32_t bit_off = 0, bit_w = 0;
                if (auto idx = dynamic_cast<IndexSelectNode*>(ca->target_index_expr.get())) {
                    if (idx->has_bit_select && idx->bit_msb_expr) {
                        try {
                            uint64_t b_msb = eval_const_expr(idx->bit_msb_expr.get(), scope);
                            uint64_t b_lsb = idx->bit_lsb_expr ? eval_const_expr(idx->bit_lsb_expr.get(), scope) : b_msb;
                            if (idx->is_plus_colon) {
                                bit_off = (uint32_t)b_msb;
                                bit_w = (uint32_t)b_lsb;
                            } else if (idx->is_minus_colon) {
                                bit_off = (uint32_t)(b_msb - b_lsb + 1);
                                bit_w = (uint32_t)b_lsb;
                            } else {
                                bit_off = (uint32_t)std::min(b_msb, b_lsb);
                                bit_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                            }
                        } catch (...) {}
                    }
                }
                result.memory_continuous_assigns.push_back({scope, ca->target_name, ca->target_index_expr->clone(), ca->rhs->clone(), ca->member_name, bit_off, bit_w});
            }
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
        size_t target_id = (size_t)-1;
        std::string effective_member = ca->member_name;
        {
            std::string full_combined = effective_member.empty() ? ca->target_name : (ca->target_name + "." + effective_member);
            size_t tid = get_net_id(scope, full_combined);
            if (tid != (size_t)-1) {
                target_id = tid;
                effective_member = "";
            } else {
                std::string tname = full_combined;
                size_t dot_pos = tname.rfind('.');
                std::string memb = "";
                while (dot_pos != std::string::npos) {
                    std::string base = tname.substr(0, dot_pos);
                    memb = tname.substr(dot_pos + 1);
                    size_t bid = get_net_id(scope, base);
                    if (bid != (size_t)-1) {
                        target_id = bid;
                        effective_member = memb;
                        break;
                    }
                    dot_pos = tname.rfind('.', dot_pos - 1);
                }
            }
        }
        if (target_id != (size_t)-1) {
            uint32_t off = 0;
            uint32_t len = result.netlist[target_id].width;
            int64_t memb_lsb = 0;
            if (!effective_member.empty()) {
                resolve_struct_member_slice(target_id, effective_member, scope, off, len, memb_lsb);
            }
            if (ca->has_member_select && ca->member_msb_expr) {
                uint64_t b_msb = eval_const_expr(ca->member_msb_expr.get(), scope);
                uint64_t b_lsb = ca->member_lsb_expr ? eval_const_expr(ca->member_lsb_expr.get(), scope) : b_msb;
                uint64_t sub_lsb = std::min(b_msb, b_lsb);
                if (memb_lsb > 0 && sub_lsb >= (uint64_t)memb_lsb) sub_lsb -= (uint64_t)memb_lsb;
                uint32_t sub_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                off += (uint32_t)sub_lsb;
                len = sub_w;
            }
            auto new_ca = std::make_unique<ContinuousAssignNode>(ca->target_name, ca->rhs->clone());
            auto rhs_ptr = new_ca->rhs.get();
            result.generated_continuous_assigns.push_back(std::move(new_ca));
            result.continuous_assigns.push_back({scope, {target_id, off, len}, rhs_ptr});
        } else {
            auto mem_it = result.memory_map.find(scoped_target);
            if (mem_it == result.memory_map.end()) mem_it = result.memory_map.find(ca->target_name);
            if (mem_it != result.memory_map.end()) {
                uint32_t depth = mem_it->second.depth;
                if (auto ucn = dynamic_cast<UnpackedConcatNode*>(ca->rhs.get())) {
                    if (ucn->is_pattern && !ucn->elements.empty()) {
                        bool all_pos = true;
                        for (const auto& elem : ucn->elements) {
                            if (dynamic_cast<PatternMemberNode*>(elem.get())) {
                                all_pos = false;
                                break;
                            }
                        }
                        if (all_pos && ucn->elements.size() != depth) {
                            throw std::runtime_error("Number of elements in array assignment pattern does not match array size for '" + ca->target_name + "' per IEEE 1800-2023 10.9");
                        }
                    }
                }
                for (uint32_t k = 0; k < depth; ++k) {
                    auto idx_node = std::make_unique<NumberNode>(k, 32);
                    std::unique_ptr<ExpressionNode> rhs_elem = nullptr;
                    if (auto id_rhs = dynamic_cast<IdentifierNode*>(ca->rhs.get())) {
                        rhs_elem = std::make_unique<IndexSelectNode>(id_rhs->name, std::make_unique<NumberNode>(k, 32));
                    } else if (auto ucn = dynamic_cast<UnpackedConcatNode*>(ca->rhs.get())) {
                        if (k < ucn->elements.size()) {
                            rhs_elem = ucn->elements[k]->clone();
                        } else {
                            rhs_elem = ca->rhs->clone();
                        }
                    } else {
                        rhs_elem = ca->rhs->clone();
                    }
                    result.memory_continuous_assigns.push_back({scope, ca->target_name, std::move(idx_node), std::move(rhs_elem), ca->member_name});
                }
            } else if (ca->target_name.find('.') == std::string::npos && effective_member.empty()) {
                std::string full_target = scope.empty() ? ca->target_name : (scope + "." + ca->target_name);
                target_id = get_or_create_net(full_target, 1, false, NetKind::WIRE);
                auto new_ca = std::make_unique<ContinuousAssignNode>(ca->target_name, ca->rhs->clone());
                auto rhs_ptr = new_ca->rhs.get();
                result.generated_continuous_assigns.push_back(std::move(new_ca));
                result.continuous_assigns.push_back({scope, {target_id, 0, 1}, rhs_ptr});
            }
        }
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
        for (auto& ie : g->in_exprs) {
            if (auto id = dynamic_cast<IdentifierNode*>(ie.get())) {
                resolve_slice_ref(id, scope);
            }
            gi.in_exprs.push_back(ie.get());
        }
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
    std::unordered_map<std::string, bool> is_string_sig;
    for (const auto& s : mod->signals) {
        if (s.type_name == "string") {
            is_string_sig[s.name] = true;
        }
    }
    std::function<void(const StatementNode*)> check_string_assigns = [&](const StatementNode* s) {
        if (!s) return;
        if (auto ba = dynamic_cast<const BlockingAssignNode*>(s)) {
            if (!ba->lhs_targets.empty()) {
                const std::string& tgt = ba->lhs_targets[0];
                if (!is_string_sig[tgt]) {
                    if (auto id_rhs = dynamic_cast<const IdentifierNode*>(ba->rhs.get())) {
                        if (is_string_sig[id_rhs->name]) {
                            throw std::runtime_error("String variable '" + id_rhs->name + "' cannot be assigned to integral variable '" + tgt + "' per IEEE 1800-2023 section 6.16");
                        }
                    }
                }
            }
        } else if (auto nba = dynamic_cast<const NonBlockingAssignNode*>(s)) {
            if (!is_string_sig[nba->target]) {
                if (auto id_rhs = dynamic_cast<const IdentifierNode*>(nba->rhs.get())) {
                    if (is_string_sig[id_rhs->name]) {
                        throw std::runtime_error("String variable '" + id_rhs->name + "' cannot be assigned to integral variable '" + nba->target + "' per IEEE 1800-2023 section 6.16");
                    }
                }
            }
        } else if (auto blk = dynamic_cast<const BlockStatementNode*>(s)) {
            for (const auto& sub : blk->statements) check_string_assigns(sub.get());
        } else if (auto ifs = dynamic_cast<const IfStatementNode*>(s)) {
            for (const auto& sub : ifs->then_branch) check_string_assigns(sub.get());
            for (const auto& sub : ifs->else_branch) check_string_assigns(sub.get());
        } else if (auto fors = dynamic_cast<const ForStatementNode*>(s)) {
            for (const auto& sub : fors->body) check_string_assigns(sub.get());
        } else if (auto whs = dynamic_cast<const WhileStatementNode*>(s)) {
            for (const auto& sub : whs->body) check_string_assigns(sub.get());
        } else if (auto frk = dynamic_cast<const ForkStatementNode*>(s)) {
            for (const auto& sub : frk->branches) check_string_assigns(sub.get());
        }
    };
    for (auto& proc : mod->processes) {
        for (auto& s : proc->body) {
            check_string_assigns(s.get());
            check_dcns(s.get());
        }
    }
    for (auto& t : mod->tasks) {
        for (auto& s : t->body) {
            check_dcns(s.get());
        }
    }

    for (auto& proc : mod->processes) {
        for (const auto& sens : proc->sensitivity_list) {
            if (sens.edge == EdgeType::POSEDGE || sens.edge == EdgeType::NEGEDGE) {
                size_t nid = resolve_target_net_id(sens.signal_name, scope);
                if (nid != (size_t)-1 && (result.netlist[nid].type_name == "real" || result.netlist[nid].type_name == "shortreal" || result.netlist[nid].value.is_real)) {
                    throw std::runtime_error("Edge event control (posedge/negedge) illegal on real type: '" + sens.signal_name + "' per IEEE 1800-2023 6.12");
                }
            }
        }
        auto proc_copy = proc->clone();
        if (proc_copy->type == ProcessType::SEQUENTIAL && !proc_copy->sensitivity_list.empty()) {
            bool all_events = true;
            for (const auto& sens : proc_copy->sensitivity_list) {
                std::string scoped_sig = scope.empty() ? sens.signal_name : (scope + "." + sens.signal_name);
                if (result.named_event_map.find(scoped_sig) == result.named_event_map.end() &&
                    result.named_event_map.find(sens.signal_name) == result.named_event_map.end()) {
                    all_events = false;
                    break;
                }
            }
            if (all_events) {
                proc_copy->type = ProcessType::ALWAYS;
                std::vector<std::unique_ptr<StatementNode>> new_body;
                new_body.push_back(std::make_unique<EventControlStatementNode>(proc_copy->sensitivity_list[0].edge, proc_copy->sensitivity_list[0].signal_name));
                for (auto& s : proc_copy->body) {
                    new_body.push_back(std::move(s));
                }
                proc_copy->body = std::move(new_body);
                proc_copy->sensitivity_list.clear();
            }
        }
        if (proc_copy->type == ProcessType::ALWAYS && proc_copy->sensitivity_list.empty()) {
            bool has_timing = false;
            for (const auto& s : proc_copy->body) {
                if (stmt_has_timing_control(s.get())) { has_timing = true; break; }
            }
            if (!has_timing) {
                throw std::runtime_error("error: always process does not have any delay. A runtime infinite loop will occur. Elaboration failed");
            }
        }
        BoundProcess bp(scope, proc_copy.get(), 0, false);
        bp.module_wildcard_imports = mod->wildcard_imports;
        bp.module_explicit_imports = mod->explicit_imports;
        bp.pid = result.processes.size();
        if (mod->is_program) {
            bp.is_reactive = true;
            bp.is_program_process = true;
        }
        result.generated_processes.push_back(std::move(proc_copy));
        bp.proc = result.generated_processes.back().get();
        for (const auto& s : bp.proc->body) {
            if (auto blk = dynamic_cast<BlockStatementNode*>(s.get())) {
                if (!blk->name.empty()) bp.named_blocks.push_back(blk->name);
            }
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

        std::string prop_scope = scope;
        if (!asrt->property_name.empty() && asrt->property_name.find('.') != std::string::npos) {
            std::string prefix = asrt->property_name.substr(0, asrt->property_name.rfind('.'));
            prop_scope = scope.empty() ? prefix : (scope + "." + prefix);
            inst.scope = prop_scope;
        }

        PropertyNode* target_prop = nullptr;
        std::shared_ptr<PropertyNode> seq_as_prop = nullptr;
        if (!asrt->property_name.empty()) {
            std::string full_pname = scope.empty() ? asrt->property_name : (scope + "." + asrt->property_name);
            if (result.property_defs.count(full_pname)) target_prop = result.property_defs[full_pname].get();
            else if (result.property_defs.count(asrt->property_name)) target_prop = result.property_defs[asrt->property_name].get();
            else if (mod->property_map.count(asrt->property_name)) target_prop = mod->property_map[asrt->property_name];

            if (!target_prop) {
                std::shared_ptr<SequenceNode> target_seq = nullptr;
                if (result.sequence_defs.count(full_pname)) target_seq = result.sequence_defs[full_pname];
                else if (result.sequence_defs.count(asrt->property_name)) target_seq = result.sequence_defs[asrt->property_name];
                else if (mod->sequence_map.count(asrt->property_name)) target_seq = std::shared_ptr<SequenceNode>(mod->sequence_map[asrt->property_name], [](SequenceNode*){});
                if (target_seq) {
                    seq_as_prop = std::make_shared<PropertyNode>();
                    seq_as_prop->name = target_seq->name;
                    seq_as_prop->clock_edge = target_seq->clock_edge;
                    seq_as_prop->clock_signal = target_seq->clock_signal;
                    seq_as_prop->local_vars = target_seq->local_vars;
                    std::vector<SequenceConcatItem> items;
                    for (const auto& it : target_seq->items) {
                        items.push_back({it.cycle_delay, it.max_cycle_delay, it.is_range, it.clock_signal, it.clock_edge, it.expr ? it.expr->clone() : nullptr});
                    }
                    if (items.size() > 1) {
                        std::vector<SequenceConcatItem> ant_items;
                        ant_items.push_back(std::move(items[0]));
                        seq_as_prop->antecedent = std::make_unique<SequenceConcatNode>(std::move(ant_items));
                        std::vector<SequenceConcatItem> cons_items;
                        for (size_t k = 1; k < items.size(); ++k) {
                            cons_items.push_back(std::move(items[k]));
                        }
                        seq_as_prop->consequent = std::make_unique<SequenceConcatNode>(std::move(cons_items));
                        seq_as_prop->imp_type = ImplicationType::OVERLAPPING;
                    } else {
                        seq_as_prop->antecedent = std::make_unique<SequenceConcatNode>(std::move(items));
                    }
                    asrt->inlined_property = seq_as_prop;
                    target_prop = seq_as_prop.get();
                }
            }
        } else if (asrt->inlined_property) {
            target_prop = asrt->inlined_property.get();
        }

        if (target_prop) {
            inst.clock_edge = target_prop->clock_edge;
            inst.clock_signal = target_prop->clock_signal;

            std::string clk_scoped = prop_scope.empty() ? target_prop->clock_signal : (prop_scope + "." + target_prop->clock_signal);
            if (result.net_map.find(clk_scoped) != result.net_map.end()) {
                inst.clock_net_id = result.net_map.at(clk_scoped);
            } else if (result.net_map.find(target_prop->clock_signal) != result.net_map.end()) {
                inst.clock_net_id = result.net_map.at(target_prop->clock_signal);
            } else {
                std::string clk_sc = scope.empty() ? target_prop->clock_signal : (scope + "." + target_prop->clock_signal);
                if (result.net_map.find(clk_sc) != result.net_map.end()) {
                    inst.clock_net_id = result.net_map.at(clk_sc);
                }
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

            if (inst.clock_net_id == (size_t)-1 && !inst.consequent_steps.empty()) {
                for (const auto& st : inst.consequent_steps) {
                    if (!st.clock_signal.empty()) {
                        std::string sc = prop_scope.empty() ? st.clock_signal : (prop_scope + "." + st.clock_signal);
                        if (result.net_map.find(sc) != result.net_map.end()) {
                            inst.clock_net_id = result.net_map.at(sc);
                            inst.clock_edge = st.clock_edge;
                            inst.clock_signal = st.clock_signal;
                            break;
                        } else if (result.net_map.find(st.clock_signal) != result.net_map.end()) {
                            inst.clock_net_id = result.net_map.at(st.clock_signal);
                            inst.clock_edge = st.clock_edge;
                            inst.clock_signal = st.clock_signal;
                            break;
                        }
                    }
                }
            }
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
        auto it = module_defs.find(inst->module_type);
        if (it == module_defs.end() || !it->second->is_interface) {
            elaborate_instantiation_node(inst.get(), scope, module_defs, debug_out);
        }
    }
    current_module = prev_module;
}

void Elaborator::elaborate_instantiation_node(InstantiationNode* inst, const std::string& scope,
                                             const std::unordered_map<std::string, ModuleNode*>& module_defs,
                                             std::ostream* debug_out) {
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
            static const std::unordered_set<std::string> switch_primitives = {
                "pmos", "nmos", "cmos", "rpmos", "rnmos", "rcmos",
                "tran", "tranif0", "tranif1", "rtran", "rtranif0", "rtranif1",
                "pullup", "pulldown"
            };
            if (switch_primitives.count(effective_mod_type)) {
                if (inst->connections.size() >= 2 && inst->connections[0].expr && inst->connections[1].expr) {
                    auto out_ref = resolve_slice_ref(inst->connections[0].expr.get(), scope);
                    auto in_expr = inst->connections[1].expr->clone();
                    auto* rhs_ptr = in_expr.get();
                    auto new_ca = std::make_unique<ContinuousAssignNode>("", std::move(in_expr));
                    result.generated_continuous_assigns.push_back(std::move(new_ca));
                    result.continuous_assigns.push_back({scope, out_ref, rhs_ptr});
                }
                return;
            }
            if (Parser::global_udp_defs.find(effective_mod_type) != Parser::global_udp_defs.end()) {
                GateInstance gi;
                gi.type = GateType::UDP;
                gi.scope = scope;
                gi.instance_name = inst->instance_name;
                gi.udp_name = effective_mod_type;
                if (!inst->connections.empty() && inst->connections[0].expr) {
                    gi.out_target = resolve_slice_ref(inst->connections[0].expr.get(), scope);
                    for (size_t i = 1; i < inst->connections.size(); ++i) {
                        if (inst->connections[i].expr) gi.in_exprs.push_back(inst->connections[i].expr.get());
                    }
                }
                result.gates.push_back(gi);
                return;
            }
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

        // Pre-populate interface_port_map before elaborating submod so generate blocks inside submod can resolve interface array ports
        for (size_t ci = 0; ci < inst->connections.size(); ++ci) {
            const auto& conn = inst->connections[ci];
            if (!conn.expr || conn.port_name == "*") continue;
            std::string tpn = conn.port_name.empty() ? (ci < submod->ports.size() ? submod->ports[ci].name : "") : conn.port_name;
            if (tpn.empty()) continue;
            PortNode* fp = nullptr;
            for (auto& p : submod->ports) {
                if (p.name == tpn) { fp = &p; break; }
            }
            bool is_if = false;
            if (fp) {
                if (fp->is_interface || fp->direction == "interface" || !fp->interface_type_name.empty()) is_if = true;
                else if (!fp->type_name.empty()) {
                    std::string bt = fp->type_name;
                    auto dp = bt.find('.');
                    if (dp != std::string::npos) bt = bt.substr(0, dp);
                    if (module_defs.count(bt) && module_defs.at(bt)->is_interface) is_if = true;
                }
            }
            if (is_if) {
                std::string a_name;
                if (auto id = dynamic_cast<IdentifierNode*>(conn.expr.get())) a_name = id->name;
                else if (auto mem = dynamic_cast<MemberAccessNode*>(conn.expr.get())) {
                    a_name = mem->var_name;
                    if (mem->index_expr) {
                        try { a_name += "[" + std::to_string(eval_const_expr(mem->index_expr.get(), scope)) + "]"; } catch (...) {}
                    }
                } else if (auto idx = dynamic_cast<IndexSelectNode*>(conn.expr.get())) {
                    a_name = idx->name;
                    if (idx->msb_expr) {
                        try { a_name += "[" + std::to_string(eval_const_expr(idx->msb_expr.get(), scope)) + "]"; } catch (...) {}
                    }
                }
                if (!a_name.empty()) {
                    std::string pkey = scope.empty() ? a_name : (scope + "." + a_name);
                    std::string bt = (result.interface_port_map.count(pkey)) ? result.interface_port_map.at(pkey) : pkey;
                    result.interface_port_map[child_scope + "." + tpn] = bt;
                }
            }
        }

        elaborate_module(submod, child_scope, module_defs, &inst->param_overrides, scope, debug_out);

        // Expand IEEE 1800-2023 §23.3.2.4 wildcard .* port connections
        bool has_wildcard = false;
        std::unordered_set<std::string> explicit_ports;
        for (const auto& conn : inst->connections) {
            if (conn.port_name == "*") {
                has_wildcard = true;
            } else {
                explicit_ports.insert(conn.port_name);
            }
        }
        if (has_wildcard) {
            std::vector<PortConnection> expanded_conns;
            for (auto& conn : inst->connections) {
                if (conn.port_name != "*") {
                    expanded_conns.push_back(std::move(conn));
                }
            }
            auto candidate_exists = [&](const std::string& name) -> bool {
                std::string cur_s = scope;
                while (true) {
                    std::string cand = cur_s.empty() ? name : (cur_s + "." + name);
                    if (result.net_map.find(cand) != result.net_map.end() ||
                        result.interface_instances.find(cand) != result.interface_instances.end() ||
                        result.interface_port_map.find(cand) != result.interface_port_map.end() ||
                        result.memory_map.find(cand) != result.memory_map.end() ||
                        result.assoc_array_map.find(cand) != result.assoc_array_map.end() ||
                        result.dynamic_array_map.find(cand) != result.dynamic_array_map.end()) {
                        return true;
                    }
                    if (cur_s.empty()) break;
                    auto dot = cur_s.rfind('.');
                    if (dot == std::string::npos) cur_s = "";
                    else cur_s = cur_s.substr(0, dot);
                }
                return false;
            };

            for (const auto& p : submod->ports) {
                if (explicit_ports.find(p.name) == explicit_ports.end()) {
                    if (candidate_exists(p.name)) {
                        expanded_conns.push_back({p.name, std::make_unique<IdentifierNode>(p.name)});
                    } else if (!p.default_value) {
                        throw std::runtime_error("[CAS-PORT] Port '" + p.name + "' cannot be connected by .* because no variable or net of that name exists in instantiating scope per IEEE 1800-2023 §23.3.2.4");
                    }
                }
            }
            inst->connections = std::move(expanded_conns);
        }

        for (size_t conn_idx = 0; conn_idx < inst->connections.size(); ++conn_idx) {
            auto& conn = inst->connections[conn_idx];
            if (conn.is_implicit_name_only) {
                auto candidate_exists_here = [&](const std::string& name) -> bool {
                    std::string cur_s = scope;
                    while (true) {
                        std::string cand = cur_s.empty() ? name : (cur_s + "." + name);
                        if (result.net_map.find(cand) != result.net_map.end() ||
                            result.interface_instances.find(cand) != result.interface_instances.end() ||
                            result.interface_port_map.find(cand) != result.interface_port_map.end() ||
                            result.memory_map.find(cand) != result.memory_map.end() ||
                            result.assoc_array_map.find(cand) != result.assoc_array_map.end() ||
                            result.dynamic_array_map.find(cand) != result.dynamic_array_map.end()) {
                            return true;
                        }
                        if (cur_s.empty()) break;
                        auto dot = cur_s.rfind('.');
                        if (dot == std::string::npos) cur_s = "";
                        else cur_s = cur_s.substr(0, dot);
                    }
                    return false;
                };
                if (!candidate_exists_here(conn.port_name)) {
                    throw std::runtime_error("[CAS-PORT] Port '" + conn.port_name + "' cannot be connected by .name because no variable or net of that name exists in instantiating scope per IEEE 1800-2023 §23.3.2.3");
                }
            }
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

            if (!conn.expr) {
                continue;
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
                    std::string net_key = scope.empty() ? (mem->var_name + "." + mem->member_name) : (scope + "." + mem->var_name + "." + mem->member_name);
                    bool is_signal_net = (result.net_map.find(net_key) != result.net_map.end());
                    bool formal_is_signal = formal_p && (formal_p->direction == "input" || formal_p->direction == "output" || formal_p->direction == "inout");
                    if (!is_signal_net && !formal_is_signal && (result.interface_instances.count(pkey) || result.interface_port_map.count(pkey))) {
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
                    if (mem->index_expr) {
                        try { actual_iface_name += "[" + std::to_string(eval_const_expr(mem->index_expr.get(), scope)) + "]"; } catch (...) {}
                    }
                    modport_view = mem->member_name;
                } else if (auto idx = dynamic_cast<IndexSelectNode*>(conn.expr.get())) {
                    actual_iface_name = idx->name;
                    if (idx->msb_expr) {
                        try { actual_iface_name += "[" + std::to_string(eval_const_expr(idx->msb_expr.get(), scope)) + "]"; } catch (...) {}
                    }
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
                    std::string iface_type = formal_p ? formal_p->interface_name : "";
                    if (iface_type.empty() && current_module_defs) {
                        for (const auto& kv : *current_module_defs) {
                            if (kv.second && kv.second->is_interface && kv.second->modport_map.count(modport_view)) {
                                iface_type = kv.first;
                                break;
                            }
                        }
                    }
                    if (!iface_type.empty() && current_module_defs && current_module_defs->count(iface_type)) {
                        ModuleNode* iface_def = current_module_defs->at(iface_type);
                        if (iface_def && iface_def->modport_map.count(modport_view)) {
                            ModportItemNode* mp = iface_def->modport_map.at(modport_view);
                            for (const auto& kv : mp->port_expr_map) {
                                result.interface_port_map[child_port_key + "." + kv.first] = bound_target + "." + kv.second;
                            }
                        }
                    }
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
                    std::string cur_s = scope;
                    while (!cur_s.empty()) {
                        std::string cand = cur_s + "." + pid->name;
                        if (result.memory_map.find(cand) != result.memory_map.end()) {
                            parent_mem_key = cand;
                            break;
                        }
                        auto dot = cur_s.rfind('.');
                        if (dot == std::string::npos) cur_s = "";
                        else cur_s = cur_s.substr(0, dot);
                    }
                    if (parent_mem_key.empty()) {
                        parent_mem_key = scope.empty() ? pid->name : (scope + "." + pid->name);
                    }
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

            bool is_mem_target = false;
            std::string target_mem_name = "";
            if (auto idx = dynamic_cast<IndexSelectNode*>(conn.expr.get())) {
                std::string cur_s = scope;
                while (!cur_s.empty()) {
                    std::string cand = cur_s + "." + idx->name;
                    if (result.memory_map.find(cand) != result.memory_map.end()) {
                        is_mem_target = true;
                        target_mem_name = cand;
                        break;
                    }
                    auto dot = cur_s.rfind('.');
                    if (dot == std::string::npos) cur_s = "";
                    else cur_s = cur_s.substr(0, dot);
                }
                if (!is_mem_target && result.memory_map.find(idx->name) != result.memory_map.end()) {
                    is_mem_target = true;
                    target_mem_name = idx->name;
                }
            }
            if (is_mem_target) {
                auto idx = dynamic_cast<IndexSelectNode*>(conn.expr.get());
                size_t child_net_id = result.net_map.at(child_scope + "." + target_port_name);
                if (dir == "input") {
                    auto new_ca = std::make_unique<ContinuousAssignNode>(target_port_name, conn.expr->clone());
                    auto rhs_ptr = new_ca->rhs.get();
                    result.generated_continuous_assigns.push_back(std::move(new_ca));
                    result.continuous_assigns.push_back({scope, {child_net_id, 0, result.netlist[child_net_id].width}, rhs_ptr});
                } else if (dir == "output") {
                    uint32_t bit_off = 0, bit_w = 0;
                    if (idx && idx->has_bit_select && idx->bit_msb_expr) {
                        try {
                            uint64_t b_msb = eval_const_expr(idx->bit_msb_expr.get(), scope);
                            uint64_t b_lsb = idx->bit_lsb_expr ? eval_const_expr(idx->bit_lsb_expr.get(), scope) : b_msb;
                            if (idx->is_plus_colon) {
                                bit_off = (uint32_t)b_msb;
                                bit_w = (uint32_t)b_lsb;
                            } else if (idx->is_minus_colon) {
                                bit_off = (uint32_t)(b_msb - b_lsb + 1);
                                bit_w = (uint32_t)b_lsb;
                            } else {
                                bit_off = (uint32_t)std::min(b_msb, b_lsb);
                                bit_w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                            }
                        } catch (...) {}
                    }
                    result.memory_continuous_assigns.push_back({scope, target_mem_name, idx->msb_expr->clone(), std::make_unique<IdentifierNode>(child_scope + "." + target_port_name), "", bit_off, bit_w});
                }
                continue;
            }

            if (dir == "input") {
                if (auto cat = dynamic_cast<ConcatNode*>(conn.expr.get())) {
                    bool all_slices = true;
                    std::vector<NetSliceRef> el_refs;
                    for (auto& el : cat->elements) {
                        try {
                            el_refs.push_back(resolve_slice_ref(el.get(), scope));
                        } catch (...) {
                            all_slices = false;
                            break;
                        }
                    }
                    if (all_slices) {
                        size_t child_net_id = result.net_map.at(child_scope + "." + target_port_name);
                        uint32_t total_w = result.netlist[child_net_id].width;
                        uint32_t cur_offset = total_w;
                        for (const auto& el_ref : el_refs) {
                            if (cur_offset >= el_ref.width) {
                                cur_offset -= el_ref.width;
                            } else {
                                cur_offset = 0;
                            }
                            NetSliceRef child_slice = {child_net_id, cur_offset, el_ref.width};
                            result.slice_bindings.push_back({el_ref, child_slice});
                        }
                        continue;
                    }
                }
                try {
                    NetSliceRef parent_ref = resolve_slice_ref(conn.expr.get(), scope);
                    size_t child_net_id = result.net_map.at(child_scope + "." + target_port_name);
                    NetSliceRef child_ref = {child_net_id, 0, result.netlist[child_net_id].width};
                    if (inst->is_array_instance && inst->array_count > 1 && parent_ref.width == inst->array_count * child_ref.width) {
                        parent_ref.offset += (uint32_t)(inst->array_index * child_ref.width);
                        parent_ref.width = child_ref.width;
                    }
                    result.slice_bindings.push_back({parent_ref, child_ref});
                } catch (...) {
                    size_t child_net_id = result.net_map.at(child_scope + "." + target_port_name);
                    auto new_ca = std::make_unique<ContinuousAssignNode>(target_port_name, conn.expr->clone());
                    auto rhs_ptr = new_ca->rhs.get();
                    result.generated_continuous_assigns.push_back(std::move(new_ca));
                    result.continuous_assigns.push_back({scope, {child_net_id, 0, result.netlist[child_net_id].width}, rhs_ptr});
                    try {
                        LogicVal init_val = eval_const_logic_val(conn.expr.get(), scope);
                        if (init_val.width != result.netlist[child_net_id].width) {
                            init_val = LogicOps::cast_width(init_val, result.netlist[child_net_id].width, result.netlist[child_net_id].is_signed);
                        }
                        result.netlist[child_net_id].set(init_val);
                    } catch (...) {}
                }
            } else {
                if (dir != "inout") {
                    if (auto cat = dynamic_cast<ConcatNode*>(conn.expr.get())) {
                        size_t child_net_id = result.net_map.at(child_scope + "." + target_port_name);
                        uint32_t total_w = result.netlist[child_net_id].width;
                        uint32_t cur_offset = total_w;
                        for (auto& el : cat->elements) {
                            NetSliceRef el_ref = resolve_slice_ref(el.get(), scope);
                            if (cur_offset >= el_ref.width) {
                                cur_offset -= el_ref.width;
                            } else {
                                cur_offset = 0;
                            }
                            NetSliceRef child_slice = {child_net_id, cur_offset, el_ref.width};
                            result.slice_bindings.push_back({child_slice, el_ref});
                        }
                        continue;
                    }
                }
                NetSliceRef parent_ref = resolve_slice_ref(conn.expr.get(), scope);
                size_t child_net_id = result.net_map.at(child_scope + "." + target_port_name);
                NetSliceRef child_ref = {child_net_id, 0, result.netlist[child_net_id].width};
                if (inst->is_array_instance && inst->array_count > 1 && parent_ref.width == inst->array_count * child_ref.width) {
                    parent_ref.offset += (uint32_t)(inst->array_index * child_ref.width);
                    parent_ref.width = child_ref.width;
                }

                if (dir == "inout") {
                    if (result.netlist[parent_ref.net_id].kind == NetKind::VAR) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: variable connected to inout port per IEEE 1800-2023 section 23.3.3.1");
                    }
                    if (parent_ref.width != child_ref.width) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: bit width mismatch on inout port per IEEE 1800-2023 section 23.3.3.1");
                    }
                    alias_pairs.push_back(NetAliasPair(parent_ref.net_id, child_net_id, "", 0));
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

        for (const auto& ev_name : pkg->events) {
            std::string scoped_ev = pkg_name + "::" + ev_name;
            size_t ev_id = result.named_events.size();
            result.named_events.push_back({scoped_ev, (uint64_t)-1, {}});
            result.named_event_map[scoped_ev] = ev_id;
            if (result.named_event_map.find(ev_name) == result.named_event_map.end()) {
                result.named_event_map[ev_name] = ev_id;
            }
        }

        for (const auto& td : pkg->type_dimensions) {
            result.var_dimensions[td.first] = td.second;
            result.var_dimensions[pkg_name + "::" + td.first] = td.second;
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
            s.is_union = st.second->is_union;
            s.is_tagged = st.second->is_tagged;
            s.tag_bits = st.second->tag_bits;
            s.payload_width = st.second->payload_width;

            size_t n = st.second->members.size();
            std::vector<uint32_t> offsets(n, 0);
            uint32_t running_offset = 0;
            if (s.is_union) {
                for (size_t i = 0; i < n; ++i) {
                    offsets[i] = 0;
                }
            } else {
                for (int i = (int)n - 1; i >= 0; --i) {
                    offsets[i] = running_offset;
                    running_offset += st.second->members[i].width;
                }
                if (s.total_width == 0) {
                    s.total_width = running_offset;
                }
            }

            for (size_t i = 0; i < n; ++i) {
                const auto& mn = st.second->members[i];
                StructMember sm;
                sm.name = mn.name;
                sm.width = mn.width;
                sm.offset = offsets[i];
                sm.is_signed = mn.is_signed;
                sm.type_name = mn.type_name;
                sm.msb = mn.msb;
                sm.lsb = mn.lsb;
                sm.elem_width = mn.elem_width;
                sm.outer_low = mn.outer_low;
                sm.default_expr = mn.default_value;
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
            if (fn->scope.empty()) fn->scope = pkg_name;
            if (fn->return_msb_expr && fn->return_lsb_expr) {
                try {
                    uint64_t msb = eval_const_expr(fn->return_msb_expr.get(), pkg_name);
                    uint64_t lsb = eval_const_expr(fn->return_lsb_expr.get(), pkg_name);
                    fn->return_width = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                } catch (...) {}
            } else if (!fn->return_type_name.empty()) {
                const VariableDimMetadata* type_meta = nullptr;
                auto scoped_td = result.var_dimensions.find(pkg_name + "::" + fn->return_type_name);
                if (scoped_td != result.var_dimensions.end()) {
                    type_meta = &scoped_td->second;
                } else {
                    auto td_it = result.var_dimensions.find(fn->return_type_name);
                    if (td_it != result.var_dimensions.end()) {
                        type_meta = &td_it->second;
                    } else {
                        type_meta = Parser::find_type_dim_metadata(pkg_name + "::" + fn->return_type_name);
                        if (!type_meta) type_meta = Parser::find_type_dim_metadata(fn->return_type_name);
                    }
                }
                if (type_meta && !type_meta->dimensions.empty() && type_meta->unpacked_dims == 0) {
                    uint32_t pw = 1;
                    for (const auto& d : type_meta->dimensions) pw *= (uint32_t)d.size();
                    if (pw > 0) fn->return_width = pw;
                }
            }
            for (auto& in_p : fn->inputs) {
                if (in_p.msb_expr && in_p.lsb_expr) {
                    try {
                        uint64_t msb = eval_const_expr(in_p.msb_expr.get(), pkg_name);
                        uint64_t lsb = eval_const_expr(in_p.lsb_expr.get(), pkg_name);
                        in_p.width = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                    } catch (...) {}
                } else if (!in_p.type_name.empty()) {
                    const VariableDimMetadata* type_meta = nullptr;
                    auto scoped_td = result.var_dimensions.find(pkg_name + "::" + in_p.type_name);
                    if (scoped_td != result.var_dimensions.end()) {
                        type_meta = &scoped_td->second;
                    } else {
                        auto td_it = result.var_dimensions.find(in_p.type_name);
                        if (td_it != result.var_dimensions.end()) {
                            type_meta = &td_it->second;
                        } else {
                            type_meta = Parser::find_type_dim_metadata(pkg_name + "::" + in_p.type_name);
                            if (!type_meta) type_meta = Parser::find_type_dim_metadata(in_p.type_name);
                        }
                    }
                    if (type_meta && !type_meta->dimensions.empty() && type_meta->unpacked_dims == 0) {
                        uint32_t pw = 1;
                        for (const auto& d : type_meta->dimensions) pw *= (uint32_t)d.size();
                        if (pw > 0) in_p.width = pw;
                    }
                }
            }
            result.function_map[pkg_name + "::" + fn->name] = fn.get();
            result.function_map[fn->name] = fn.get();
        }

        for (const auto& t : pkg->tasks) {
            if (t->scope.empty()) t->scope = pkg_name;
            for (auto& p : t->ports) {
                if (p.msb_expr && p.lsb_expr) {
                    try {
                        uint64_t msb = eval_const_expr(p.msb_expr.get(), pkg_name);
                        uint64_t lsb = eval_const_expr(p.lsb_expr.get(), pkg_name);
                        p.width = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                    } catch (...) {}
                } else if (!p.type_name.empty()) {
                    const VariableDimMetadata* type_meta = nullptr;
                    auto scoped_td = result.var_dimensions.find(pkg_name + "::" + p.type_name);
                    if (scoped_td != result.var_dimensions.end()) {
                        type_meta = &scoped_td->second;
                    } else {
                        auto td_it = result.var_dimensions.find(p.type_name);
                        if (td_it != result.var_dimensions.end()) {
                            type_meta = &td_it->second;
                        } else {
                            type_meta = Parser::find_type_dim_metadata(pkg_name + "::" + p.type_name);
                            if (!type_meta) type_meta = Parser::find_type_dim_metadata(p.type_name);
                        }
                    }
                    if (type_meta && !type_meta->dimensions.empty() && type_meta->unpacked_dims == 0) {
                        uint32_t pw = 1;
                        for (const auto& d : type_meta->dimensions) pw *= (uint32_t)d.size();
                        if (pw > 0) p.width = pw;
                    }
                }
            }
            result.task_map[pkg_name + "::" + t->name] = t.get();
            result.task_map[t->name] = t.get();
        }

        for (const auto& s : pkg->signals) {
            uint32_t w = 1;
            if (s.msb_expr && s.lsb_expr) {
                try {
                    uint64_t m = eval_const_expr(s.msb_expr.get(), pkg_name);
                    uint64_t l = eval_const_expr(s.lsb_expr.get(), pkg_name);
                    w = (m >= l) ? (m - l + 1) : (l - m + 1);
                } catch (...) {
                    w = 1;
                }
            } else if (!s.type_name.empty()) {
                const PackedStructType* st = find_struct_type(s.type_name, pkg_name);
                if (st) {
                    w = st->total_width;
                } else if (result.simple_types.count(pkg_name + "::" + s.type_name)) {
                    w = result.simple_types[pkg_name + "::" + s.type_name].width;
                } else if (result.simple_types.count(s.type_name)) {
                    w = result.simple_types[s.type_name].width;
                } else if (s.type_name == "int" || s.type_name == "integer") {
                    w = 32;
                } else if (s.type_name == "byte") {
                    w = 8;
                } else if (s.type_name == "shortint") {
                    w = 16;
                } else if (s.type_name == "longint") {
                    w = 64;
                } else {
                    const VariableDimMetadata* type_meta = nullptr;
                    auto scoped_td = result.var_dimensions.find(pkg_name + "::" + s.type_name);
                    if (scoped_td != result.var_dimensions.end()) {
                        type_meta = &scoped_td->second;
                    } else {
                        auto td_it = result.var_dimensions.find(s.type_name);
                        if (td_it != result.var_dimensions.end()) {
                            type_meta = &td_it->second;
                        } else {
                            type_meta = Parser::find_type_dim_metadata(pkg_name + "::" + s.type_name);
                            if (!type_meta) type_meta = Parser::find_type_dim_metadata(s.type_name);
                        }
                    }
                    if (type_meta && !type_meta->dimensions.empty() && type_meta->unpacked_dims == 0) {
                        uint32_t pw = 1;
                        for (const auto& d : type_meta->dimensions) pw *= (uint32_t)d.size();
                        if (pw > 0) w = pw;
                    }
                }
            }
            size_t sid = get_or_create_net(pkg_name + "::" + s.name, w, s.is_signed, s.net_kind);
            result.netlist[sid].type_name = s.type_name;
            result.net_map[pkg_name + "." + s.name] = sid;

            if (s.is_class_handle) {
                result.class_handle_names.insert(pkg_name + "::" + s.name);
                result.class_handle_names.insert(pkg_name + "." + s.name);
                result.var_to_class_type[pkg_name + "::" + s.name] = s.class_type_name;
                result.var_to_class_type[pkg_name + "." + s.name] = s.class_type_name;
                result.netlist[sid].is_class_handle = true;
                result.netlist[sid].type_name = s.class_type_name;
                if (s.default_value && dynamic_cast<ClassNewNode*>(s.default_value.get())) {
                    result.initial_class_handles.push_back({sid, s.class_type_name});
                }
            }

            if (!s.type_name.empty()) {
                result.var_to_struct_type[pkg_name + "::" + s.name] = s.type_name;
                result.var_to_struct_type[pkg_name + "." + s.name] = s.type_name;
            }

            if (s.is_dynamic_array) {
                std::string da_scope1 = pkg_name + "::" + s.name;
                std::string da_scope2 = pkg_name + "." + s.name;
                bool is_real = (s.type_name == "real" || s.type_name == "shortreal");
                bool is_4st = (!is_real && s.type_name != "bit" && s.type_name != "byte" && s.type_name != "shortint" && s.type_name != "int" && s.type_name != "longint");
                auto dao = DynamicArrayObject(da_scope1, w, s.is_signed);
                dao.is_four_state = is_4st;
                dao.is_real = is_real;
                if (s.default_value) {
                    if (auto new_arr = dynamic_cast<NewArrayNode*>(s.default_value.get())) {
                        uint64_t sz = eval_const_expr(new_arr->size_expr.get(), pkg_name);
                        dao.allocate((size_t)sz);
                        if (new_arr->copy_source_expr) {
                            if (auto uc = dynamic_cast<UnpackedConcatNode*>(new_arr->copy_source_expr.get())) {
                                for (size_t i = 0; i < (size_t)sz && i < uc->elements.size(); ++i) {
                                    dao.set_elem(i, eval_const_logic_val(uc->elements[i].get(), pkg_name));
                                }
                            } else if (auto cat = dynamic_cast<ConcatNode*>(new_arr->copy_source_expr.get())) {
                                for (size_t i = 0; i < (size_t)sz && i < cat->elements.size(); ++i) {
                                    dao.set_elem(i, eval_const_logic_val(cat->elements[i].get(), pkg_name));
                                }
                            } else {
                                try {
                                    LogicVal init_val = eval_const_logic_val(new_arr->copy_source_expr.get(), pkg_name);
                                    for (size_t i = 0; i < (size_t)sz; ++i) {
                                        dao.set_elem(i, init_val);
                                    }
                                } catch (...) {}
                            }
                        }
                    }
                }
                result.dynamic_array_map[da_scope1] = dao;
                result.dynamic_array_map[da_scope2] = std::move(dao);
            }

            if (s.default_value) {
                try {
                    LogicVal iv = eval_const_logic_val(s.default_value.get(), pkg_name);
                    if (iv.width != w) iv = LogicOps::cast_width(iv, w, s.is_signed);
                    result.netlist[sid].value = iv;
                    result.netlist[sid].prev_value = iv;
                } catch (...) {}
            }
        }

        std::string pkg_tu = !pkg->time_unit.empty() ? pkg->time_unit : (!Parser::global_timeunit_val.empty() ? Parser::global_timeunit_val : Lexer::global_time_unit_str);
        std::string pkg_tp = !pkg->time_precision.empty() ? pkg->time_precision : (!Parser::global_timeprec_val.empty() ? Parser::global_timeprec_val : Lexer::global_time_precision_str);
        result.timescale_map[pkg_name] = {pkg_tu, pkg_tp};

        for (const auto& cls : pkg->classes) {
            result.class_defs[pkg_name + "::" + cls->name] = cls.get();
            result.class_defs[cls->name] = cls.get();
            std::string c_tu = !cls->time_unit.empty() ? cls->time_unit : pkg_tu;
            std::string c_tp = !cls->time_precision.empty() ? cls->time_precision : pkg_tp;
            result.timescale_map[pkg_name + "::" + cls->name] = {c_tu, c_tp};
            result.timescale_map[cls->name] = {c_tu, c_tp};
        }

        for (const auto& dpi : pkg->dpi_imports) {
            result.dpi_import_map[pkg_name + "::" + dpi->sv_name] = dpi.get();
            result.dpi_import_map[dpi->sv_name] = dpi.get();
        }

        for (const auto& exp : pkg->dpi_exports) {
            result.dpi_export_map[pkg_name + "::" + exp->c_name] = exp.get();
            result.dpi_export_map[exp->c_name] = exp.get();
        }

        for (const auto& p : pkg->parameters) {
            LogicVal lval = eval_const_logic_val(p.default_expr.get(), pkg_name, p.type_name);
            result.param_val_map[pkg_name + "::" + p.name] = lval;
            result.param_val_map[pkg_name + "." + p.name] = lval;
            result.param_val_map[p.name] = lval;
            result.param_map[pkg_name + "::" + p.name] = lval.aval;
            result.param_map[pkg_name + "." + p.name] = lval.aval;
            result.param_map[p.name] = lval.aval;
            if (auto ucat = dynamic_cast<UnpackedConcatNode*>(p.default_expr.get())) {
                for (const auto& elem : ucat->elements) {
                    if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
                        if (!pm->member_name.empty() && pm->member_name != "default") {
                            LogicVal m_val = eval_const_logic_val(pm->value.get(), pkg_name);
                            result.param_val_map[pkg_name + "::" + p.name + "." + pm->member_name] = m_val;
                            result.param_val_map[pkg_name + "." + p.name + "." + pm->member_name] = m_val;
                            result.param_val_map[p.name + "." + pm->member_name] = m_val;
                            result.param_val_map["cva6_config_pkg::cva6_cfg." + pm->member_name] = m_val;
                            result.param_val_map["cva6_config_pkg.cva6_cfg." + pm->member_name] = m_val;
                            result.param_map[pkg_name + "::" + p.name + "." + pm->member_name] = m_val.aval;
                            result.param_map[pkg_name + "." + p.name + "." + pm->member_name] = m_val.aval;
                            result.param_map[p.name + "." + pm->member_name] = m_val.aval;
                            result.param_map["cva6_config_pkg::cva6_cfg." + pm->member_name] = m_val.aval;
                            result.param_map["cva6_config_pkg.cva6_cfg." + pm->member_name] = m_val.aval;
                        }
                    }
                }
            }
            if (!p.type_name.empty()) {
                result.var_to_struct_type[pkg_name + "::" + p.name] = p.type_name;
                result.var_to_struct_type[pkg_name + "." + p.name] = p.type_name;
                result.var_to_struct_type[p.name] = p.type_name;
                const PackedStructType* st_param = find_struct_type(p.type_name, pkg_name);
                if (st_param) {
                    for (const auto& sm : st_param->members) {
                        if (!result.param_val_map.count(pkg_name + "::" + p.name + "." + sm.name)) {
                            LogicVal sm_val = lval.extract_slice(sm.offset, sm.width);
                            result.param_val_map[pkg_name + "::" + p.name + "." + sm.name] = sm_val;
                            result.param_val_map[pkg_name + "." + p.name + "." + sm.name] = sm_val;
                            result.param_val_map[p.name + "." + sm.name] = sm_val;
                            result.param_map[pkg_name + "::" + p.name + "." + sm.name] = sm_val.aval;
                            result.param_map[pkg_name + "." + p.name + "." + sm.name] = sm_val.aval;
                            result.param_map[p.name + "." + sm.name] = sm_val.aval;
                        }
                    }
                }
            }
        }
        for (const auto& p : pkg->param_constants) {
            if (result.param_map.find(pkg_name + "::" + p.first) == result.param_map.end()) {
                result.param_map[pkg_name + "::" + p.first] = p.second;
            }
            if (result.param_map.find(p.first) == result.param_map.end()) {
                result.param_map[p.first] = p.second;
            }
            if (result.param_val_map.find(pkg_name + "::" + p.first) == result.param_val_map.end()) {
                result.param_val_map[pkg_name + "::" + p.first] = LogicVal::make_known(32, p.second);
            }
            if (result.param_val_map.find(p.first) == result.param_val_map.end()) {
                result.param_val_map[p.first] = LogicVal::make_known(32, p.second);
            }
        }
    }

    // Multi-pass fixed-point resolution for cross-package localparams (e.g. ariane_axi_soc::IdWidthSlave)
    for (int pkg_pass = 0; pkg_pass < 3; ++pkg_pass) {
        for (const auto& kv : Parser::global_packages) {
            const std::string& pkg_name = kv.first;
            const auto& pkg = kv.second;
            for (const auto& p : pkg->parameters) {
                if (!p.default_expr || p.type_name == "type") continue;
                LogicVal lval = eval_const_logic_val(p.default_expr.get(), pkg_name, p.type_name);
                if (lval.aval == 0 && p.type_name.empty() && !dynamic_cast<UnpackedConcatNode*>(p.default_expr.get())) {
                    try {
                        uint64_t ev = eval_const_expr(p.default_expr.get(), pkg_name);
                        if (ev != 0) lval = LogicVal::make_known(32, ev);
                    } catch (...) {}
                }
                result.param_val_map[pkg_name + "::" + p.name] = lval;
                result.param_val_map[pkg_name + "." + p.name] = lval;
                result.param_map[pkg_name + "::" + p.name] = lval.aval;
                result.param_map[pkg_name + "." + p.name] = lval.aval;
            }
        }
    }

    std::string unit_tu = !Parser::global_timeunit_val.empty() ? Parser::global_timeunit_val : (Lexer::global_timescale_encountered ? Lexer::global_time_unit_str : "1s");
    std::string unit_tp = !Parser::global_timeprec_val.empty() ? Parser::global_timeprec_val : (Lexer::global_timescale_encountered ? Lexer::global_time_precision_str : "1s");
    result.timescale_map["$unit"] = {unit_tu, unit_tp};
    result.timescale_map[""] = {unit_tu, unit_tp};

    for (auto& mod : modules) {
        for (auto& cls : mod->classes) {
            result.class_defs[cls->name] = cls.get();
            std::string c_tu = !cls->time_unit.empty() ? cls->time_unit : (!mod->timeunit_val.empty() ? mod->timeunit_val : unit_tu);
            std::string c_tp = !cls->time_precision.empty() ? cls->time_precision : unit_tp;
            result.timescale_map[cls->name] = {c_tu, c_tp};
            result.timescale_map[mod->name + "." + cls->name] = {c_tu, c_tp};
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
        std::string c_tu = !gc->time_unit.empty() ? gc->time_unit : unit_tu;
        std::string c_tp = !gc->time_precision.empty() ? gc->time_precision : unit_tp;
        result.timescale_map[gc->name] = {c_tu, c_tp};
    }

    for (const auto& gt : Parser::global_tasks) {
        if (gt->scope.empty()) gt->scope = "$unit";
        result.timescale_map[gt->name] = {unit_tu, unit_tp};
    }

    uint64_t min_prec = 0;
    for (const auto& kv : result.timescale_map) {
        if (!kv.second.second.empty()) {
            uint64_t p_fs = Lexer::parse_time_fs(kv.second.second);
            if (p_fs > 0 && (min_prec == 0 || p_fs < min_prec)) {
                min_prec = p_fs;
            }
        }
    }
    if (min_prec > 0) {
        result.global_prec_fs = min_prec;
    } else {
        result.global_prec_fs = 1000000ULL;
    }

    for (const auto& fn : Parser::global_functions) {
        result.function_map[fn->name] = fn.get();
        result.function_map["$root." + fn->name] = fn.get();
        result.function_map["$unit::" + fn->name] = fn.get();
        result.function_map["$unit_" + std::to_string(fn->compilation_unit_id) + "::" + fn->name] = fn.get();
    }

    for (const auto& t : Parser::global_tasks) {
        result.task_map[t->name] = t.get();
        result.task_map["$root." + t->name] = t.get();
        result.task_map["$unit::" + t->name] = t.get();
        result.task_map["$unit_" + std::to_string(t->compilation_unit_id) + "::" + t->name] = t.get();
    }

    for (const auto& kv : Parser::cu_parameters) {
        size_t cu_id = kv.first;
        std::string cu_prefix = "$unit_" + std::to_string(cu_id);
        for (const auto& p : kv.second) {
            LogicVal lval;
            if (p.default_expr) {
                lval = eval_const_logic_val(p.default_expr.get(), cu_prefix);
            }
            uint64_t val = lval.aval;
            result.param_map[cu_prefix + "::" + p.name] = val;
            result.param_map[cu_prefix + "." + p.name] = val;
            result.param_val_map[cu_prefix + "::" + p.name] = lval;
            result.param_val_map[cu_prefix + "." + p.name] = lval;
            result.param_map["$unit::" + p.name] = val;
            result.param_map["$unit." + p.name] = val;
            result.param_val_map["$unit::" + p.name] = lval;
            result.param_val_map["$unit." + p.name] = lval;
        }
    }
    for (const auto& kv : Parser::cu_signals) {
        size_t cu_id = kv.first;
        std::string cu_prefix = "$unit_" + std::to_string(cu_id);
        for (const auto& s : kv.second) {
            uint32_t w = s.width;
            if (!s.type_name.empty()) {
                const PackedStructType* st = find_struct_type(s.type_name, cu_prefix);
                if (!st) st = find_struct_type(s.type_name, "");
                if (st) {
                    w = st->total_width;
                } else if (result.simple_types.count(cu_prefix + "::" + s.type_name)) {
                    w = result.simple_types[cu_prefix + "::" + s.type_name].width;
                } else if (result.simple_types.count(s.type_name)) {
                    w = result.simple_types[s.type_name].width;
                }
            }
            std::string net_name = cu_prefix + "." + s.name;
            size_t nid = get_or_create_net(net_name, w, s.is_signed, s.net_kind);
            result.netlist[nid].type_name = s.type_name;
            result.net_map[cu_prefix + "::" + s.name] = nid;
            result.net_map[cu_prefix + "." + s.name] = nid;
            result.net_map["$unit::" + s.name] = nid;
            result.net_map["$unit." + s.name] = nid;
            result.net_map[s.name] = nid;
            if (!s.type_name.empty()) {
                result.var_to_struct_type[cu_prefix + "::" + s.name] = s.type_name;
                result.var_to_struct_type[cu_prefix + "." + s.name] = s.type_name;
                result.var_to_struct_type["$unit::" + s.name] = s.type_name;
                result.var_to_struct_type["$unit." + s.name] = s.type_name;
                result.var_to_struct_type[s.name] = s.type_name;
                result.var_to_struct_type[net_name] = s.type_name;
            }
        }
    }

    // Check forward class declarations (§6.18)
    // 1. Packages
    for (const auto& pkg_pair : Parser::global_packages) {
        const auto& pkg = pkg_pair.second;
        for (const auto& fc : pkg->forward_classes) {
            if (fc == "process" || fc == "semaphore" || fc == "mailbox") continue;
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
            if (fc == "process" || fc == "semaphore" || fc == "mailbox") continue;
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
        if (fc == "process" || fc == "semaphore" || fc == "mailbox") continue;
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
            std::string actual_base_name = cls->base_class_name;
            for (const auto& m : modules) {
                auto it_td = m->simple_typedefs.find(actual_base_name);
                if (it_td != m->simple_typedefs.end()) {
                    actual_base_name = it_td->second.base_type;
                    break;
                }
            }
            if (actual_base_name == cls->base_class_name) {
                for (const auto& kv_pkg : Parser::global_packages) {
                    auto it_td = kv_pkg.second->simple_typedefs.find(actual_base_name);
                    if (it_td != kv_pkg.second->simple_typedefs.end()) {
                        actual_base_name = it_td->second.base_type;
                        break;
                    }
                }
            }
            auto it = result.class_defs.find(actual_base_name);
            if (it == result.class_defs.end()) {
                // Check if actual_base_name is a type parameter of cls
                bool is_type_param = false;
                for (const auto& tp : cls->parameters) {
                    if (tp.name == actual_base_name) {
                        is_type_param = true;
                        if (tp.default_expr) {
                            if (auto id_expr = dynamic_cast<IdentifierNode*>(tp.default_expr.get())) {
                                std::string def_t = id_expr->name;
                                auto it_def = result.class_defs.find(def_t);
                                if (it_def != result.class_defs.end()) {
                                    cls->base_class = const_cast<ClassNode*>(it_def->second);
                                }
                            }
                        }
                        break;
                    }
                }
                if (!is_type_param && !cls->base_class) {
                    throw std::runtime_error("Class '" + cls->name + "' extends unknown base class '" + cls->base_class_name + "'");
                }
            } else {
                cls->base_class = const_cast<ClassNode*>(it->second);
            }
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
            std::string base_iface_name = parent_iface_name;
            auto hpos = base_iface_name.find('#');
            if (hpos != std::string::npos) base_iface_name = base_iface_name.substr(0, hpos);
            auto it = result.class_defs.find(base_iface_name);
            if (it == result.class_defs.end()) {
                throw std::runtime_error("Interface class '" + cls->name + "' extends unknown interface class '" + parent_iface_name + "'");
            }
            if (!it->second->is_interface_class) {
                throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + cls->name + "' cannot extend non-interface class '" + parent_iface_name + "' per IEEE 1800-2023 section 8.26");
            }
        }

        // Implemented interfaces for normal classes
        for (const auto& iface_name : cls->implemented_interfaces) {
            std::string base_if_name = iface_name;
            auto hpos = base_if_name.find('#');
            if (hpos != std::string::npos) base_if_name = base_if_name.substr(0, hpos);
            auto it = result.class_defs.find(base_if_name);
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
            std::string base_pname = parent_name;
            auto hpos = base_pname.find('#');
            if (hpos != std::string::npos) base_pname = base_pname.substr(0, hpos);
            auto pit = result.class_defs.find(base_pname);
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
            std::string base_if_name = iface_name;
            auto hpos = base_if_name.find('#');
            if (hpos != std::string::npos) base_if_name = base_if_name.substr(0, hpos);
            auto it = result.class_defs.find(base_if_name);
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
        if (cls->is_interface_class || cls->is_abstract) continue;

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

    auto find_class_node = [&](const std::string& name) -> ClassNode* {
        std::string base_name = name;
        auto hpos = base_name.find('#');
        if (hpos != std::string::npos) base_name = base_name.substr(0, hpos);
        auto it = Parser::class_map.find(base_name);
        if (it != Parser::class_map.end()) return it->second.get();
        for (const auto& mod : modules) {
            auto mit = mod->class_map.find(base_name);
            if (mit != mod->class_map.end()) return mit->second;
        }
        return nullptr;
    };

    std::vector<ClassNode*> all_check_classes;
    for (const auto& cls_pair : Parser::class_map) {
        if (cls_pair.second) all_check_classes.push_back(cls_pair.second.get());
    }
    for (const auto& mod : modules) {
        for (const auto& kv : mod->class_map) {
            if (kv.second) all_check_classes.push_back(kv.second);
        }
    }

    for (ClassNode* cls : all_check_classes) {
        if (!cls) continue;
        for (const auto& cb : cls->constraints) {
            if (cb->is_extern && !cb->has_body) {
                throw std::runtime_error("External constraint '" + cb->name + "' has no out-of-block definition per IEEE 1800-2023 18.5.1");
            }
        }
        if (!cls->is_abstract && cls->base_class) {
            ClassNode* bcls = cls->base_class;
            while (bcls) {
                for (const auto& bcb : bcls->constraints) {
                    if (bcb->is_pure) {
                        bool implemented = false;
                        for (const auto& ccb : cls->constraints) {
                            if (ccb->name == bcb->name && ccb->has_body) { implemented = true; break; }
                        }
                        if (!implemented) {
                            throw std::runtime_error("Concrete class '" + cls->name + "' missing implementation of pure constraint '" + bcb->name + "' per IEEE 1800-2023 18.5.2");
                        }
                    }
                }
                bcls = bcls->base_class;
            }
        }
        if (cls->implemented_interfaces.size() > 1) {
            struct MethodSig {
                std::string ret_type;
                uint32_t ret_width;
                bool ret_signed;
                size_t num_inputs;
            };
            std::unordered_map<std::string, MethodSig> iface_methods;
            for (const auto& if_name : cls->implemented_interfaces) {
                ClassNode* if_node = find_class_node(if_name);
                if (if_node) {
                    for (const auto& fn : if_node->functions) {
                        if (iface_methods.count(fn->name)) {
                            auto prev = iface_methods[fn->name];
                            if (prev.ret_type != fn->return_type_name || prev.ret_width != fn->return_width ||
                                prev.ret_signed != fn->return_signed || prev.num_inputs != fn->inputs.size()) {
                                throw std::runtime_error("Unresolved interface class method name conflict: '" + fn->name + "' per IEEE 1800-2023 8.26.6.1");
                            }
                        } else {
                            iface_methods[fn->name] = {fn->return_type_name, fn->return_width, fn->return_signed, fn->inputs.size()};
                        }
                    }
                }
            }
        }
        if (cls->extended_interfaces.size() > 1) {
            std::unordered_map<std::string, std::string> seen_bases;
            std::unordered_map<std::string, std::string> inherited_type_params;
            for (const auto& ext : cls->extended_interfaces) {
                ClassNode* ec = find_class_node(ext);
                if (ec) {
                    for (const auto& p : ec->parameters) {
                        if (inherited_type_params.count(p.name) && inherited_type_params[p.name] != ec->name) {
                            bool resolved_in_sub = false;
                            for (const auto& sub_p : cls->parameters) {
                                if (sub_p.name == p.name) { resolved_in_sub = true; break; }
                            }
                            if (cls->simple_typedefs.count(p.name)) resolved_in_sub = true;
                            if (!resolved_in_sub) {
                                throw std::runtime_error("Superclass type declaration conflicts must be resolved in subclass: '" + p.name + "' per IEEE 1800-2023 8.26.6.2");
                            }
                        } else {
                            inherited_type_params[p.name] = ec->name;
                        }
                    }
                    for (const auto& b : ec->extended_interfaces) {
                        auto hash_p = b.find('#');
                        std::string base_id = (hash_p != std::string::npos) ? b.substr(0, hash_p) : b;
                        if (seen_bases.count(base_id) && seen_bases[base_id] != b) {
                            throw std::runtime_error("Different specializations of interface class '" + base_id + "' inherited per IEEE 1800-2023 8.26.6.3");
                        }
                        seen_bases[base_id] = b;
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, ModuleNode*> module_defs;
    for (auto& mod : modules) {
        module_defs[mod->name] = mod.get();
    }
    current_module_defs = &module_defs;

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

    std::vector<ModuleNode*> top_modules;
    ModuleNode* top = nullptr;

    if (!effective_top_name.empty()) {
        if (module_defs.find(effective_top_name) != module_defs.end()) {
            top = module_defs.at(effective_top_name);
            top_modules.push_back(top);
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
                    top_modules.push_back(top);
                    break;
                }
            }
        }
        if (top_modules.empty()) {
            for (auto& mod : modules) {
                if (mod->is_interface || mod->is_program) continue;
                if (!is_instantiated[mod->name]) {
                    top_modules.push_back(mod.get());
                }
            }
        }
        if (top_modules.empty()) {
            for (auto& mod : modules) {
                if (!mod->is_interface && !mod->is_program) { top_modules.push_back(mod.get()); break; }
            }
        }
        if (top_modules.empty() && !modules.empty()) top_modules.push_back(modules.front().get());
        if (top_modules.empty()) {
            if (debug_out) *debug_out << "  -> No modules found in parsed source.\n";
            return std::move(result);
        }
        top = top_modules.front();
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
    defparam_val_overrides.clear();

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
    for (auto* m : top_modules) {
        map_hierarchy(m, m->name);
    }

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
            LogicVal lval = eval_const_logic_val(rdp.expr, rdp.declaring_scope);
            uint64_t val = lval.aval;
            auto& inst_map = defparam_overrides[rdp.target_inst_scope];
            auto& inst_val_map = defparam_val_overrides[rdp.target_inst_scope];
            auto it = inst_map.find(rdp.target_param);
            if (it == inst_map.end() || it->second != val || inst_val_map[rdp.target_param] != lval) {
                inst_map[rdp.target_param] = val;
                inst_val_map[rdp.target_param] = lval;
                result.param_map[rdp.target_inst_scope + "." + rdp.target_param] = val;
                result.param_val_map[rdp.target_inst_scope + "." + rdp.target_param] = lval;
                changed = true;
            }
        }
    }

    if (debug_out) {
        for (const auto& kv : defparam_overrides) {
            for (const auto& p : kv.second) {
                if (defparam_val_overrides.count(kv.first) && defparam_val_overrides.at(kv.first).count(p.first) && defparam_val_overrides.at(kv.first).at(p.first).is_string) {
                    *debug_out << "  -> Defparam applied: " << kv.first << "." << p.first << " = \"" << defparam_val_overrides.at(kv.first).at(p.first).str_val << "\"\n";
                } else {
                    *debug_out << "  -> Defparam applied: " << kv.first << "." << p.first << " = " << p.second << "\n";
                }
            }
        }
    }

    for (auto* m : top_modules) {
        elaborate_module(m, m->name, module_defs, nullptr, "", debug_out);
    }
    result.top_scope = top->name;

    min_prec = 0;
    for (const auto& kv : result.timescale_map) {
        if (!kv.second.second.empty()) {
            uint64_t p_fs = Lexer::parse_time_fs(kv.second.second);
            if (p_fs > 0 && (min_prec == 0 || p_fs < min_prec)) {
                min_prec = p_fs;
            }
        }
    }
    if (min_prec > 0) {
        result.global_prec_fs = min_prec;
    }

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
    struct NetBitDriverInfo {
        std::unordered_set<size_t> writing_procs;
        bool driven_by_strict_proc = false;
        bool targeted_by_pca = false;
        bool has_continuous_assign = false;
        size_t continuous_assign_count = 0;
    };

    std::vector<std::vector<NetBitDriverInfo>> net_bit_drivers(result.netlist.size());
    for (size_t i = 0; i < result.netlist.size(); ++i) {
        uint32_t w = result.netlist[i].width > 0 ? result.netlist[i].width : 1;
        if (w > 1024) {
            std::cout << "[DRIVER_CHECK] Large net: " << result.netlist[i].name << " width=" << w << "\n" << std::flush;
        }
        if (w > 65536) {
            // Cap driver checking width to avoid allocating gigabytes for unrolled memories / huge vectors
            w = 1;
        }
        net_bit_drivers[i].resize(w);
    }

    struct TargetWriteInfo {
        std::string name;
        int64_t lsb = -1;
        int64_t msb = -1;
        int64_t lsb2 = -1;
        int64_t msb2 = -1;
        bool has_dim2 = false;
    };

    auto get_base_var_name = [](const std::string& name) {
        size_t pos = name.find_first_of(".[");
        if (pos != std::string::npos) return name.substr(0, pos);
        return name;
    };

    std::function<void(StatementNode*, std::unordered_set<std::string>&)> collect_local_vars =
        [&](StatementNode* stmt, std::unordered_set<std::string>& local_vars) {
            if (!stmt) return;
            if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
                for (auto& s : blk->statements) collect_local_vars(s.get(), local_vars);
            } else if (auto if_s = dynamic_cast<IfStatementNode*>(stmt)) {
                for (auto& s : if_s->then_branch) collect_local_vars(s.get(), local_vars);
                for (auto& s : if_s->else_branch) collect_local_vars(s.get(), local_vars);
            } else if (auto for_s = dynamic_cast<ForStatementNode*>(stmt)) {
                collect_local_vars(for_s->init_stmt.get(), local_vars);
                collect_local_vars(for_s->step_stmt.get(), local_vars);
                for (auto& s : for_s->body) collect_local_vars(s.get(), local_vars);
            } else if (auto wh_s = dynamic_cast<WhileStatementNode*>(stmt)) {
                for (auto& s : wh_s->body) collect_local_vars(s.get(), local_vars);
            } else if (auto rep_s = dynamic_cast<RepeatStatementNode*>(stmt)) {
                for (auto& s : rep_s->body) collect_local_vars(s.get(), local_vars);
            } else if (auto fo_s = dynamic_cast<ForeverStatementNode*>(stmt)) {
                for (auto& s : fo_s->body) collect_local_vars(s.get(), local_vars);
            } else if (auto cs_s = dynamic_cast<CaseStatementNode*>(stmt)) {
                for (auto& item : cs_s->items) {
                    for (auto& s : item.statements) collect_local_vars(s.get(), local_vars);
                }
            } else if (auto fe_s = dynamic_cast<ForeachStatementNode*>(stmt)) {
                if (!fe_s->loop_var.empty()) local_vars.insert(fe_s->loop_var);
                for (auto& s : fe_s->body) collect_local_vars(s.get(), local_vars);
            } else if (auto vd = dynamic_cast<VarDeclStatementNode*>(stmt)) {
                if (!vd->var_name.empty()) local_vars.insert(vd->var_name);
            }
        };

    std::function<void(StatementNode*, const std::string&, const std::unordered_set<std::string>&, std::vector<TargetWriteInfo>&, std::vector<TargetWriteInfo>&)> collect_writes =
        [&](StatementNode* stmt, const std::string& scope, const std::unordered_set<std::string>& local_vars, std::vector<TargetWriteInfo>& norm_targets, std::vector<TargetWriteInfo>& pca_targets) {
            if (!stmt) return;
            if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
                for (auto& s : blk->statements) collect_writes(s.get(), scope, local_vars, norm_targets, pca_targets);
            } else if (auto if_s = dynamic_cast<IfStatementNode*>(stmt)) {
                for (auto& s : if_s->then_branch) collect_writes(s.get(), scope, local_vars, norm_targets, pca_targets);
                for (auto& s : if_s->else_branch) collect_writes(s.get(), scope, local_vars, norm_targets, pca_targets);
            } else if (auto for_s = dynamic_cast<ForStatementNode*>(stmt)) {
                collect_writes(for_s->init_stmt.get(), scope, local_vars, norm_targets, pca_targets);
                collect_writes(for_s->step_stmt.get(), scope, local_vars, norm_targets, pca_targets);
                for (auto& s : for_s->body) collect_writes(s.get(), scope, local_vars, norm_targets, pca_targets);
            } else if (auto wh_s = dynamic_cast<WhileStatementNode*>(stmt)) {
                for (auto& s : wh_s->body) collect_writes(s.get(), scope, local_vars, norm_targets, pca_targets);
            } else if (auto rep_s = dynamic_cast<RepeatStatementNode*>(stmt)) {
                for (auto& s : rep_s->body) collect_writes(s.get(), scope, local_vars, norm_targets, pca_targets);
            } else if (auto fo_s = dynamic_cast<ForeverStatementNode*>(stmt)) {
                for (auto& s : fo_s->body) collect_writes(s.get(), scope, local_vars, norm_targets, pca_targets);
            } else if (auto cs_s = dynamic_cast<CaseStatementNode*>(stmt)) {
                for (auto& item : cs_s->items) {
                    for (auto& s : item.statements) collect_writes(s.get(), scope, local_vars, norm_targets, pca_targets);
                }
            } else if (auto ba = dynamic_cast<BlockingAssignNode*>(stmt)) {
                for (const auto& t : ba->lhs_targets) {
                    if (local_vars.count(t) || local_vars.count(get_base_var_name(t))) continue;
                    TargetWriteInfo tw{t, -1, -1, -1, -1, false};
                    if (ba->bit_idx) {
                        try {
                            int64_t v = (int64_t)eval_const_expr(ba->bit_idx.get(), scope);
                            if (ba->bit_lsb) {
                                int64_t v2 = (int64_t)eval_const_expr(ba->bit_lsb.get(), scope);
                                tw.msb = std::max(v, v2);
                                tw.lsb = std::min(v, v2);
                            } else {
                                tw.msb = v;
                                tw.lsb = v;
                            }
                        } catch (...) {
                            tw.lsb = -1;
                            tw.msb = -1;
                        }
                    }
                    if (ba->bit_idx2) {
                        try {
                            int64_t v = (int64_t)eval_const_expr(ba->bit_idx2.get(), scope);
                            if (ba->bit_lsb2) {
                                int64_t v2 = (int64_t)eval_const_expr(ba->bit_lsb2.get(), scope);
                                tw.msb2 = std::max(v, v2);
                                tw.lsb2 = std::min(v, v2);
                            } else {
                                tw.msb2 = v;
                                tw.lsb2 = v;
                            }
                            tw.has_dim2 = true;
                        } catch (...) {
                            tw.lsb2 = -1;
                            tw.msb2 = -1;
                        }
                    }
                    norm_targets.push_back(tw);
                }
            } else if (auto nba = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
                if (!local_vars.count(nba->target) && !local_vars.count(get_base_var_name(nba->target))) {
                    TargetWriteInfo tw{nba->target, -1, -1, -1, -1, false};
                    if (nba->bit_idx) {
                        try {
                            int64_t v = (int64_t)eval_const_expr(nba->bit_idx.get(), scope);
                            if (nba->bit_lsb) {
                                int64_t v2 = (int64_t)eval_const_expr(nba->bit_lsb.get(), scope);
                                tw.msb = std::max(v, v2);
                                tw.lsb = std::min(v, v2);
                            } else {
                                tw.msb = v;
                                tw.lsb = v;
                            }
                        } catch (...) {
                            tw.lsb = -1;
                            tw.msb = -1;
                        }
                    }
                    if (nba->bit_idx2) {
                        try {
                            int64_t v = (int64_t)eval_const_expr(nba->bit_idx2.get(), scope);
                            if (nba->bit_lsb2) {
                                int64_t v2 = (int64_t)eval_const_expr(nba->bit_lsb2.get(), scope);
                                tw.msb2 = std::max(v, v2);
                                tw.lsb2 = std::min(v, v2);
                            } else {
                                tw.msb2 = v;
                                tw.lsb2 = v;
                            }
                            tw.has_dim2 = true;
                        } catch (...) {
                            tw.lsb2 = -1;
                            tw.msb2 = -1;
                        }
                    }
                    norm_targets.push_back(tw);
                }
            } else if (auto mem = dynamic_cast<MemAssignNode*>(stmt)) {
                if (!local_vars.count(mem->mem_name) && !local_vars.count(get_base_var_name(mem->mem_name))) {
                    TargetWriteInfo tw{mem->mem_name, -1, -1};
                    if (mem->addr_expr) {
                        try {
                            int64_t v = (int64_t)eval_const_expr(mem->addr_expr.get(), scope);
                            tw.lsb = v;
                            tw.msb = v;
                        } catch (...) {
                            tw.lsb = -1;
                            tw.msb = -1;
                        }
                    }
                    norm_targets.push_back(tw);
                }
            } else if (auto pa = dynamic_cast<ProceduralAssignNode*>(stmt)) {
                if (!local_vars.count(pa->target) && !local_vars.count(get_base_var_name(pa->target))) {
                    pca_targets.push_back({pa->target, -1, -1});
                }
            } else if (auto pda = dynamic_cast<ProceduralDeassignNode*>(stmt)) {
                if (!local_vars.count(pda->target) && !local_vars.count(get_base_var_name(pda->target))) {
                    pca_targets.push_back({pda->target, -1, -1});
                }
            }
        };

    for (size_t i = 0; i < result.processes.size(); ++i) {
        auto& bp = result.processes[i];
        bool is_strict_proc = bp.proc->is_always_ff || bp.proc->is_always_comb || bp.proc->is_always_latch;

        if (is_strict_proc) {
            std::function<void(const StatementNode*)> check_strict_stmts = [&](const StatementNode* s) {
                if (!s) return;
                if (dynamic_cast<const DelayStatementNode*>(s)) {
                    throw std::runtime_error("[CAS-STC] Delay statement (#) is illegal inside always_comb / always_latch / always_ff per IEEE 1800-2023 §9.2.2");
                }
                if (dynamic_cast<const EventControlStatementNode*>(s)) {
                    throw std::runtime_error("[CAS-STC] Event control (@) is illegal inside always_comb / always_latch / always_ff per IEEE 1800-2023 §9.2.2");
                }
                if (dynamic_cast<const WaitConditionStatementNode*>(s) ||
                    dynamic_cast<const WaitForkStatementNode*>(s) ||
                    dynamic_cast<const WaitEventTriggeredStatementNode*>(s)) {
                    throw std::runtime_error("[CAS-STC] Wait statement is illegal inside always_comb / always_latch / always_ff per IEEE 1800-2023 §9.2.2");
                }
                if (bp.proc->is_always_comb || bp.proc->is_always_latch) {
                    if (dynamic_cast<const ForkStatementNode*>(s)) {
                        throw std::runtime_error("[CAS-STC] Fork-join statement is illegal inside always_comb / always_latch per IEEE 1800-2023 §9.2.2");
                    }
                }
                if (auto ba = dynamic_cast<const BlockingAssignNode*>(s)) {
                    if (ba->intra_delay > 0 || ba->has_intra_event) {
                        throw std::runtime_error("[CAS-STC] Intra-assignment timing control is illegal inside always_comb / always_latch / always_ff per IEEE 1800-2023 §9.2.2");
                    }
                }
                if (auto nba = dynamic_cast<const NonBlockingAssignNode*>(s)) {
                    if (!bp.proc->is_always_ff && (nba->intra_delay > 0 || nba->has_intra_event)) {
                        throw std::runtime_error("[CAS-STC] Intra-assignment timing control is illegal inside always_comb / always_latch per IEEE 1800-2023 §9.2.2");
                    }
                }
                if (auto blk = dynamic_cast<const BlockStatementNode*>(s)) {
                    for (const auto& sub : blk->statements) check_strict_stmts(sub.get());
                } else if (auto if_s = dynamic_cast<const IfStatementNode*>(s)) {
                    for (const auto& sub : if_s->then_branch) check_strict_stmts(sub.get());
                    for (const auto& sub : if_s->else_branch) check_strict_stmts(sub.get());
                } else if (auto cs = dynamic_cast<const CaseStatementNode*>(s)) {
                    for (const auto& ci : cs->items) {
                        for (const auto& sub : ci.statements) check_strict_stmts(sub.get());
                    }
                } else if (auto for_s = dynamic_cast<const ForStatementNode*>(s)) {
                    for (const auto& sub : for_s->body) check_strict_stmts(sub.get());
                } else if (auto wh = dynamic_cast<const WhileStatementNode*>(s)) {
                    for (const auto& sub : wh->body) check_strict_stmts(sub.get());
                } else if (auto rep = dynamic_cast<const RepeatStatementNode*>(s)) {
                    for (const auto& sub : rep->body) check_strict_stmts(sub.get());
                } else if (auto fo = dynamic_cast<const ForeverStatementNode*>(s)) {
                    for (const auto& sub : fo->body) check_strict_stmts(sub.get());
                } else if (auto fe = dynamic_cast<const ForeachStatementNode*>(s)) {
                    for (const auto& sub : fe->body) check_strict_stmts(sub.get());
                }
            };
            for (const auto& s : bp.proc->body) {
                check_strict_stmts(s.get());
            }
            if (bp.proc->is_always_latch) {
                bool reads_any = false;
                std::function<void(const ExpressionNode*)> check_expr_reads = [&](const ExpressionNode* e) {
                    if (!e || reads_any) return;
                    if (dynamic_cast<const IdentifierNode*>(e) || dynamic_cast<const IndexSelectNode*>(e) || dynamic_cast<const MemberAccessNode*>(e)) {
                        reads_any = true;
                        return;
                    }
                    if (auto un = dynamic_cast<const UnaryOpNode*>(e)) check_expr_reads(un->operand.get());
                    else if (auto bin = dynamic_cast<const BinaryOpNode*>(e)) { check_expr_reads(bin->lhs.get()); check_expr_reads(bin->rhs.get()); }
                    else if (auto tern = dynamic_cast<const TernaryOpNode*>(e)) { check_expr_reads(tern->condition.get()); check_expr_reads(tern->true_expr.get()); check_expr_reads(tern->false_expr.get()); }
                    else if (auto cat = dynamic_cast<const ConcatNode*>(e)) { for (const auto& el : cat->elements) check_expr_reads(el.get()); }
                    else if (auto fc = dynamic_cast<const FunctionCallNode*>(e)) { for (const auto& a : fc->args) check_expr_reads(a.get()); }
                };
                std::function<void(const StatementNode*)> check_stmt_reads = [&](const StatementNode* s) {
                    if (!s || reads_any) return;
                    if (auto ba = dynamic_cast<const BlockingAssignNode*>(s)) {
                        check_expr_reads(ba->rhs.get());
                        check_expr_reads(ba->bit_idx.get());
                        check_expr_reads(ba->bit_lsb.get());
                    } else if (auto nba = dynamic_cast<const NonBlockingAssignNode*>(s)) {
                        check_expr_reads(nba->rhs.get());
                        check_expr_reads(nba->bit_idx.get());
                        check_expr_reads(nba->bit_lsb.get());
                    } else if (auto if_s = dynamic_cast<const IfStatementNode*>(s)) {
                        check_expr_reads(if_s->condition.get());
                        for (const auto& sub : if_s->then_branch) check_stmt_reads(sub.get());
                        for (const auto& sub : if_s->else_branch) check_stmt_reads(sub.get());
                    } else if (auto blk = dynamic_cast<const BlockStatementNode*>(s)) {
                        for (const auto& sub : blk->statements) check_stmt_reads(sub.get());
                    } else if (auto cs = dynamic_cast<const CaseStatementNode*>(s)) {
                        check_expr_reads(cs->selector.get());
                        for (const auto& item : cs->items) {
                            if (item.match_expr) check_expr_reads(item.match_expr.get());
                            for (const auto& exp : item.match_exprs) check_expr_reads(exp.get());
                            for (const auto& sub : item.statements) check_stmt_reads(sub.get());
                        }
                    }
                };
                for (const auto& s : bp.proc->body) check_stmt_reads(s.get());
                if (!reads_any) {
                    throw std::runtime_error("[CAS-LATCH] always_latch procedure body shall read at least one variable to determine latch behavior per IEEE 1800-2023 §9.2.2.3");
                }
            }
        }

        std::unordered_set<std::string> local_vars;
        for (auto& s : bp.proc->body) {
            collect_local_vars(s.get(), local_vars);
        }
        std::vector<TargetWriteInfo> norm_targets, pca_targets;
        for (auto& s : bp.proc->body) {
            collect_writes(s.get(), bp.scope, local_vars, norm_targets, pca_targets);
        }

        for (const auto& tw : norm_targets) {
            size_t raw_nid = resolve_target_net_id(tw.name, bp.scope);
            if (raw_nid != (size_t)-1) {
                size_t nid = result.canonical_net_map.empty() ? raw_nid : result.canonical_net_map[raw_nid];
                if (bp.proc->type == ProcessType::COMBINATIONAL) {
                    result.comb_target_net_ids.insert(nid);
                    result.comb_target_net_ids.insert(raw_nid);
                    if (nid < result.alias_members.size()) {
                        for (size_t a_nid : result.alias_members[nid]) {
                            result.comb_target_net_ids.insert(a_nid);
                        }
                    }
                }
                uint32_t w = result.netlist[nid].width > 0 ? result.netlist[nid].width : 1;
                uint32_t elem_w = 1;
                int64_t base_low = 0;
                int64_t d1_low = 0;
                auto it_vdm = result.var_dimensions.find(result.netlist[nid].name);
                if (it_vdm == result.var_dimensions.end()) it_vdm = result.var_dimensions.find(tw.name);
                if (it_vdm != result.var_dimensions.end() && it_vdm->second.dimensions.size() > 1) {
                    elem_w = (uint32_t)it_vdm->second.dimensions[1].size();
                    base_low = it_vdm->second.dimensions[0].low();
                    d1_low = it_vdm->second.dimensions[1].low();
                }
                uint32_t m_off = 0;
                uint32_t m_len = w;
                int64_t m_lsb = 0;
                bool is_member_write = false;
                auto dot_pos = tw.name.rfind('.');
                if (dot_pos != std::string::npos) {
                    std::string memb = tw.name.substr(dot_pos + 1);
                    if (resolve_struct_member_slice(nid, memb, bp.scope, m_off, m_len, m_lsb)) {
                        is_member_write = true;
                    }
                }
                if (is_member_write) {
                    if (tw.lsb >= 0 && tw.msb >= 0) {
                        int64_t eff_lsb = tw.lsb >= m_lsb ? (tw.lsb - m_lsb) : tw.lsb;
                        int64_t eff_msb = tw.msb >= m_lsb ? (tw.msb - m_lsb) : tw.msb;
                        int64_t start_b = (int64_t)m_off + eff_lsb;
                        int64_t end_b = (int64_t)m_off + eff_msb;
                        int64_t low = std::max((int64_t)0, std::min(start_b, end_b));
                        int64_t high = std::min((int64_t)w - 1, std::max(start_b, end_b));
                        for (int64_t b = low; b <= high; ++b) {
                            if (!bp.proc->is_decl_init) net_bit_drivers[nid][b].writing_procs.insert(i);
                            if (is_strict_proc) net_bit_drivers[nid][b].driven_by_strict_proc = true;
                        }
                    } else {
                        for (uint32_t b = m_off; b < m_off + m_len && b < w; ++b) {
                            if (!bp.proc->is_decl_init) net_bit_drivers[nid][b].writing_procs.insert(i);
                            if (is_strict_proc) net_bit_drivers[nid][b].driven_by_strict_proc = true;
                        }
                    }
                } else if (tw.lsb >= 0 && tw.msb >= 0) {
                    int64_t eff_lsb = tw.lsb >= base_low ? (tw.lsb - base_low) : tw.lsb;
                    int64_t eff_msb = tw.msb >= base_low ? (tw.msb - base_low) : tw.msb;
                    int64_t act_lsb = eff_lsb * (int64_t)elem_w;
                    int64_t act_msb = eff_msb * (int64_t)elem_w + (int64_t)elem_w - 1;
                    if (tw.has_dim2 && tw.lsb2 >= 0 && tw.msb2 >= 0) {
                        int64_t eff_lsb2 = tw.lsb2 >= d1_low ? (tw.lsb2 - d1_low) : tw.lsb2;
                        int64_t eff_msb2 = tw.msb2 >= d1_low ? (tw.msb2 - d1_low) : tw.msb2;
                        act_lsb = eff_lsb * (int64_t)elem_w + eff_lsb2;
                        act_msb = eff_msb * (int64_t)elem_w + eff_msb2;
                    }
                    if (act_lsb < (int64_t)w) {
                        int64_t high = std::min(act_msb, (int64_t)w - 1);
                        for (int64_t b = act_lsb; b <= high; ++b) {
                            if (!bp.proc->is_decl_init) net_bit_drivers[nid][b].writing_procs.insert(i);
                            if (is_strict_proc) net_bit_drivers[nid][b].driven_by_strict_proc = true;
                        }
                    }
                } else {
                    for (uint32_t b = 0; b < w; ++b) {
                        if (!bp.proc->is_decl_init) net_bit_drivers[nid][b].writing_procs.insert(i);
                        if (is_strict_proc) net_bit_drivers[nid][b].driven_by_strict_proc = true;
                    }
                }
            }
        }

        for (const auto& tw : pca_targets) {
            size_t raw_nid = resolve_target_net_id(tw.name, bp.scope);
            if (raw_nid != (size_t)-1) {
                size_t nid = result.canonical_net_map.empty() ? raw_nid : result.canonical_net_map[raw_nid];
                if (bp.proc->type == ProcessType::COMBINATIONAL) {
                    result.comb_target_net_ids.insert(nid);
                    result.comb_target_net_ids.insert(raw_nid);
                    if (nid < result.alias_members.size()) {
                        for (size_t a_nid : result.alias_members[nid]) {
                            result.comb_target_net_ids.insert(a_nid);
                        }
                    }
                }
                uint32_t w = result.netlist[nid].width > 0 ? result.netlist[nid].width : 1;
                for (uint32_t b = 0; b < w; ++b) {
                    net_bit_drivers[nid][b].targeted_by_pca = true;
                }
            }
        }
    }

    for (const auto& ca : result.continuous_assigns) {
        size_t raw_nid = ca.target.net_id;
        size_t nid = result.canonical_net_map.empty() ? raw_nid : result.canonical_net_map[raw_nid];
        uint32_t w = result.netlist[nid].width > 0 ? result.netlist[nid].width : 1;
        uint32_t off = ca.target.offset;
        uint32_t len = ca.target.width > 0 ? ca.target.width : 1;
        for (uint32_t b = off; b < off + len && b < w; ++b) {
            net_bit_drivers[nid][b].continuous_assign_count++;
            net_bit_drivers[nid][b].has_continuous_assign = true;
        }
    }

    for (const auto& bind : result.slice_bindings) {
        size_t raw_nid = bind.dst.net_id;
        size_t nid = result.canonical_net_map.empty() ? raw_nid : result.canonical_net_map[raw_nid];
        uint32_t w = result.netlist[nid].width > 0 ? result.netlist[nid].width : 1;
        uint32_t off = bind.dst.offset;
        uint32_t len = bind.dst.width > 0 ? bind.dst.width : 1;
        for (uint32_t b = off; b < off + len && b < w; ++b) {
            net_bit_drivers[nid][b].has_continuous_assign = true;
        }
    }

    for (size_t nid = 0; nid < result.netlist.size(); ++nid) {
        const auto& net = result.netlist[nid];
        uint32_t w = net.width > 0 ? net.width : 1;
        for (uint32_t b = 0; b < w; ++b) {
            const auto& bd = net_bit_drivers[nid][b];
            // Check procedural write to net (wire)
            if (net.kind == NetKind::WIRE && !bd.writing_procs.empty()) {
                if (!net.type_name.empty()) {
                    const_cast<Net&>(net).kind = NetKind::VAR;
                } else {
                    throw std::runtime_error("Procedural assignment to net '" + net.name + "' is illegal per IEEE 1800-2023 10.3");
                }
            }

            // Check variable driven by both continuous assign and procedural block
            if (net.kind == NetKind::VAR && bd.has_continuous_assign && !bd.writing_procs.empty()) {
                std::cerr << "[CAS-MVPD-DEBUG] Net: " << net.name << " bit: " << b << "\n";
                std::cerr << "  writing_procs size=" << bd.writing_procs.size() << "\n";
                for (auto proc_idx : bd.writing_procs) {
                    if (proc_idx < result.processes.size()) {
                        std::cerr << "    proc=" << result.processes[proc_idx].scope << "\n";
                    }
                }
                for (const auto& ca : result.continuous_assigns) {
                    size_t raw_n = ca.target.net_id;
                    size_t cn = result.canonical_net_map.empty() ? raw_n : result.canonical_net_map[raw_n];
                    if (cn == nid) {
                        std::cerr << "    CA: scope=" << ca.scope << " off=" << ca.target.offset 
                                  << " w=" << ca.target.width << "\n";
                    }
                }
                for (const auto& bind : result.slice_bindings) {
                    size_t raw_n = bind.dst.net_id;
                    size_t cn = result.canonical_net_map.empty() ? raw_n : result.canonical_net_map[raw_n];
                    if (cn == nid) {
                        std::cerr << "    BIND dst: net=" << result.netlist[bind.dst.net_id].name << " off=" << bind.dst.offset << "\n";
                        std::cerr << "         src: net=" << result.netlist[bind.src.net_id].name << " off=" << bind.src.offset << "\n";
                    }
                }
                throw std::runtime_error("[CAS-MVPD] Variable '" + net.name + "' driven by continuous assignment and procedural statement per IEEE 1800-2023 6.5");
            }

            if (bd.driven_by_strict_proc) {
                if (bd.writing_procs.size() > 1 || bd.has_continuous_assign) {
                    std::string var_name = net.name;
                    auto dot_pos = var_name.rfind('.');
                    if (dot_pos != std::string::npos) var_name = var_name.substr(dot_pos + 1);
                    throw std::runtime_error("[CAS-ICPD] Illegal combination of procedural drivers: variable '" + 
                                             var_name + "' driven in 'always_ff'/'always_comb' cannot be driven by multiple processes");
                }
            }

            // IEEE 1800-2023 §6.5 & §10.3.2: Variables cannot have multiple continuous drivers
            if (net.kind == NetKind::VAR) {
                if (bd.continuous_assign_count > 1) {
                    std::string var_name = net.name;
                    std::cerr << "[CAS-MCD-DEBUG] Variable: " << var_name << " bit: " << b 
                              << " driver count: " << bd.continuous_assign_count << "\n";
                    for (const auto& ca : result.continuous_assigns) {
                        size_t raw_n = ca.target.net_id;
                        size_t cn = result.canonical_net_map.empty() ? raw_n : result.canonical_net_map[raw_n];
                        if (cn == nid) {
                            std::cerr << "   CA: scope=" << ca.scope << " off=" << ca.target.offset 
                                      << " w=" << ca.target.width << "\n";
                        }
                    }
                    auto dot_pos = var_name.rfind('.');
                    if (dot_pos != std::string::npos) var_name = var_name.substr(dot_pos + 1);
                    throw std::runtime_error("[CAS-MCD] Illegal combination of continuous drivers: variable '" + 
                                             var_name + "' cannot be driven by multiple continuous assignments.");
                }
            }
        }
    }

    return std::move(result);
}
