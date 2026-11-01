#pragma once
#include "../common/logic_val.hpp"
#include <algorithm>

class LogicOps {
public:

    // ------------------------------------------------------------------------
    // IEEE Std 1800-2023 §11.4.3: Unary Reduction Operators
    // ------------------------------------------------------------------------
    static LogicVal reduce_and(const LogicVal& v) {
        if (v.width == 0) return LogicVal::make_x(1);
        bool has_zero = false;
        bool has_unknown = false;

        for (uint32_t i = 0; i < v.width; ++i) {
            uint64_t a = (v.aval >> i) & 1;
            uint64_t b = (v.bval >> i) & 1;
            if (b == 1) {
                has_unknown = true;
            } else if (a == 0) {
                has_zero = true;
                break; // 0 dominates reduction AND
            }
        }

        if (has_zero) return LogicVal::make_known(1, 0);
        if (has_unknown) return LogicVal::make_x(1);
        return LogicVal::make_known(1, 1);
    }

    static LogicVal reduce_nand(const LogicVal& v) {
        LogicVal r = reduce_and(v);
        if (r.is_unknown()) return LogicVal::make_x(1);
        return LogicVal::make_known(1, r.aval ^ 1);
    }

    static LogicVal reduce_or(const LogicVal& v) {
        if (v.width == 0) return LogicVal::make_x(1);
        bool has_one = false;
        bool has_unknown = false;

        for (uint32_t i = 0; i < v.width; ++i) {
            uint64_t a = (v.aval >> i) & 1;
            uint64_t b = (v.bval >> i) & 1;
            if (b == 1) {
                has_unknown = true;
            } else if (a == 1) {
                has_one = true;
                break; // 1 dominates reduction OR
            }
        }

        if (has_one) return LogicVal::make_known(1, 1);
        if (has_unknown) return LogicVal::make_x(1);
        return LogicVal::make_known(1, 0);
    }

    static LogicVal reduce_nor(const LogicVal& v) {
        LogicVal r = reduce_or(v);
        if (r.is_unknown()) return LogicVal::make_x(1);
        return LogicVal::make_known(1, r.aval ^ 1);
    }

    static LogicVal reduce_xor(const LogicVal& v) {
        if (v.width == 0) return LogicVal::make_x(1);
        uint64_t parity = 0;

        for (uint32_t i = 0; i < v.width; ++i) {
            uint64_t a = (v.aval >> i) & 1;
            uint64_t b = (v.bval >> i) & 1;
            if (b == 1) return LogicVal::make_x(1); // Any unknown forces XOR result to X
            parity ^= a;
        }

        return LogicVal::make_known(1, parity & 1);
    }

    static LogicVal reduce_xnor(const LogicVal& v) {
        LogicVal r = reduce_xor(v);
        if (r.is_unknown()) return LogicVal::make_x(1);
        return LogicVal::make_known(1, r.aval ^ 1);
    }

    static LogicVal cast_width(const LogicVal& v, uint32_t target_width, bool is_signed) {
        if (v.is_real) return v;
        if (v.width == target_width) return v;
        if (v.width > target_width) {
            uint64_t mask = (target_width >= 64) ? ~0ULL : ((1ULL << target_width) - 1);
            return LogicVal(target_width, v.aval & mask, v.bval & mask);
        }

        uint64_t new_a = v.aval;
        uint64_t new_b = v.bval;

        if (is_signed && v.width > 0) {
            bool sign_bit_a = (v.aval >> (v.width - 1)) & 1;
            bool sign_bit_b = (v.bval >> (v.width - 1)) & 1;

            if (sign_bit_b) {
                uint64_t ext_mask = ~0ULL << v.width;
                new_a |= ext_mask;
                new_b |= ext_mask;
            } else if (sign_bit_a) {
                uint64_t ext_mask = ~0ULL << v.width;
                new_a |= ext_mask;
            }
        }

        uint64_t target_mask = (target_width >= 64) ? ~0ULL : ((1ULL << target_width) - 1);
        return LogicVal(target_width, new_a & target_mask, new_b & target_mask);
    }

    static LogicVal bitwise_not(const LogicVal& v) {
        uint64_t new_a = (~v.aval) & (~v.bval);
        uint64_t new_b = v.bval;
        new_a |= v.bval;
        return LogicVal(v.width, new_a, new_b);
    }

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

    static LogicVal bitwise_or(const LogicVal& l, const LogicVal& r) {
        uint32_t max_w = std::max(l.width, r.width);
        uint64_t l_1 = l.aval & (~l.bval);
        uint64_t r_1 = r.aval & (~r.bval);
        uint64_t any_1 = l_1 | r_1;

        uint64_t new_a = l.aval | r.aval | any_1;
        uint64_t new_b = (l.bval | r.bval) & (~any_1);
        return LogicVal(max_w, new_a, new_b);
    }

    static LogicVal bitwise_xor(const LogicVal& l, const LogicVal& r) {
        uint32_t max_w = std::max(l.width, r.width);
        uint64_t new_b = l.bval | r.bval;
        uint64_t new_a = (l.aval ^ r.aval) | new_b;
        return LogicVal(max_w, new_a, new_b);
    }

    static LogicVal shift_left(const LogicVal& l, const LogicVal& r) {
        if (r.is_unknown()) return LogicVal::make_x(l.width);
        uint64_t sh = r.aval;
        if (sh >= l.width) return LogicVal(l.width, 0, 0);
        return LogicVal(l.width, l.aval << sh, l.bval << sh);
    }

    static LogicVal shift_right(const LogicVal& l, const LogicVal& r) {
        if (r.is_unknown()) return LogicVal::make_x(l.width);
        uint64_t sh = r.aval;
        if (sh >= l.width) return LogicVal(l.width, 0, 0);
        return LogicVal(l.width, l.aval >> sh, l.bval >> sh);
    }

    static LogicVal arith_shift_right(const LogicVal& l, const LogicVal& r, bool is_signed) {
        if (r.is_unknown()) return LogicVal::make_x(l.width);
        uint64_t sh = r.aval;
        if (sh == 0) return l;

        if (!is_signed) {
            return shift_right(l, r);
        }

        if (l.width == 0) return l;
        bool sign_a = (l.aval >> (l.width - 1)) & 1;
        bool sign_b = (l.bval >> (l.width - 1)) & 1;

        if (sh >= l.width) {
            if (sign_b) return LogicVal::make_x(l.width);
            return sign_a ? LogicVal::make_known(l.width, (l.width >= 64) ? ~0ULL : ((1ULL << l.width) - 1))
                          : LogicVal::make_known(l.width, 0);
        }

        uint64_t new_a = l.aval >> sh;
        uint64_t new_b = l.bval >> sh;

        uint64_t fill_mask = (~0ULL) << (l.width - sh);
        uint64_t width_mask = (l.width >= 64) ? ~0ULL : ((1ULL << l.width) - 1);
        fill_mask &= width_mask;

        if (sign_b) {
            new_a |= fill_mask;
            new_b |= fill_mask;
        } else if (sign_a) {
            new_a |= fill_mask;
        }

        return LogicVal(l.width, new_a & width_mask, new_b & width_mask);
    }

    static LogicVal logical_eq(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
        return LogicVal::make_known(1, (l.aval == r.aval) ? 1 : 0);
    }

    static LogicVal logical_neq(const LogicVal& l, const LogicVal& r) {
        if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
        return LogicVal::make_known(1, (l.aval != r.aval) ? 1 : 0);
    }

    static LogicVal case_eq(const LogicVal& l, const LogicVal& r) {
        uint32_t w = std::max(l.width, r.width);
        LogicVal el = cast_width(l, w, false);
        LogicVal er = cast_width(r, w, false);
        bool match = (el.aval == er.aval) && (el.bval == er.bval);
        return LogicVal::make_known(1, match ? 1 : 0);
    }

    static LogicVal casez_eq(const LogicVal& l, const LogicVal& r) {
        uint32_t w = std::max(l.width, r.width);
        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        LogicVal el = cast_width(l, w, false);
        LogicVal er = cast_width(r, w, false);
        uint64_t care_mask = ~((~el.aval & el.bval) | (~er.aval & er.bval)) & mask;
        bool match = ((el.aval & care_mask) == (er.aval & care_mask)) &&
                     ((el.bval & care_mask) == (er.bval & care_mask));
        return LogicVal::make_known(1, match ? 1 : 0);
    }

    static LogicVal casex_eq(const LogicVal& l, const LogicVal& r) {
        uint32_t w = std::max(l.width, r.width);
        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        LogicVal el = cast_width(l, w, false);
        LogicVal er = cast_width(r, w, false);
        uint64_t care_mask = ~(el.bval | er.bval) & mask;
        bool match = ((el.aval & care_mask) == (er.aval & care_mask));
        return LogicVal::make_known(1, match ? 1 : 0);
    }

    static LogicVal case_neq(const LogicVal& l, const LogicVal& r) {
        bool match = (l.aval == r.aval) && (l.bval == r.bval);
        return LogicVal::make_known(1, match ? 0 : 1);
    }

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

    // IEEE 1800-2023 §6.5 & Table 6-1: wire / tri resolution function
    static LogicVal resolve_wire(const LogicVal& a, const LogicVal& b) {
        uint32_t w = std::max(a.width, b.width);
        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        LogicVal ea = cast_width(a, w, false);
        LogicVal eb = cast_width(b, w, false);

        uint64_t is_z_a = (~ea.aval) & ea.bval & mask;
        uint64_t is_z_b = (~eb.aval) & eb.bval & mask;

        uint64_t both_z = is_z_a & is_z_b;
        uint64_t only_z_a = is_z_a & (~is_z_b);
        uint64_t only_z_b = (~is_z_a) & is_z_b;
        uint64_t neither_z = (~is_z_a) & (~is_z_b) & mask;

        uint64_t same_known = neither_z & (~ea.bval) & (~eb.bval) & (~(ea.aval ^ eb.aval)) & mask;
        uint64_t conflict_or_x = neither_z & (~same_known) & mask;

        uint64_t res_a = ((only_z_a & eb.aval) | (only_z_b & ea.aval) | (same_known & ea.aval) | conflict_or_x) & mask;
        uint64_t res_b = (both_z | (only_z_a & eb.bval) | (only_z_b & ea.bval) | conflict_or_x) & mask;

        return LogicVal(w, res_a, res_b);
    }

    // IEEE 1800-2023 §6.5 & Table 6-2: wand / triand resolution function
    static LogicVal resolve_wand(const LogicVal& a, const LogicVal& b) {
        uint32_t w = std::max(a.width, b.width);
        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        LogicVal ea = cast_width(a, w, false);
        LogicVal eb = cast_width(b, w, false);

        uint64_t is_zero_a = (~ea.aval) & (~ea.bval) & mask;
        uint64_t is_zero_b = (~eb.aval) & (~eb.bval) & mask;
        uint64_t any_zero = (is_zero_a | is_zero_b) & mask;

        uint64_t not_zero = (~any_zero) & mask;
        uint64_t is_z_a = not_zero & (~ea.aval) & ea.bval;
        uint64_t is_z_b = not_zero & (~eb.aval) & eb.bval;

        uint64_t both_z = is_z_a & is_z_b;
        uint64_t only_z_a = is_z_a & (~is_z_b);
        uint64_t only_z_b = (~is_z_a) & is_z_b;
        uint64_t active = not_zero & (~is_z_a) & (~is_z_b) & mask;

        uint64_t both_one = active & ea.aval & (~ea.bval) & eb.aval & (~eb.bval) & mask;
        uint64_t is_x = active & (~both_one) & mask;

        uint64_t res_a = ((only_z_a & eb.aval) | (only_z_b & ea.aval) | both_one | is_x) & mask;
        uint64_t res_b = (both_z | (only_z_a & eb.bval) | (only_z_b & ea.bval) | is_x) & mask;

        return LogicVal(w, res_a, res_b);
    }

    // IEEE 1800-2023 §6.5 & Table 6-3: wor / trior resolution function
    static LogicVal resolve_wor(const LogicVal& a, const LogicVal& b) {
        uint32_t w = std::max(a.width, b.width);
        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        LogicVal ea = cast_width(a, w, false);
        LogicVal eb = cast_width(b, w, false);

        uint64_t is_one_a = ea.aval & (~ea.bval) & mask;
        uint64_t is_one_b = eb.aval & (~eb.bval) & mask;
        uint64_t any_one = (is_one_a | is_one_b) & mask;

        uint64_t not_one = (~any_one) & mask;
        uint64_t is_z_a = not_one & (~ea.aval) & ea.bval;
        uint64_t is_z_b = not_one & (~eb.aval) & eb.bval;

        uint64_t both_z = is_z_a & is_z_b;
        uint64_t only_z_a = is_z_a & (~is_z_b);
        uint64_t only_z_b = (~is_z_a) & is_z_b;
        uint64_t active = not_one & (~is_z_a) & (~is_z_b) & mask;

        uint64_t both_zero = active & (~ea.aval) & (~ea.bval) & (~eb.aval) & (~eb.bval) & mask;
        uint64_t is_x = active & (~both_zero) & mask;

        uint64_t res_a = (any_one | (only_z_a & eb.aval) | (only_z_b & ea.aval) | is_x) & mask;
        uint64_t res_b = (both_z | (only_z_a & eb.bval) | (only_z_b & ea.bval) | is_x) & mask;

        return LogicVal(w, res_a, res_b);
    }

    // Unified resolution dispatch based on NetKind
    static LogicVal resolve_net(NetKind kind, const LogicVal& a, const LogicVal& b) {
        switch (kind) {
            case NetKind::WAND: return resolve_wand(a, b);
            case NetKind::WOR:  return resolve_wor(a, b);
            case NetKind::WIRE:
            case NetKind::TRI:
            default:            return resolve_wire(a, b);
        }
    }

    // IEEE 1800-2023 §28.5 & Tables 28-5, 28-6: Tristate control gates (bufif1, bufif0, notif1, notif0)
    static LogicVal eval_bufif(bool if1, bool invert_data, const LogicVal& data, const LogicVal& ctrl) {
        uint32_t w = data.width;
        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);

        uint64_t ctrl_a = (ctrl.width == 1) ? (-(ctrl.aval & 1ULL)) : ctrl.aval;
        uint64_t ctrl_b = (ctrl.width == 1) ? (-(ctrl.bval & 1ULL)) : ctrl.bval;
        ctrl_a &= mask;
        ctrl_b &= mask;

        uint64_t ctrl_is_1 = ctrl_a & (~ctrl_b) & mask;
        uint64_t ctrl_is_0 = (~ctrl_a) & (~ctrl_b) & mask;
        uint64_t ctrl_is_xz = ctrl_b & mask;

        uint64_t enable = if1 ? ctrl_is_1 : ctrl_is_0;
        uint64_t disable = if1 ? ctrl_is_0 : ctrl_is_1;

        uint64_t d_a = data.aval & mask;
        uint64_t d_b = data.bval & mask;
        if (invert_data) {
            uint64_t is_known = ~d_b & mask;
            uint64_t not_a = (~d_a) & is_known;
            uint64_t not_b = d_b & mask;
            d_a = not_a | (d_b & mask);
            d_b = not_b;
        } else {
            uint64_t is_z = (~d_a) & d_b & mask;
            d_a |= is_z;
        }

        uint64_t res_a = ((enable & d_a) | ctrl_is_xz) & mask;
        uint64_t res_b = ((enable & d_b) | disable | ctrl_is_xz) & mask;

        return LogicVal(w, res_a, res_b);
    }
};