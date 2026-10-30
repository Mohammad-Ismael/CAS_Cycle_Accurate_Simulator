#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <functional>
#include <algorithm>
#include "logic_val.hpp"
#include "../frontend/ast_expr.hpp"

struct CovergroupBin {
    std::string name;
    std::vector<InsideRange> ranges;
    uint64_t hit_count = 0;

    CovergroupBin() = default;
    CovergroupBin(std::string n, std::vector<InsideRange> r)
        : name(std::move(n)), ranges(std::move(r)), hit_count(0) {}

    bool matches(uint64_t val, const std::function<uint64_t(ExpressionNode*)>& eval_fn) const {
        for (const auto& r : ranges) {
            if (r.high) {
                uint64_t lo = eval_fn(r.low.get());
                uint64_t hi = eval_fn(r.high.get());
                if (val >= lo && val <= hi) return true;
            } else {
                uint64_t v = eval_fn(r.low.get());
                if (val == v) return true;
            }
        }
        return false;
    }
};

struct CoverpointItem {
    std::string label;
    std::unique_ptr<ExpressionNode> target_expr;
    std::vector<CovergroupBin> bins;

    CoverpointItem() = default;
    CoverpointItem(std::string lbl, std::unique_ptr<ExpressionNode> expr)
        : label(std::move(lbl)), target_expr(std::move(expr)) {}

    void add_bin(CovergroupBin b) {
        bins.push_back(std::move(b));
    }

    double get_coverage() const {
        if (bins.empty()) return 100.0;
        size_t covered = 0;
        for (const auto& b : bins) {
            if (b.hit_count > 0) covered++;
        }
        return (static_cast<double>(covered) / static_cast<double>(bins.size())) * 100.0;
    }
};

struct CovergroupNode;

struct CovergroupInstance {
    uint64_t handle_id = 0;
    std::string type_name;
    const CovergroupNode* def = nullptr;
    std::vector<CoverpointItem> coverpoints;
    std::string creation_scope = "";

    CovergroupInstance() = default;
    CovergroupInstance(uint64_t id, std::string tname, const CovergroupNode* d = nullptr, std::string sc = "")
        : handle_id(id), type_name(std::move(tname)), def(d), creation_scope(std::move(sc)) {}

    void sample(const std::function<LogicVal(ExpressionNode*, const std::string&)>& eval_fn) {
        for (auto& cp : coverpoints) {
            if (!cp.target_expr) continue;
            LogicVal val = eval_fn(cp.target_expr.get(), creation_scope);
            uint64_t aval = val.aval;
            auto const_eval = [&](ExpressionNode* e) -> uint64_t {
                if (auto num = dynamic_cast<NumberNode*>(e)) return num->aval;
                return eval_fn(e, creation_scope).aval;
            };
            for (auto& b : cp.bins) {
                if (b.matches(aval, const_eval)) {
                    b.hit_count++;
                }
            }
        }
    }

    double get_coverage() const {
        if (coverpoints.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& cp : coverpoints) {
            sum += cp.get_coverage();
        }
        return sum / static_cast<double>(coverpoints.size());
    }
};
