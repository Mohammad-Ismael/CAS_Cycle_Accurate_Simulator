#pragma once
#include "ast_base.hpp"
#include <string>
#include <cstdint>
#include <memory>
#include <vector>

struct NumberNode : public ExpressionNode {
    uint64_t aval;
    uint64_t bval;
    uint32_t width;
    bool is_real = false;
    double real_val = 0.0;

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    explicit NumberNode(T val, uint32_t w = 32, bool unk = false)
        : aval(unk ? ((w >= 64) ? ~0ULL : ((1ULL << w) - 1)) : static_cast<uint64_t>(val)),
          bval(unk ? ((w >= 64) ? ~0ULL : ((1ULL << w) - 1)) : 0),
          width(w), is_real(false), real_val(0.0) {}

    NumberNode(uint64_t a, uint64_t b, uint32_t w)
        : aval(a), bval(b), width(w), is_real(false), real_val(0.0) {}

    explicit NumberNode(double r)
        : aval(static_cast<uint64_t>(r)), bval(0), width(64), is_real(true), real_val(r) {}

    bool is_unknown() const { return bval != 0; }

    std::unique_ptr<ExpressionNode> clone() const override {
        auto node = std::make_unique<NumberNode>(aval, bval, width);
        node->is_real = is_real;
        node->real_val = real_val;
        return node;
    }
};

struct IdentifierNode : public ExpressionNode {
    std::string name;

    explicit IdentifierNode(std::string n) : name(std::move(n)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<IdentifierNode>(name);
    }
};

struct FunctionCallNode : public ExpressionNode {
    std::string func_name;
    std::vector<std::unique_ptr<ExpressionNode>> args;

    FunctionCallNode(std::string name, std::vector<std::unique_ptr<ExpressionNode>> a)
        : func_name(std::move(name)), args(std::move(a)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> c_args;
        for (const auto& a : args) c_args.push_back(a ? a->clone() : nullptr);
        return std::make_unique<FunctionCallNode>(func_name, std::move(c_args));
    }
};

struct IndexSelectNode : public ExpressionNode {
    std::string name;
    std::unique_ptr<ExpressionNode> msb_expr;
    std::unique_ptr<ExpressionNode> lsb_expr;
    bool is_part_select = false;

    IndexSelectNode(std::string n, std::unique_ptr<ExpressionNode> idx)
        : name(std::move(n)), msb_expr(std::move(idx)), lsb_expr(nullptr), is_part_select(false) {}

    IndexSelectNode(std::string n, std::unique_ptr<ExpressionNode> msb, std::unique_ptr<ExpressionNode> lsb)
        : name(std::move(n)), msb_expr(std::move(msb)), lsb_expr(std::move(lsb)), is_part_select(true) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        if (!is_part_select) {
            return std::make_unique<IndexSelectNode>(name, msb_expr ? msb_expr->clone() : nullptr);
        } else {
            return std::make_unique<IndexSelectNode>(
                name,
                msb_expr ? msb_expr->clone() : nullptr,
                lsb_expr ? lsb_expr->clone() : nullptr
            );
        }
    }
};

struct ConcatNode : public ExpressionNode {
    std::vector<std::unique_ptr<ExpressionNode>> elements;

    ConcatNode() = default;
    explicit ConcatNode(std::vector<std::unique_ptr<ExpressionNode>> elems)
        : elements(std::move(elems)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> cloned;
        for (const auto& elem : elements) {
            cloned.push_back(elem ? elem->clone() : nullptr);
        }
        return std::make_unique<ConcatNode>(std::move(cloned));
    }
};

struct ReplicateNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> count_expr;
    std::unique_ptr<ExpressionNode> target_expr;

    ReplicateNode(std::unique_ptr<ExpressionNode> c, std::unique_ptr<ExpressionNode> t)
        : count_expr(std::move(c)), target_expr(std::move(t)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<ReplicateNode>(
            count_expr ? count_expr->clone() : nullptr,
            target_expr ? target_expr->clone() : nullptr
        );
    }
};

struct UnaryOpNode : public ExpressionNode {
    std::string op;
    std::unique_ptr<ExpressionNode> operand;

    UnaryOpNode(std::string o, std::unique_ptr<ExpressionNode> opnd)
        : op(std::move(o)), operand(std::move(opnd)) {}

    UnaryOpNode(char o, std::unique_ptr<ExpressionNode> opnd)
        : op(1, o), operand(std::move(opnd)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<UnaryOpNode>(op, operand ? operand->clone() : nullptr);
    }
};

struct BinaryOpNode : public ExpressionNode {
    std::string op;
    std::unique_ptr<ExpressionNode> lhs;
    std::unique_ptr<ExpressionNode> rhs;

    BinaryOpNode(std::string o, std::unique_ptr<ExpressionNode> l, std::unique_ptr<ExpressionNode> r)
        : op(std::move(o)), lhs(std::move(l)), rhs(std::move(r)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<BinaryOpNode>(
            op,
            lhs ? lhs->clone() : nullptr,
            rhs ? rhs->clone() : nullptr
        );
    }
};

struct TernaryOpNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> condition;
    std::unique_ptr<ExpressionNode> true_expr;
    std::unique_ptr<ExpressionNode> false_expr;

    TernaryOpNode(std::unique_ptr<ExpressionNode> c,
                  std::unique_ptr<ExpressionNode> t,
                  std::unique_ptr<ExpressionNode> f)
        : condition(std::move(c)), true_expr(std::move(t)), false_expr(std::move(f)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<TernaryOpNode>(
            condition ? condition->clone() : nullptr,
            true_expr ? true_expr->clone() : nullptr,
            false_expr ? false_expr->clone() : nullptr
        );
    }
};

struct MethodCallNode : public ExpressionNode {
    std::string var_name;
    std::string method_name;
    std::vector<std::unique_ptr<ExpressionNode>> args;

    MethodCallNode(std::string v, std::string m, std::vector<std::unique_ptr<ExpressionNode>> a = {})
        : var_name(std::move(v)), method_name(std::move(m)), args(std::move(a)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> c_args;
        for (const auto& a : args) c_args.push_back(a ? a->clone() : nullptr);
        return std::make_unique<MethodCallNode>(var_name, method_name, std::move(c_args));
    }
};

struct MemberAccessNode : public ExpressionNode {
    std::string var_name;
    std::string member_name;

    MemberAccessNode(std::string v, std::string m)
        : var_name(std::move(v)), member_name(std::move(m)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<MemberAccessNode>(var_name, member_name);
    }
};

struct CastNode : public ExpressionNode {
    std::string target_type;
    std::unique_ptr<ExpressionNode> expr;

    CastNode(std::string t, std::unique_ptr<ExpressionNode> e)
        : target_type(std::move(t)), expr(std::move(e)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<CastNode>(target_type, expr ? expr->clone() : nullptr);
    }
};

struct StringNode : public ExpressionNode {
    std::string value;

    explicit StringNode(std::string val) : value(std::move(val)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<StringNode>(value);
    }
};


struct NewArrayNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> size_expr;
    std::unique_ptr<ExpressionNode> copy_source_expr;

    NewArrayNode(std::unique_ptr<ExpressionNode> sz, std::unique_ptr<ExpressionNode> cp = nullptr)
        : size_expr(std::move(sz)), copy_source_expr(std::move(cp)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<NewArrayNode>(
            size_expr ? size_expr->clone() : nullptr,
            copy_source_expr ? copy_source_expr->clone() : nullptr
        );
    }
};

struct NullNode : public ExpressionNode {
    NullNode() = default;

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<NullNode>();
    }
};

struct ThisNode : public ExpressionNode {
    ThisNode() = default;

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<ThisNode>();
    }
};

struct ClassNewNode : public ExpressionNode {
    std::string class_type_name;
    std::vector<std::unique_ptr<ExpressionNode>> args;

    ClassNewNode(std::string type_name, std::vector<std::unique_ptr<ExpressionNode>> a)
        : class_type_name(std::move(type_name)), args(std::move(a)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> cloned_args;
        for (const auto& arg : args) {
            cloned_args.push_back(arg ? arg->clone() : nullptr);
        }
        return std::make_unique<ClassNewNode>(class_type_name, std::move(cloned_args));
    }
};

struct SysCastCallNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> dest_expr;
    std::unique_ptr<ExpressionNode> src_expr;

    SysCastCallNode(std::unique_ptr<ExpressionNode> d, std::unique_ptr<ExpressionNode> s)
        : dest_expr(std::move(d)), src_expr(std::move(s)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<SysCastCallNode>(
            dest_expr ? dest_expr->clone() : nullptr,
            src_expr ? src_expr->clone() : nullptr
        );
    }
};

struct InsideRange {
    std::unique_ptr<ExpressionNode> low;
    std::unique_ptr<ExpressionNode> high;

    InsideRange() = default;
    InsideRange(std::unique_ptr<ExpressionNode> l, std::unique_ptr<ExpressionNode> h = nullptr)
        : low(std::move(l)), high(std::move(h)) {}

    InsideRange clone() const {
        return InsideRange(low ? low->clone() : nullptr, high ? high->clone() : nullptr);
    }
};

struct InsideNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> target;
    std::vector<InsideRange> ranges;

    InsideNode(std::unique_ptr<ExpressionNode> t, std::vector<InsideRange> r)
        : target(std::move(t)), ranges(std::move(r)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<InsideRange> cloned_ranges;
        for (const auto& r : ranges) {
            cloned_ranges.push_back(r.clone());
        }
        return std::make_unique<InsideNode>(target ? target->clone() : nullptr, std::move(cloned_ranges));
    }
};

struct RandomizeCallNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> target_handle;
    std::vector<std::unique_ptr<ExpressionNode>> inline_constraints;

    RandomizeCallNode(std::unique_ptr<ExpressionNode> target, std::vector<std::unique_ptr<ExpressionNode>> constraints = {})
        : target_handle(std::move(target)), inline_constraints(std::move(constraints)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> cloned_constraints;
        for (const auto& c : inline_constraints) {
            cloned_constraints.push_back(c ? c->clone() : nullptr);
        }
        return std::make_unique<RandomizeCallNode>(
            target_handle ? target_handle->clone() : nullptr,
            std::move(cloned_constraints)
        );
    }
};

enum class SysSampledFuncType {
    ROSE,
    FELL,
    STABLE,
    PAST
};

struct SysSampledFuncNode : public ExpressionNode {
    SysSampledFuncType func_type;
    std::string func_name;
    std::unique_ptr<ExpressionNode> expr;
    std::unique_ptr<ExpressionNode> num_cycles_expr;

    SysSampledFuncNode(SysSampledFuncType t, std::string name, std::unique_ptr<ExpressionNode> e, std::unique_ptr<ExpressionNode> cycles = nullptr)
        : func_type(t), func_name(std::move(name)), expr(std::move(e)), num_cycles_expr(std::move(cycles)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<SysSampledFuncNode>(
            func_type,
            func_name,
            expr ? expr->clone() : nullptr,
            num_cycles_expr ? num_cycles_expr->clone() : nullptr
        );
    }
};

enum class RepetitionType {
    CONSECUTIVE,
    NON_CONSECUTIVE,
    GOTO
};

struct SequenceRepetitionNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> expr;
    RepetitionType rep_type;
    uint32_t min_count = 1;
    uint32_t max_count = 1;

    SequenceRepetitionNode(std::unique_ptr<ExpressionNode> e, RepetitionType t, uint32_t min_c, uint32_t max_c)
        : expr(std::move(e)), rep_type(t), min_count(min_c), max_count(max_c) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<SequenceRepetitionNode>(
            expr ? expr->clone() : nullptr,
            rep_type,
            min_count,
            max_count
        );
    }
};

struct SequenceConcatItem {
    uint32_t delay = 0;
    std::unique_ptr<ExpressionNode> expr;

    SequenceConcatItem clone() const {
        return {delay, expr ? expr->clone() : nullptr};
    }
};

struct SequenceConcatNode : public ExpressionNode {
    std::vector<SequenceConcatItem> elements;

    SequenceConcatNode() = default;
    explicit SequenceConcatNode(std::vector<SequenceConcatItem> elems) : elements(std::move(elems)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<SequenceConcatItem> cloned;
        for (const auto& item : elements) {
            cloned.push_back(item.clone());
        }
        return std::make_unique<SequenceConcatNode>(std::move(cloned));
    }
};

struct SequenceMatchAssignment {
    std::string var_name;
    std::unique_ptr<ExpressionNode> expr;

    SequenceMatchAssignment clone() const {
        return {var_name, expr ? expr->clone() : nullptr};
    }
};

struct SequenceMatchItemNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> expr;
    std::vector<SequenceMatchAssignment> match_assignments;

    SequenceMatchItemNode(std::unique_ptr<ExpressionNode> e, std::vector<SequenceMatchAssignment> assigns)
        : expr(std::move(e)), match_assignments(std::move(assigns)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<SequenceMatchAssignment> cloned_assigns;
        for (const auto& a : match_assignments) {
            cloned_assigns.push_back(a.clone());
        }
        return std::make_unique<SequenceMatchItemNode>(
            expr ? expr->clone() : nullptr,
            std::move(cloned_assigns)
        );
    }
};

struct StaticMethodCallNode : public ExpressionNode {
    std::string class_type_name;
    std::string method_name;
    std::vector<std::unique_ptr<ExpressionNode>> args;

    StaticMethodCallNode(std::string cls_type, std::string m_name, std::vector<std::unique_ptr<ExpressionNode>> a)
        : class_type_name(std::move(cls_type)), method_name(std::move(m_name)), args(std::move(a)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> c_args;
        for (const auto& a : args) c_args.push_back(a ? a->clone() : nullptr);
        return std::make_unique<StaticMethodCallNode>(class_type_name, method_name, std::move(c_args));
    }
};

struct StaticMemberAccessNode : public ExpressionNode {
    std::string class_type_name;
    std::string member_name;

    StaticMemberAccessNode(std::string cls_type, std::string m_name)
        : class_type_name(std::move(cls_type)), member_name(std::move(m_name)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<StaticMemberAccessNode>(class_type_name, member_name);
    }
};
