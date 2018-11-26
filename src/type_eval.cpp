#include "types.h"
#include "scopes.h"
#include "dbg.h"
#include "unification.h"

const char *tbstr(type_builtins_t tb) {
	switch (tb) {
	case tb_gc:
		return TYPE_OP_GC;
	case tb_ref:
		return TYPE_OP_IS_REF;
	case tb_true:
		return TYPE_OP_IS_TRUE;
	case tb_false:
		return TYPE_OP_IS_FALSE;
	case tb_bool:
		return TYPE_OP_IS_BOOL;
	case tb_str:
		return TYPE_OP_IS_STR;
	case tb_pointer:
		return TYPE_OP_IS_POINTER;
	case tb_function:
		return TYPE_OP_IS_FUNCTION;
	case tb_callable:
		return TYPE_OP_IS_CALLABLE;
	case tb_void:
		return TYPE_OP_IS_VOID;
	case tb_bottom:
		return TYPE_OP_IS_BOTTOM;
	case tb_unit:
		return TYPE_OP_IS_UNIT;
	case tb_null:
		return TYPE_OP_IS_NULL;
	case tb_int:
		return TYPE_OP_IS_INT;
	case tb_maybe:
		return TYPE_OP_IS_MAYBE;
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
	case tb_bool:
		return nullptr;
	case tb_false:
		return FALSE_TYPE;
	case tb_pointer:
		return nullptr;
	case tb_function:
		return nullptr;
	case tb_callable:
		return nullptr;
	case tb_void:
		return VOID_TYPE;
	case tb_bottom:
		return BOTTOM_TYPE;
	case tb_unit:
		return nullptr;
	case tb_null:
		return NULL_TYPE;
	case tb_int:
		return nullptr;
	case tb_str:
		return MANAGED_STR;
	case tb_maybe:
		return nullptr;
	}
	panic("unreachable id_from_tb");
	return "";
}

namespace types {
	auto type_true = type_id(make_iid("true"));
	auto type_false = type_id(make_iid("false"));
	auto truthy_id = make_iid("Truthy");
	auto falsey_id = make_iid("Falsey");
	auto type_truthy_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(truthy_id)));
	auto type_falsey_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(falsey_id)));

	bool type_t::eval_predicate(type_builtins_t tb, env_t::ref _env) const {
		env_t::ref env = (_env == nullptr) ? _empty_env : _env;
		debug_above(9, log("%s receiving eval_predicate(%s, ..., ...)",
					str().c_str(), tbstr(tb)));

		if (const char *id = id_from_tb(tb)) {
			/* if this predicate is just a simple id check... */
			return is_type_id(shared_from_this(), id, env);
		}

		auto predicate = type_operator(type_id(make_iid(tbstr(tb))), shared_from_this());
		auto result = predicate->eval_core(env, false);
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

	type_t::ref type_t::eval_core(env_t::ref env, bool get_structural_type) const {
		return shared_from_this();
	}

	type_t::ref type_eq_t::eval_core(env_t::ref env, bool get_structural_env) const {
		auto lhs_eval = lhs->eval_core(env, get_structural_env);
		auto rhs_eval = rhs->eval_core(env, get_structural_env);

		return (lhs_eval->repr() == rhs_eval->repr()) ? type_true : type_false;
	}

	type_t::ref type_function_closure_t::eval_core(env_t::ref env, bool get_structural_type) const {
		auto new_func = function->eval_core(env, get_structural_type);
		if (get_structural_type) {
			return type_operator(type_id(make_iid("__closure_t")), new_func)->eval_core(env, get_structural_type);
		}
		if (new_func != function) {
			return ::type_function_closure(new_func);
		}

		return shared_from_this();
	}

	type_t::ref type_t::eval(env_t::ref env, bool get_structural_type) const {
		auto res = eval_core(env, get_structural_type);
		debug_above(10, log("eval(%s, %s) -> %s",
					str().c_str(),
				   	boolstr(get_structural_type),
					res->str().c_str()));
		return res;
	}

	type_t::ref type_id_t::eval_core(env_t::ref env, bool get_structural_type) const {
		static int depth = 0;
		depth_guard_t depth_guard(id->get_location(), depth, 4);

		auto type = env->get_type(id->get_name(), get_structural_type);
		if (type != nullptr && type != shared_from_this() && type->repr() != repr() /*hack?*/) {
			return type->eval_core(env, get_structural_type);
		}

		return shared_from_this();
	}

	type_t::ref type_eval_not(types::type_t::ref operand, env_t::ref env) {
		auto operand_ = operand->eval_core(env, false);
		if (is_type_id(operand_, FALSE_TYPE, nullptr)) {
			return type_true;
		} else if (is_type_id(operand_, TRUE_TYPE, nullptr)) {
			return type_false;
		} else {
			return nullptr;
		}
	}

	type_t::ref type_eval_is_gc(types::type_t::ref operand, env_t::ref env) {
		return operand->is_managed_ptr(env) ? type_true : type_false;
	}

	template <typename T>
	type_t::ref type_eval_is_type(types::type_t::ref operand, env_t::ref env) {
		if (dyncast<const T>(operand->eval(env))) {
			return type_true;
		} else {
			return type_false;
		}
	}

	template <typename T, typename U>
	type_t::ref type_eval_is_type_in_pair(types::type_t::ref operand, env_t::ref env) {
		if (dyncast<const T>(operand->eval(env))) {
			return type_true;
		} else if (dyncast<const U>(operand->eval(env))) {
			return type_true;
		} else {
			return type_false;
		}
	}

	type_t::ref type_eval_is_unit(types::type_t::ref operand, env_t::ref env) {
		if (auto tuple_type = dyncast<const type_tuple_t>(operand->eval(env))) {
            return (tuple_type->dimensions.size() == 0) ? type_true : type_false;
		} else {
			return type_false;
		}
	}

#define type_eval_is_(z, Z) \
	type_t::ref type_eval_is_##z(types::type_t::ref operand, env_t::ref env) { \
		if (auto id_type = dyncast<const types::type_id_t>(operand->eval(env))) { \
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
	type_eval_is_(bottom, BOTTOM_TYPE)

	type_t::ref type_eval_is_bool(types::type_t::ref operand, env_t::ref env) {
		if (auto id_type = dyncast<const types::type_id_t>(operand->eval(env))) {
			std::string name = id_type->id->get_name();
			if (name == BOOL_TYPE || name == TRUE_TYPE || name == FALSE_TYPE) {
				return type_true;
			}
		}
		return type_false;
	}

	type_t::ref type_eval_is_int(types::type_t::ref operand, env_t::ref env) {
		debug_above(7, log("type_eval_is_int on %s = %s = %s",
			   	operand->str().c_str(),
			   	operand->eval(env)->str().c_str(),
			   	operand->eval(env, true)->str().c_str()));
		if (auto id_type = dyncast<const types::type_id_t>(operand->eval(env))) {
			std::string name = id_type->id->get_name();
			if (name == INT_TYPE) {
				return type_true;
			}
		} else if (auto int_type = dyncast<const types::type_integer_t>(operand->eval(env))) {
			return type_true;
		}
		return type_false;
	}

	type_t::ref type_eval_if(types::type_t::ref operand, env_t::ref env) {
		auto operand_ = operand->eval_core(env, false);
		if (is_type_id(operand_, TRUE_TYPE, nullptr)) {
			return type_truthy_lambda;
		} else if (is_type_id(operand_, FALSE_TYPE, nullptr)) {
			return type_falsey_lambda;
		} else {
			return nullptr;
		}
	}

	type_t::ref eval_builtin_func(
			std::string function_name,
			type_t::ref (*fn)(types::type_t::ref, env_t::ref env),
			type_t::ref value,
			env_t::ref env,
			type_t::ref default_value)
	{
		auto result = fn(value, env);
		if (result != nullptr) {
			return result;
		} else {
			log(log_warning, "unable to compute function " c_id("%s") " for %s",
				   	function_name.c_str(),
				   	default_value->str().c_str());
			assert(false);
			return default_value;
		}
	}

	type_t::ref type_function_t::eval_core(env_t::ref env, bool get_structural_type) const {
		type_t::ref new_args = args->eval_core(env, get_structural_type);
		type_t::ref new_return_type = return_type->eval_core(env, get_structural_type);
		if (new_args != args || new_return_type != return_type) {
			return ::type_function(get_location(), type_constraints, new_args, new_return_type);
		}
		return shared_from_this();
	}

	type_t::ref type_data_t::eval_core(env_t::ref env, bool get_structural_env) const {
		return shared_from_this();
	}

	type_t::ref type_operator_t::eval_core(env_t::ref env, bool get_structural_type) const {
		auto oper_ = oper->eval_core(env, get_structural_type);

		static struct {
			const std::string function_name;
			type_t::ref (*type_eval)(types::type_t::ref, env_t::ref env);
		} builtin_functions[] = {
			{TYPE_OP_NOT, type_eval_not},
			{TYPE_OP_GC, type_eval_is_gc},
			{TYPE_OP_IF, type_eval_if},
			{TYPE_OP_IS_REF, type_eval_is_type<type_ref_t>},
			{TYPE_OP_IS_TRUE, type_eval_is_true},
			{TYPE_OP_IS_FALSE, type_eval_is_false},
			{TYPE_OP_IS_BOOL, type_eval_is_bool},
			{TYPE_OP_IS_INT, type_eval_is_int},
			{TYPE_OP_IS_POINTER, type_eval_is_type<type_ptr_t>},
			{TYPE_OP_IS_FUNCTION, type_eval_is_type<type_function_t>},
			{TYPE_OP_IS_CALLABLE, type_eval_is_type_in_pair<type_function_t, type_function_closure_t>},
			{TYPE_OP_IS_VOID, type_eval_is_void},
			{TYPE_OP_IS_BOTTOM, type_eval_is_bottom},
			{TYPE_OP_IS_UNIT, type_eval_is_unit},
			{TYPE_OP_IS_NULL, type_eval_is_null},
			{TYPE_OP_IS_MAYBE, type_eval_is_type<type_maybe_t>},
		};

		if (auto lambda = dyncast<const type_lambda_t>(oper_)) {
			auto var_name = lambda->binding->get_name();
			return lambda->body->rebind({{var_name, operand}})->eval_core(env, get_structural_type);
		}

		for (size_t i=0; i<sizeof(builtin_functions)/sizeof(builtin_functions[0]); ++i) {
			if (is_type_id(oper_, builtin_functions[i].function_name, nullptr)) {
				return eval_builtin_func(
						builtin_functions[i].function_name,
						builtin_functions[i].type_eval,
						operand,
						env,
						shared_from_this());
			}
		}

		return shared_from_this();
	}

	type_t::ref type_subtype_t::eval_core(env_t::ref env, bool get_structural_type) const {
		if (unifies(rhs, lhs, env)) {
			return type_true;
		} else {
			return type_false;
		}
	}

	type_t::ref type_and_t::eval_core(env_t::ref env, bool get_structural_type) const {
		for (const auto &term : terms) {
			auto evaled = term->eval(env, get_structural_type);
			if (auto id = dyncast<const type_id_t>(evaled)) {
				if (id->id->get_name() == TRUE_TYPE) {
					continue;
				} else if (id->id->get_name() == FALSE_TYPE) {
					return type_false;
				}
			}
			auto error = user_error(term->get_location(), "term %s does not evaluate to true or false (%s -> %s)",
					term->str().c_str(), evaled->str().c_str());
			error.add_info(location, "while evaluating %s", str().c_str());
			throw error;
		}
		return type_true;
	}

	type_t::ref type_ptr_t::eval_core(env_t::ref env, bool get_structural_type) const {
		auto expansion = element_type->eval_core(env, get_structural_type);
		if (expansion != element_type) {
			return type_ptr(expansion);
		} else {
			return shared_from_this();
		}
	}

	type_t::ref type_ref_t::eval_core(env_t::ref env, bool get_structural_type) const {
		auto expansion = element_type->eval_core(env, get_structural_type);
		if (expansion != element_type) {
			return type_ref(expansion);
		} else {
			return shared_from_this();
		}
	}

	type_t::ref type_maybe_t::eval_core(env_t::ref env, bool get_structural_type) const {
		auto expansion = just->eval_core(env, get_structural_type);
		if (expansion != just) {
			return type_maybe(expansion, env);
		} else {
			return shared_from_this();
		}
	}

	type_t::ref type_managed_t::eval_core(env_t::ref env, bool get_structural_type) const {
		return shared_from_this();
	}

	type_t::ref type_struct_t::eval_core(env_t::ref env, bool get_structural_type) const {
		return shared_from_this();
	}

	type_t::ref type_lambda_t::eval_core(env_t::ref env, bool get_structural_type) const {
		auto new_body = body->eval_core(env, get_structural_type);
		if (new_body != body) {
			return ::type_lambda(binding, body);
		}
		return shared_from_this();
	}

	type_t::ref type_tuple_t::eval_core(env_t::ref env, bool get_structural_type) const {
		return shared_from_this();
	}

	type_t::ref type_args_t::eval_core(env_t::ref env, bool get_structural_type) const {
		type_t::refs new_args;
		new_args.reserve(args.size());

		bool new_found = false;
		for (auto &arg : args) {
			auto new_arg = arg->eval_core(env, get_structural_type);
			if (new_arg != arg) {
				new_found = true;
			}
			new_args.push_back(new_arg);
			debug_above(10, log("eval'd arg %s -> %s", arg->str().c_str(), new_arg->str().c_str()));
		}

		if (new_found) {
			return ::type_args(new_args, names);
		}
		return shared_from_this();
	}

	type_t::ref type_integer_t::eval_core(env_t::ref env, bool get_structural_type) const {
		auto new_bit_size = bit_size->eval_core(env, get_structural_type);
		auto new_signed = signed_->eval_core(env, get_structural_type);
		if (new_bit_size != bit_size || new_signed != signed_) {
			return ::type_integer(new_bit_size, new_signed);
		}
		return shared_from_this();
	}

	type_t::ref type_injection_t::eval_core(env_t::ref env, bool get_structural_type) const {
		auto new_module_type = module_type->eval_core(env, get_structural_type);
		if (new_module_type != module_type) {
			return ::type_injection(new_module_type);
		}
		return shared_from_this();
	}

	type_t::ref type_extern_t::eval_core(env_t::ref env, bool get_structural_type) const {
		return shared_from_this();
	}
}
