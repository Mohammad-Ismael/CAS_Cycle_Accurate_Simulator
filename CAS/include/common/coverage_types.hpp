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

struct CoverCrossBin {
    std::vector<size_t> bin_indices; // index into each participating coverpoint's bins
    uint64_t hit_count = 0;
};

struct CoverCrossItem {
    std::string label;
    std::vector<std::string> crossed_cp_names;
    std::vector<size_t> crossed_cp_indices;
    std::vector<CoverCrossBin> cross_bins;

    double get_coverage() const {
        if (cross_bins.empty()) return 100.0;
        size_t covered = 0;
        for (const auto& cb : cross_bins) {
            if (cb.hit_count > 0) covered++;
        }
        return (static_cast<double>(covered) / static_cast<double>(cross_bins.size())) * 100.0;
    }

    void generate_cartesian(const std::vector<CoverpointItem>& cps) {
        cross_bins.clear();
        if (crossed_cp_indices.empty()) return;
        for (size_t cp_idx : crossed_cp_indices) {
            if (cp_idx >= cps.size() || cps[cp_idx].bins.empty()) return;
        }

        std::vector<size_t> current_tuple(crossed_cp_indices.size(), 0);
        std::function<void(size_t)> recurse = [&](size_t depth) {
            if (depth == crossed_cp_indices.size()) {
                CoverCrossBin cb;
                cb.bin_indices = current_tuple;
                cb.hit_count = 0;
                cross_bins.push_back(cb);
                return;
            }
            size_t cp_idx = crossed_cp_indices[depth];
            size_t num_bins = cps[cp_idx].bins.size();
            for (size_t b = 0; b < num_bins; ++b) {
                current_tuple[depth] = b;
                recurse(depth + 1);
            }
        };
        recurse(0);
    }
};

struct CovergroupNode;

struct CovergroupInstance {
    uint64_t handle_id = 0;
    std::string type_name;
    const CovergroupNode* def = nullptr;
    std::vector<CoverpointItem> coverpoints;
    std::vector<CoverCrossItem> crosses;
    std::string creation_scope = "";

    CovergroupInstance() = default;
    CovergroupInstance(uint64_t id, std::string tname, const CovergroupNode* d = nullptr, std::string sc = "")
        : handle_id(id), type_name(std::move(tname)), def(d), creation_scope(std::move(sc)) {}

    void sample(const std::function<LogicVal(ExpressionNode*, const std::string&)>& eval_fn) {
        std::vector<std::vector<size_t>> matched_bins(coverpoints.size());
        for (size_t cp_idx = 0; cp_idx < coverpoints.size(); ++cp_idx) {
            auto& cp = coverpoints[cp_idx];
            if (!cp.target_expr) continue;
            LogicVal val = eval_fn(cp.target_expr.get(), creation_scope);
            uint64_t aval = val.aval;
            auto const_eval = [&](ExpressionNode* e) -> uint64_t {
                if (auto num = dynamic_cast<NumberNode*>(e)) return num->aval;
                return eval_fn(e, creation_scope).aval;
            };
            for (size_t b_idx = 0; b_idx < cp.bins.size(); ++b_idx) {
                if (cp.bins[b_idx].matches(aval, const_eval)) {
                    cp.bins[b_idx].hit_count++;
                    matched_bins[cp_idx].push_back(b_idx);
                }
            }
        }

        for (auto& cross_item : crosses) {
            bool all_have_matches = true;
            for (size_t cp_idx : cross_item.crossed_cp_indices) {
                if (cp_idx >= matched_bins.size() || matched_bins[cp_idx].empty()) {
                    all_have_matches = false;
                    break;
                }
            }
            if (!all_have_matches) continue;

            for (auto& cb : cross_item.cross_bins) {
                bool bin_matches = true;
                for (size_t k = 0; k < cross_item.crossed_cp_indices.size(); ++k) {
                    size_t cp_idx = cross_item.crossed_cp_indices[k];
                    size_t target_bin_idx = cb.bin_indices[k];
                    const auto& m_list = matched_bins[cp_idx];
                    if (std::find(m_list.begin(), m_list.end(), target_bin_idx) == m_list.end()) {
                        bin_matches = false;
                        break;
                    }
                }
                if (bin_matches) {
                    cb.hit_count++;
                }
            }
        }
    }

    double get_coverage() const {
        size_t total_items = coverpoints.size() + crosses.size();
        if (total_items == 0) return 0.0;
        double sum = 0.0;
        for (const auto& cp : coverpoints) {
            sum += cp.get_coverage();
        }
        for (const auto& cr : crosses) {
            sum += cr.get_coverage();
        }
        return sum / static_cast<double>(total_items);
    }
};
