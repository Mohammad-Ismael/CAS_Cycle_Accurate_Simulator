#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include "class_types.hpp"
#include "../frontend/ast_expr.hpp"
#include "../frontend/ast_module.hpp"

struct VarDomain {
    std::string name;
    uint32_t width = 32;
    bool is_signed = false;
    bool is_rand = false;
    bool is_randc = false;
    uint64_t min_val = 0;
    uint64_t max_val = ~0ULL;
};

class ConstraintSolver {
public:
    template <typename ExprEvalFn>
    bool solve(ClassObject* obj,
               const std::vector<std::unique_ptr<ExpressionNode>>& inline_constraints,
               const std::string& scope,
               ExprEvalFn&& eval_fn) {
        if (!obj || !obj->class_def) return false;

        auto fail_contradiction = []() {
            std::cout << "[CAS-CNST-CIF] Constraints inconsistency failure: Constraints are inconsistent and cannot be solved." << std::endl;
            return false;
        };

        // 1. Collect all rand and randc properties in declaration order from base to derived
        std::vector<const ClassNode*> hierarchy;
        const ClassNode* cur = obj->class_def;
        while (cur) {
            hierarchy.push_back(cur);
            cur = cur->base_class;
        }

        std::unordered_map<std::string, VarDomain> domains;
        std::vector<std::string> rand_order;

        for (auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it) {
            for (const auto& m : (*it)->member_vars) {
                if (m.is_rand || m.is_randc) {
                    VarDomain d;
                    d.name = m.name;
                    d.width = m.width;
                    d.is_signed = m.is_signed;
                    d.is_rand = m.is_rand;
                    d.is_randc = m.is_randc;
                    d.min_val = 0;
                    d.max_val = (m.width >= 64) ? ~0ULL : ((1ULL << m.width) - 1);
                    if (domains.find(m.name) == domains.end()) {
                        rand_order.push_back(m.name);
                    }
                    domains[m.name] = d;
                }
            }
        }

        if (domains.empty()) return true;

        // 2. Collect all active constraint expressions
        std::vector<ExpressionNode*> all_constraints;
        for (auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it) {
            for (const auto& cb : (*it)->constraints) {
                for (const auto& e : cb->exprs) {
                    all_constraints.push_back(e.get());
                }
            }
        }
        for (const auto& e : inline_constraints) {
            all_constraints.push_back(e.get());
        }

        // 3. Process constraints
        auto eval_const = [&](ExpressionNode* e) -> uint64_t {
            if (auto num = dynamic_cast<NumberNode*>(e)) return num->aval;
            LogicVal lv = eval_fn(e);
            return lv.aval;
        };

        auto get_var_name = [&](ExpressionNode* e) -> std::string {
            if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                return id->name;
            }
            if (auto ma = dynamic_cast<MemberAccessNode*>(e)) {
                return ma->member_name;
            }
            return "";
        };

        for (auto* expr : all_constraints) {
            if (!expr) continue;
            if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
                std::string var_name;
                bool var_on_lhs = false;
                ExpressionNode* other_node = nullptr;

                std::string lhs_name = get_var_name(bin->lhs.get());
                if (!lhs_name.empty() && domains.find(lhs_name) != domains.end()) {
                    var_name = lhs_name;
                    var_on_lhs = true;
                    other_node = bin->rhs.get();
                } else {
                    std::string rhs_name = get_var_name(bin->rhs.get());
                    if (!rhs_name.empty() && domains.find(rhs_name) != domains.end()) {
                        var_name = rhs_name;
                        var_on_lhs = false;
                        other_node = bin->lhs.get();
                    }
                }

                if (!var_name.empty() && other_node) {
                    uint64_t cval = eval_const(other_node);
                    VarDomain& d = domains[var_name];

                    if (var_on_lhs) {
                        if (bin->op == "==" || bin->op == "===") {
                            d.min_val = std::max(d.min_val, cval);
                            d.max_val = std::min(d.max_val, cval);
                        } else if (bin->op == ">=") {
                            d.min_val = std::max(d.min_val, cval);
                        } else if (bin->op == ">") {
                            d.min_val = std::max(d.min_val, cval + 1);
                        } else if (bin->op == "<=") {
                            d.max_val = std::min(d.max_val, cval);
                        } else if (bin->op == "<") {
                            if (cval == 0) return fail_contradiction();
                            d.max_val = std::min(d.max_val, cval - 1);
                        }
                    } else {
                        // other_node op var
                        if (bin->op == "==" || bin->op == "===") {
                            d.min_val = std::max(d.min_val, cval);
                            d.max_val = std::min(d.max_val, cval);
                        } else if (bin->op == "<=") {
                            // cval <= var  =>  var >= cval
                            d.min_val = std::max(d.min_val, cval);
                        } else if (bin->op == "<") {
                            // cval < var  =>  var > cval
                            d.min_val = std::max(d.min_val, cval + 1);
                        } else if (bin->op == ">=") {
                            // cval >= var  =>  var <= cval
                            d.max_val = std::min(d.max_val, cval);
                        } else if (bin->op == ">") {
                            // cval > var  =>  var < cval
                            if (cval == 0) return fail_contradiction();
                            d.max_val = std::min(d.max_val, cval - 1);
                        }
                    }
                }
            } else if (auto inside_node = dynamic_cast<InsideNode*>(expr)) {
                std::string tname = get_var_name(inside_node->target.get());
                if (!tname.empty() && domains.find(tname) != domains.end()) {
                    VarDomain& d = domains[tname];
                    for (const auto& r : inside_node->ranges) {
                        if (r.high) {
                            uint64_t lo = eval_const(r.low.get());
                            uint64_t hi = eval_const(r.high.get());
                            d.min_val = std::max(d.min_val, lo);
                            d.max_val = std::min(d.max_val, hi);
                        } else {
                            uint64_t v = eval_const(r.low.get());
                            d.min_val = std::max(d.min_val, v);
                            d.max_val = std::min(d.max_val, v);
                        }
                    }
                }
            }
        }

        // 4. Check for contradictory intervals
        for (const auto& name : rand_order) {
            const auto& d = domains[name];
            if (d.min_val > d.max_val) {
                return fail_contradiction();
            }
        }

        // 5. Assign pseudo-random values to properties
        for (const auto& name : rand_order) {
            const auto& d = domains[name];
            uint64_t chosen = d.min_val;
            if (d.min_val < d.max_val) {
                uint64_t range_size = d.max_val - d.min_val + 1;
                uint32_t r = obj->next_random();
                chosen = d.min_val + (r % range_size);
            }
            obj->set_property(name, LogicVal(d.width, chosen, 0));
        }

        return true;
    }
};
