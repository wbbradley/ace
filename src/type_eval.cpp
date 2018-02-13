#include "types.h"

namespace types {
    auto type_true = type_id(make_iid("true"));
    auto type_false = type_id(make_iid("false"));
    auto truthy_id = make_iid("Truthy");
    auto falsey_id = make_iid("Falsey");
    auto type_truthy_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(truthy_id)));
    auto type_falsey_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(falsey_id)));


    type_t::ref type_t::eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const {
        return shared_from_this();
    }

    type_t::ref type_id_t::eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const {
        auto nominal_mapping = nominal_env.find(id->get_name());
        if (nominal_mapping != nominal_env.end()) {
            return nominal_mapping->second->eval_expr(nominal_env, structural_env, allow_structural_env);
        } else if (allow_structural_env) {
            auto structural_mapping = structural_env.find(id->get_name());
            if (structural_mapping != structural_env.end()) {
                return structural_mapping->second->eval_expr(nominal_env, structural_env, allow_structural_env);
            }
        }

        return shared_from_this();
    }

    type_t::ref type_operator_t::eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const {
        auto oper_ = oper->eval_expr(nominal_env, structural_env, allow_structural_env);

        if (is_type_id(oper_, TYPE_OP_NOT)) {
            auto operand_ = operand->eval_expr(nominal_env, structural_env, allow_structural_env);
            if (is_type_id(operand_, "false")) {
                return type_true;
            } else if (oper_ != oper || operand_ != operand) {
                return type_operator(oper_, operand_);
            } else {
                return shared_from_this();
            }
        } else if (is_type_id(oper_, TYPE_OP_GC)) {
            if (is_managed_ptr(operand, structural_env)) {
                return type_true;
            } else {
                return type_false;
            }
        } else if (is_type_id(oper_, TYPE_OP_IF)) {
            auto operand_ = operand->eval_expr(nominal_env, structural_env, allow_structural_env);
            if (is_type_id(operand_, "true")) {
                return type_truthy_lambda;
            } else {
                return type_falsey_lambda;
            }
        } else if (auto lambda = dyncast<const type_lambda_t>(oper_)) {
            auto var_name = lambda->binding->get_name();
            return lambda->body->rebind({{var_name, operand}})->eval_expr(nominal_env, structural_env, allow_structural_env);
        } else {
            return shared_from_this();
        }
    }

    type_t::ref type_and_t::eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const {
        static auto false_type = type_id(make_iid("false"));
        not_impl();
        return false_type;
    }
}
