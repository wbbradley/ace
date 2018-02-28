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
	case tb_maybe:
		return "is_maybe";
	}

	assert(false);
	return "";
}

const char *id_from_tb(type_builtins_t tb) {
	switch (tb) {
	case tb_gc:
		return nullptr;
	case tb_ref:
		return nullptr;
	case tb_true:
		return TRUE_TYPE;
	case tb_false:
		return FALSE_TYPE;
	case tb_pointer:
		return nullptr;
	case tb_function:
		return nullptr;
	case tb_void:
		return VOID_TYPE;
	case tb_null:
		return NULL_TYPE;
	case tb_maybe:
		return nullptr;
	}
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
		debug_above(9, log("%s receiving eval_predicate(%s, ..., ...)",
					str().c_str(), tbstr(tb)));

		if (const char *id = id_from_tb(tb)) {
			return is_type_id(shared_from_this(), id, nominal_env, total_env);
		}

		auto predicate = type_operator(type_id(make_iid(tbstr(tb))), shared_from_this());
		auto result = predicate->eval_core(nominal_env, total_env, false);
		if (auto id_type = dyncast<const types::type_id_t>(result)) {
			if (id_type->id->get_name() == TRUE_TYPE) {
				return true;
			} else if (id_type->id->get_name() == FALSE_TYPE) {
				return false;
			}
		}

		log(c_var("predicate") " %s => %s", predicate->str().c_str(), result->str().c_str());
		assert(false);
		return false;
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

	type_t::ref type_eval_not(types::type_t::ref operand, const type_t::map &nominal_env, const type_t::map &total_env) {
		auto operand_ = operand->eval_core(nominal_env, total_env, false);
		if (is_type_id(operand_, FALSE_TYPE, {}, {})) {
			return type_true;
		} else if (is_type_id(operand_, TRUE_TYPE, {}, {})) {
			return type_false;
		} else {
			return nullptr;
		}
	}

	type_t::ref type_eval_is_gc(types::type_t::ref operand, const type_t::map &nominal_env, const type_t::map &total_env) {
		if (is_managed_ptr(operand, nominal_env, total_env)) {
			return type_true;
		} else {
			return type_false;
		}
	}

	template <typename T>
	type_t::ref type_eval_is_type(types::type_t::ref operand, const type_t::map &nominal_env, const type_t::map &total_env) {
		if (dyncast<const T>(operand->eval(nominal_env, total_env))) {
			return type_true;
		} else {
			return type_false;
		}
	}

#define type_eval_is_(z, Z) \
	type_t::ref type_eval_is_##z(types::type_t::ref operand, const type_t::map &nominal_env, const type_t::map &total_env) { \
		if (auto id_type = dyncast<const types::type_id_t>(operand->eval(nominal_env, total_env))) { \
			if (id_type->id->get_name() == Z) { \
				return type_true; \
			} \
		} \
		return type_false; \
	}
	type_eval_is_(false, FALSE_TYPE)
	type_eval_is_(true, TRUE_TYPE)
	type_eval_is_(void, VOID_TYPE)
	type_eval_is_(null, NULL_TYPE)

	type_t::ref type_eval_if(types::type_t::ref operand, const type_t::map &nominal_env, const type_t::map &total_env) {
		auto operand_ = operand->eval_core(nominal_env, total_env, false);
		if (is_type_id(operand_, TRUE_TYPE, {}, {})) {
			return type_truthy_lambda;
		} else if (is_type_id(operand_, FALSE_TYPE, {}, {})) {
			return type_falsey_lambda;
		} else {
			return nullptr;
		}
	}

	type_t::ref eval_builtin_func(
			std::string function_name,
			type_t::ref (*fn)(types::type_t::ref, const type_t::map &, const type_t::map &),
			type_t::ref value,
			const type_t::map &nominal_env,
			const type_t::map &total_env,
			type_t::ref default_value)
	{
		auto result = fn(value, nominal_env, total_env);
		if (result != nullptr) {
			return result;
		} else {
			log(log_warning, "unable to compute function %s for %s", function_name.c_str(), default_value->str().c_str());
			assert(false);
			return default_value;
		}
	}

	type_t::ref type_operator_t::eval_core(const map &nominal_env, const map &total_env, bool get_structural_type) const {
		auto oper_ = oper->eval_core(nominal_env, total_env, get_structural_type);

		static struct {
			const std::string function_name;
			type_t::ref (*type_eval)(types::type_t::ref, const type_t::map &, const type_t::map &);
		} builtin_functions[] = {
			{TYPE_OP_NOT, type_eval_not},
			{TYPE_OP_GC, type_eval_is_gc},
			{TYPE_OP_IF, type_eval_if},
			{TYPE_OP_IS_REF, type_eval_is_type<type_ref_t>},
			{TYPE_OP_IS_TRUE, type_eval_is_true},
			{TYPE_OP_IS_FALSE, type_eval_is_false},
			{TYPE_OP_IS_POINTER, type_eval_is_type<type_ptr_t>},
			{TYPE_OP_IS_FUNCTION, type_eval_is_type<type_function_t>},
			{TYPE_OP_IS_VOID, type_eval_is_void},
			{TYPE_OP_IS_NULL, type_eval_is_null},
			{TYPE_OP_IS_MAYBE, type_eval_is_type<type_maybe_t>},
		};

		if (auto lambda = dyncast<const type_lambda_t>(oper_)) {
			auto var_name = lambda->binding->get_name();
			return lambda->body->rebind({{var_name, operand}})->eval_core(nominal_env, total_env, get_structural_type);
		}

		for (size_t i=0; i<sizeof(builtin_functions)/sizeof(builtin_functions[0]); ++i) {
			if (is_type_id(oper_, builtin_functions[i].function_name, {}, {})) {
				return eval_builtin_func(
						builtin_functions[i].function_name,
						builtin_functions[i].type_eval,
						operand,
						nominal_env,
						total_env,
						shared_from_this());
			}
		}

		return shared_from_this();
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
