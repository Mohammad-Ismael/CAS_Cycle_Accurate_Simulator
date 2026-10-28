#include "../../include/kernel/elaborator.hpp"
#include "../../include/tracer/vcd_dumper.hpp"
#include <stdexcept>

size_t Elaborator::get_or_create_net(const std::string& scoped_name, uint32_t width, bool is_signed) {
    if (result.net_map.find(scoped_name) != result.net_map.end()) {
        return result.net_map[scoped_name];
    }
    size_t id = result.netlist.size();
    result.netlist.emplace_back(scoped_name, width, is_signed);
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
        get_or_create_net(scope + "." + p.name, w, p.is_signed);
    }

    for (auto& s : mod->signals) {
        uint32_t w = 1;
        if (s.msb_expr && s.lsb_expr) {
            uint64_t msb = eval_const_expr(s.msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(s.lsb_expr.get(), scope);
            w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
        }
        if (s.is_array) {
            uint32_t depth = 1;
            if (s.bound1_expr && s.bound2_expr) {
                uint64_t b1 = eval_const_expr(s.bound1_expr.get(), scope);
                uint64_t b2 = eval_const_expr(s.bound2_expr.get(), scope);
                depth = (b1 >= b2) ? (uint32_t)(b1 - b2 + 1) : (uint32_t)(b2 - b1 + 1);
            } else if (s.bound1_expr) {
                depth = (uint32_t)eval_const_expr(s.bound1_expr.get(), scope);
            }
            std::string mem_scope = scope + "." + s.name;
            result.memory_map[mem_scope] = MemoryArray(mem_scope, w, depth);
            if (debug_out) {
                *debug_out << "     * Memory allocated: " << mem_scope 
                           << " [" << w << "-bit x " << depth << " words]\n";
            }
        } else {
            get_or_create_net(scope + "." + s.name, w, s.is_signed);
        }
    }

    for (auto& ca : mod->continuous_assigns) {
        std::string scoped_target = scope + "." + ca->target_name;
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
        result.task_map[scope + "." + t->name] = t.get();
    }

    for (auto& f : mod->functions) {
        result.function_map[scope + "." + f->name] = f.get();
        result.function_map[f->name] = f.get();
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

            std::string dir = "input";
            for (auto& p : submod->ports) {
                if (p.name == target_port_name) {
                    dir = p.direction;
                    break;
                }
            }

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
            for (auto& inst : mod->instantiations) {
                is_instantiated[inst->module_type] = true;
            }
        }
        for (auto& mod : modules) {
            if (!is_instantiated[mod->name]) {
                top = mod.get();
                break;
            }
        }
        if (!top) top = modules.front().get();
        if (debug_out) *debug_out << "  -> Auto-detected top-level root module: '" << top->name << "'\n";
    }

    elaborate_module(top, top->name, module_defs, nullptr, "", debug_out);
    return std::move(result);
}