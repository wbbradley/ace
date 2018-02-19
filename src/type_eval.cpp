#include "types.h"
#include "scopes.h"
#include "dbg.h"

const char *tbstr(type_builtins_t tb) {
	switch (tb) {
	case tb_gc:
		return "gc";
	case tb_ref:
		return "is_ref";
	case tb_true:
		return "is_true";
	case tb_false:
		return "is_false";
	case tb_pointer:
		return "is_pointer";
	case tb_function:
		return "is_function";
	case tb_void:
		return "is_void";
	case tb_null:
		return "is_null";
	case tb_zero:
		return "is_zero";
	case tb_maybe:
		return "is_maybe";
	}

	assert(false);
	return "";
}
namespace types {
    auto type_true = type_id(make_iid("true"));
    auto type_false = type_id(make_iid("false"));
    auto truthy_id = make_iid("Truthy");
    auto falsey_id = make_iid("Falsey");
    auto type_truthy_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(truthy_id)));
    auto type_falsey_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(falsey_id)));


	bool type_t::eval_predicate(type_builtins_t tb, const scope_t::ref &scope) const {
		return eval_predicate(tb, scope->get_nominal_env(), scope->get_total_env());
	}

	bool type_t::eval_predicate(type_builtins_t tb, const map &nominal_env, const map &total_env) const {
		debug_above(8, log("%s receiving eval_predicate(%s, ..., ...)",
					str().c_str(), tbstr(tb)));

		auto predicate = type_operator(type_id(make_iid(tbstr(tb))), shared_from_this());
		auto result = predicate->eval_core(nominal_env, total_env, false);
		if (auto id_type = dyncast<const types::type_id_t>(result)) {
			if (id_type->id->get_name() == TRUE_TYPE) {
				return true;
			} else if (id_type->id->get_name() == FALSE_TYPE) {
				return false;
			} else {
				log(c_var("predicate") " %s => %s", predicate->str().c_str(), result->str().c_str());
				assert(false);
				return false;
			}
		} else {
			log(c_var("predicate") " %s => %s", predicate->str().c_str(), result->str().c_str());
			assert(false);
			return false;
		}
	}

    type_t::ref type_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        return shared_from_this();
    }

	type_t::ref type_lazy_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
		status_t status;
		auto type = type_sum_safe(status, options, location, nominal_env, total_env);

		// TODO: plumbing...
		assert(!!status);

		return type->eval_core(nominal_env, total_env, get_structural_type);
	}

	type_t::ref type_t::eval(const map &nominal_env, const map &structural_env, bool get_structural_type) const {
		auto res = eval_core(nominal_env, structural_env, get_structural_type);
		debug_above(10, log("eval(%s, %s) -> %s",
					str().c_str(), boolstr(get_structural_type),
					res->str().c_str()));
		return res;
	}

    type_t::ref type_t::eval(const scope_t::ref &scope, bool get_structural_type) const {
		return eval(scope->get_nominal_env(), scope->get_total_env(), get_structural_type);
    }

    type_t::ref type_id_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto nominal_mapping = nominal_env.find(id->get_name());
		if (nominal_mapping != nominal_env.end()) {
			return nominal_mapping->second->eval_core(nominal_env, total_env, get_structural_type);
		} else if (get_structural_type) {
            auto structural_mapping = total_env.find(id->get_name());
			if (structural_mapping != total_env.end()) {
				return structural_mapping->second->eval_core(nominal_env, total_env, get_structural_type);
			}
        }

        return shared_from_this();
    }

    type_t::ref type_operator_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto oper_ = oper->eval_core(nominal_env, total_env, get_structural_type);

        if (is_type_id(oper_, TYPE_OP_NOT, {}, {})) {
            auto operand_ = operand->eval_core(nominal_env, total_env, get_structural_type);
            if (is_type_id(operand_, FALSE_TYPE, {}, {})) {
                return type_true;
            } else if (oper_ != oper || operand_ != operand) {
                return type_operator(oper_, operand_);
            } else {
                return shared_from_this();
            }
        } else if (is_type_id(oper_, TYPE_OP_GC, {}, {})) {
            if (is_managed_ptr(operand, nominal_env, total_env)) {
                return type_true;
            } else {
                return type_false;
            }
        } else if (is_type_id(oper_, TYPE_OP_IF, {}, {})) {
            auto operand_ = operand->eval_core(nominal_env, total_env, get_structural_type);
            if (is_type_id(operand_, "true", {}, {})) {
                return type_truthy_lambda;
            } else {
                return type_falsey_lambda;
            }
        } else if (auto lambda = dyncast<const type_lambda_t>(oper_)) {
            auto var_name = lambda->binding->get_name();
            return lambda->body->rebind({{var_name, operand}})->eval_core(nominal_env, total_env, get_structural_type);
        } else {
            return shared_from_this();
        }
    }

    type_t::ref type_and_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        static auto false_type = type_id(make_iid("false"));
        not_impl();
        return false_type;
    }

    type_t::ref type_ptr_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto expansion = element_type->eval_core(nominal_env, total_env, get_structural_type);
        if (expansion != element_type) {
            return type_ptr(expansion);
        } else {
            return shared_from_this();
        }
    }

    type_t::ref type_ref_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto expansion = element_type->eval_core(nominal_env, total_env, get_structural_type);
        if (expansion != element_type) {
            return type_ref(expansion);
        } else {
            return shared_from_this();
        }
    }

    type_t::ref type_maybe_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
        auto expansion = just->eval_core(nominal_env, total_env, get_structural_type);
        if (expansion != just) {
            return type_maybe(expansion);
        } else {
            return shared_from_this();
        }
    }
}
