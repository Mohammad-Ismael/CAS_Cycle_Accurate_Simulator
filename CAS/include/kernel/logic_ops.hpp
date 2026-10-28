#pragma once
#include "../common/logic_val.hpp"
#include <algorithm>

class LogicOps {
public:
    // Bitwise NOT (~)
    static LogicVal bitwise_not(const LogicVal& v) {
        uint64_t new_a = (~v.aval) & (~v.bval);
        // Any X remains X (a=1, b=1), Z converts to X in standard operations
        uint64_t new_b = v.bval;
        new_a |= v.bval; // IEEE: ~X -> X, ~Z -> X
        return LogicVal(v.width, new_a, new_b);
    }

    // Bitwise AND (&)
    static LogicVal bitwise_and(const LogicVal& l, const LogicVal& r) {
        uint32_t max_w = std::max(l.width, r.width);
        uint64_t l_0 = (~l.aval) & (~l.bval);
        uint64_t r_0 = (~r.aval) & (~r.bval);
        uint64_t any_0 = l_0 | r_0;

        uint64_t new_a = (l.aval & r.aval) | (l.bval | r.bval);
        uint64_t new_b = (l.bval | r.bval) & (~any_0);
        new_a &= (~any_0);
        return LogicVal(max_w, new_a, new_b);
    }

    // Bitwise OR (|)
    static LogicVal bitwise_or(const LogicVal& l, const LogicVal& r) {
        uint32_t max_w = std::max(l.width, r.width);
        uint64_t l_1 = l.aval & (~l.bval);
        uint64_t r_1 = r.aval & (~r.bval);
        uint64_t any_1 = l_1 | r_1;

        uint64_t new_a = l.aval | r.aval | any_1;
        uint64_t new_b = (l.bval | r.bval) & (~any_1);
        return LogicVal(max_w, new_a, new_b);
    }

    // Bitwise XOR (^)
    static LogicVal bitwise_xor(const LogicVal& l, const LogicVal& r) {
        uint32_t max_w = std::max(l.width, r.width);
        uint64_t new_b = l.bval | r.bval;
        uint64_t new_a = (l.aval ^ r.aval) | new_b;
        return LogicVal(max_w, new_a, new_b);
    }

    // Logical Left Shift (<<)
    static LogicVal shift_left(const LogicVal& l, const LogicVal& r) {
        if (r.is_unknown()) return LogicVal::make_x(l.width);
        uint64_t sh = r.aval;
        if (sh >= l.width) return LogicVal(l.width, 0, 0);
        return LogicVal(l.width, l.aval << sh, l.bval << sh);
    }

    // Logical Right Shift (>>)
    static LogicVal shift_right(const LogicVal& l, const LogicVal& r) {
        if (r.is_unknown()) return LogicVal::make_x(l.width);
        uint64_t sh = r.aval;
        if (sh >= l.width) return LogicVal(l.width, 0, 0);
        return LogicVal(l.width, l.aval >> sh, l.bval >> sh);
    }

    // Logical Equality (==) -> Returns 1'bx if either side has X or Z (§11.4.5)
    static LogicVal logical_eq(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
        return LogicVal::make_known(1, (l.aval == r.aval) ? 1 : 0);
    }

    // Logical Inequality (!=) -> Returns 1'bx if either side has X or Z (§11.4.5)
    static LogicVal logical_neq(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
        return LogicVal::make_known(1, (l.aval != r.aval) ? 1 : 0);
    }

    // Case Equality (===) -> Strict 4-state bit match (§11.4.5)
    static LogicVal case_eq(const LogicVal& l, const LogicVal& r) {
        bool match = (l.aval == r.aval) && (l.bval == r.bval);
        return LogicVal::make_known(1, match ? 1 : 0);
    }

    // Case Inequality (!==) -> Strict 4-state bit mismatch (§11.4.5)
    static LogicVal case_neq(const LogicVal& l, const LogicVal& r) {
        bool match = (l.aval == r.aval) && (l.bval == r.bval);
        return LogicVal::make_known(1, match ? 0 : 1);
    }

    // Ternary Operator: cond ? expr1 : expr2 (§11.4.11)
    // When cond has X/Z, matching bits pass through; differing bits become X
    static LogicVal ternary(const LogicVal& cond, const LogicVal& t_val, const LogicVal& f_val) {
        if (!cond.is_unknown()) {
            return (cond.aval != 0) ? t_val : f_val;
        }
        uint32_t w = std::max(t_val.width, f_val.width);
        uint64_t same_known_0 = ((~t_val.aval) & (~t_val.bval)) & ((~f_val.aval) & (~f_val.bval));
        uint64_t same_known_1 = (t_val.aval & (~t_val.bval)) & (f_val.aval & (~f_val.bval));

        uint64_t new_a = same_known_1 | (~(same_known_0 | same_known_1));
        uint64_t new_b = ~(same_known_0 | same_known_1);
        return LogicVal(w, new_a, new_b);
    }
};