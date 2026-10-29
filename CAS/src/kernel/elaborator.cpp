#include "../../include/kernel/elaborator.hpp"
#include "../../include/tracer/vcd_dumper.hpp"
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
        std::string scoped_id = scope.empty() ? id->name : (scope + "." + id->name);
        if (result.param_map.find(scoped_id) != result.param_map.end()) {
            return result.param_map.at(scoped_id);
        }
        if (result.param_map.find(id->name) != result.param_map.end()) {
            return result.param_map.at(id->name);
        }
        if (result.enum_constant_map.find(scoped_id) != result.enum_constant_map.end()) {
            return result.enum_constant_map.at(scoped_id).aval;
        }
        if (result.enum_constant_map.find(id->name) != result.enum_constant_map.end()) {
            return result.enum_constant_map.at(id->name).aval;
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
        uint64_t l = eval_const_expr(bin->lhs.get(), scope);
        uint64_t r = eval_const_expr(bin->rhs.get(), scope);
        if (bin->op == "+") return l + r;
        if (bin->op == "-") return (l >= r) ? (l - r) : 0;
        if (bin->op == "*") return l * r;
        if (bin->op == "<<") return l << r;
        if (bin->op == ">>" || bin->op == ">>>") return l >> r;
    }
    return 0;
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

void Elaborator::elaborate_module(ModuleNode* mod, const std::string& scope,
                                 const std::unordered_map<std::string, ModuleNode*>& module_defs,
                                 const std::vector<ParamOverride>* overrides,
                                 const std::string& parent_scope,
                                 std::ostream* debug_out) {
    if (debug_out) {
        *debug_out << "  -> Scoping module '" << mod->name << "' as [" << scope << "]\n";
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
    }

    for (size_t i = 0; i < mod->parameters.size(); ++i) {
        const auto& p = mod->parameters[i];
        uint64_t val = 0;
        bool overridden = false;

        if (overrides) {
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
        size_t pid = get_or_create_net(scope + "." + p.name, w, p.is_signed, p.net_kind);
        if (!p.type_name.empty()) {
            result.netlist[pid].type_name = p.type_name;
            result.var_to_enum_type[scope + "." + p.name] = p.type_name;
            result.var_to_enum_type[p.name] = p.type_name;
            result.var_to_struct_type[scope + "." + p.name] = p.type_name;
            result.var_to_struct_type[p.name] = p.type_name;
        }
    }

    for (auto& s : mod->signals) {
        uint32_t w = 1;
        if (s.msb_expr && s.lsb_expr) {
            uint64_t msb = eval_const_expr(s.msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(s.lsb_expr.get(), scope);
            w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
        }
        if (s.is_dynamic_array) {
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
            uint32_t depth = 1;
            int64_t b1_val = 0;
            int64_t b2_val = 0;
            if (s.bound1_expr && s.bound2_expr) {
                b1_val = (int64_t)eval_const_expr(s.bound1_expr.get(), scope);
                b2_val = (int64_t)eval_const_expr(s.bound2_expr.get(), scope);
                depth = (b1_val >= b2_val) ? (uint32_t)(b1_val - b2_val + 1) : (uint32_t)(b2_val - b1_val + 1);
            } else if (s.bound1_expr) {
                depth = (uint32_t)eval_const_expr(s.bound1_expr.get(), scope);
                b1_val = 0;
                b2_val = depth > 0 ? (int64_t)(depth - 1) : 0;
            }
            std::string mem_scope = scope + "." + s.name;
            result.memory_map[mem_scope] = MemoryArray(mem_scope, w, depth, b1_val, b2_val);
            if (debug_out) {
                *debug_out << "     * Memory allocated: " << mem_scope 
                           << " [" << w << "-bit x " << depth << " words]\n";
            }
        } else {
            size_t sid = get_or_create_net(scope + "." + s.name, w, s.is_signed, s.net_kind);
            if (!s.type_name.empty()) {
                result.netlist[sid].type_name = s.type_name;
                result.var_to_enum_type[scope + "." + s.name] = s.type_name;
                result.var_to_enum_type[s.name] = s.type_name;
                result.var_to_struct_type[scope + "." + s.name] = s.type_name;
                result.var_to_struct_type[s.name] = s.type_name;
            }
        }
    }

    for (auto& ca : mod->continuous_assigns) {
        std::string scoped_target = scope + "." + ca->target_name;
        if (result.queue_map.find(scoped_target) != result.queue_map.end() ||
            result.queue_map.find(ca->target_name) != result.queue_map.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to queue '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 7.10.4");
        }
        if (result.dynamic_array_map.find(scoped_target) != result.dynamic_array_map.end() ||
            result.dynamic_array_map.find(ca->target_name) != result.dynamic_array_map.end()) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: continuous assignment to dynamic array '" + ca->target_name + "' is illegal per IEEE 1800-2023 section 7.5");
        }
        std::function<void(ExpressionNode*)> check_expr = [&](ExpressionNode* expr) {
            if (!expr) return;
            if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
                std::string s_id = scope + "." + id->name;
                if (result.queue_map.find(s_id) != result.queue_map.end() ||
                    result.queue_map.find(id->name) != result.queue_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: queue '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 7.10.4");
                }
                if (result.dynamic_array_map.find(s_id) != result.dynamic_array_map.end() ||
                    result.dynamic_array_map.find(id->name) != result.dynamic_array_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: dynamic array '" + id->name + "' used in continuous assignment per IEEE 1800-2023 section 7.5");
                }
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
                check_expr(idx->msb_expr.get());
                check_expr(idx->lsb_expr.get());
            } else if (auto mc = dynamic_cast<MethodCallNode*>(expr)) {
                std::string s_id = scope + "." + mc->var_name;
                if (result.queue_map.find(s_id) != result.queue_map.end() ||
                    result.queue_map.find(mc->var_name) != result.queue_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: queue method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 7.10.4");
                }
                if (result.dynamic_array_map.find(s_id) != result.dynamic_array_map.end() ||
                    result.dynamic_array_map.find(mc->var_name) != result.dynamic_array_map.end()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: dynamic array method '" + mc->var_name + "." + mc->method_name + "()' used in continuous assignment per IEEE 1800-2023 section 7.5");
                }
                for (auto& a : mc->args) check_expr(a.get());
            } else if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
                check_expr(bin->lhs.get());
                check_expr(bin->rhs.get());
            } else if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
                check_expr(un->operand.get());
            } else if (auto ter = dynamic_cast<TernaryOpNode*>(expr)) {
                check_expr(ter->condition.get());
                check_expr(ter->true_expr.get());
                check_expr(ter->false_expr.get());
            } else if (auto fc = dynamic_cast<FunctionCallNode*>(expr)) {
                for (auto& a : fc->args) check_expr(a.get());
            }
        };
        check_expr(ca->rhs.get());
        size_t target_id = result.net_map.at(scoped_target);
        result.continuous_assigns.push_back({scope, {target_id, 0, result.netlist[target_id].width}, ca->rhs.get()});
    }

    for (auto& g : mod->gates) {
        GateInstance gi;
        gi.type = g->type;
        gi.scope = scope;
        gi.out_target = resolve_slice_ref(g->out_expr.get(), scope);
        for (auto& ie : g->in_exprs) gi.in_exprs.push_back(ie.get());
        result.gates.push_back(gi);
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
            sinfo.skew = item.skew.empty() ? (item.direction == "input" ? cb_node->default_input_skew : cb_node->default_output_skew) : item.skew;

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

        if (cb_node->is_default) {
            result.default_clocking_map[scope] = cb_idx;
        }
    }

    std::function<void(StatementNode*)> check_dcns = [&](StatementNode* s) {
        if (!s) return;
        if (auto cd = dynamic_cast<CycleDelayStatementNode*>(s)) {
            if (cd->clocking_name.empty()) {
                if (mod->default_clocking_name.empty()) {
                    throw std::runtime_error("[CAS-DCNS] Default clocking not specified: Cycle delay operator requires a default clocking to be specified.");
                }
            } else {
                if (mod->clocking_block_map.find(cd->clocking_name) == mod->clocking_block_map.end()) {
                    throw std::runtime_error("[CAS-DCNS] Default clocking not specified: Cycle delay operator requires a default clocking to be specified.");
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
        result.processes.push_back({scope, proc.get(), 0, false});
    }

    for (auto& inst : mod->instantiations) {
        std::string child_scope = scope + "." + inst->instance_name;
        if (module_defs.find(inst->module_type) == module_defs.end()) {
            throw std::runtime_error("Undefined module type instantiated: " + inst->module_type);
        }
        ModuleNode* submod = module_defs.at(inst->module_type);

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
            NetSliceRef parent_ref = resolve_slice_ref(conn.expr.get(), scope);
            size_t child_net_id = result.net_map.at(child_scope + "." + target_port_name);
            NetSliceRef child_ref = {child_net_id, 0, result.netlist[child_net_id].width};

            if (dir == "input") {
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

    std::unordered_map<std::string, ModuleNode*> module_defs;
    for (auto& mod : modules) {
        module_defs[mod->name] = mod.get();
    }

    ModuleNode* top = nullptr;

    if (!top_module_name.empty()) {
        if (module_defs.find(top_module_name) != module_defs.end()) {
            top = module_defs.at(top_module_name);
            if (debug_out) *debug_out << "  -> Using explicit top module: '" << top->name << "'\n";
        } else {
            throw std::runtime_error("Elaboration error: Top module '" + top_module_name + "' was not found in parsed sources.");
        }
    } else {
        std::unordered_map<std::string, bool> is_instantiated;
        for (auto& mod : modules) {
            if (mod->is_interface) continue;
            for (auto& inst : mod->instantiations) {
                is_instantiated[inst->module_type] = true;
            }
        }
        for (auto& mod : modules) {
            if (mod->is_interface) continue;
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
                if (mod->is_interface) continue;
                if (!is_instantiated[mod->name]) {
                    top = mod.get();
                    break;
                }
            }
        }
        if (!top) {
            for (auto& mod : modules) {
                if (!mod->is_interface) { top = mod.get(); break; }
            }
        }
        if (!top) top = modules.front().get();
        if (debug_out) *debug_out << "  -> Auto-detected top-level root module: '" << top->name << "'\n";
    }

    elaborate_module(top, top->name, module_defs, nullptr, "", debug_out);

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
                                         var_name + "' written in always_ff/always_comb/always_latch cannot be driven by any other process or procedural assign.");
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
