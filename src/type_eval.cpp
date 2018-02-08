#include "types.h"

namespace types {

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
        assert(false);
        return nullptr;
    }

    type_t::ref type_and_t::eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const {
        static auto false_type = type_id(make_iid("false"));
        not_impl();
        return false_type;
    }

    type_t::ref type_sum_t::eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const {
        static auto false_type = type_id(make_iid("false"));
        not_impl();
        return false_type;
    }
}
