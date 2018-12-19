#include "types.h"
#include "dbg.h"
#include "unification.h"
#include "ast.h"
#include "user_error.h"

namespace types {
	auto type_true = type_id(make_iid("true"));
	auto type_false = type_id(make_iid("false"));
	auto truthy_id = make_iid("Truthy");
	auto falsey_id = make_iid("Falsey");
	auto type_truthy_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(truthy_id)));
	auto type_falsey_lambda = type_lambda(truthy_id, type_lambda(falsey_id, type_variable(falsey_id)));

	type_t::ref type_t::eval_core(env_t::ref env) const {
		return shared_from_this();
	}

	type_t::ref type_eq_t::eval_core(env_t::ref env) const {
		auto lhs_eval = lhs->eval_core(env);
		auto rhs_eval = rhs->eval_core(env);

		return (lhs_eval->repr() == rhs_eval->repr()) ? type_true : type_false;
	}

	type_t::ref type_t::eval(env_t::ref env) const {
		auto res = eval_core(env);
		debug_above(10, log("eval(%s) -> %s",
					str().c_str(),
					res->str().c_str()));
		return res;
	}

	type_t::ref type_id_t::eval_core(env_t::ref env) const {
		static int depth = 0;
		depth_guard_t depth_guard(id.location, depth, 4);

		auto type = env.lookup_env(id);
		if (type != nullptr && type != shared_from_this() && type->repr() != repr() /*hack?*/) {
			return type->eval_core(env);
		}

		return shared_from_this();
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
			if (id_type->id.name == Z) { \
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
			std::string name = id_type->id.name;
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
			   	operand->eval(env)->str().c_str()));
		if (auto id_type = dyncast<const types::type_id_t>(operand->eval(env))) {
			std::string name = id_type->id.name;
			if (name == INT_TYPE) {
				return type_true;
			}
		} else if (auto int_type = dyncast<const types::type_integer_t>(operand->eval(env))) {
			return type_true;
		}
		return type_false;
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

	type_t::ref type_data_t::eval_core(env_t::ref env) const {
		return shared_from_this();
	}

	type_t::ref type_operator_t::eval_core(env_t::ref env) const {
		auto oper_ = oper->eval_core(env);

		if (auto lambda = dyncast<const type_lambda_t>(oper_)) {
			auto var_name = lambda->binding.name;
			return lambda->body->rebind({{var_name, operand}})->eval_core(env);
		}

		return shared_from_this();
	}

	type_t::ref type_subtype_t::eval_core(env_t::ref env) const {
		if (unifies(rhs, lhs, env)) {
			return type_true;
		} else {
			return type_false;
		}
	}

	type_t::ref type_and_t::eval_core(env_t::ref env) const {
		for (const auto &term : terms) {
			auto evaled = term->eval(env);
			if (auto id = dyncast<const type_id_t>(evaled)) {
				if (id->id.name == TRUE_TYPE) {
					continue;
				} else if (id->id.name == FALSE_TYPE) {
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

	type_t::ref type_ptr_t::eval_core(env_t::ref env) const {
		auto expansion = element_type->eval_core(env);
		if (expansion != element_type) {
			return type_ptr(expansion);
		} else {
			return shared_from_this();
		}
	}

	type_t::ref type_ref_t::eval_core(env_t::ref env) const {
		auto expansion = element_type->eval_core(env);
		if (expansion != element_type) {
			return type_ref(expansion);
		} else {
			return shared_from_this();
		}
	}

	type_t::ref type_managed_t::eval_core(env_t::ref env) const {
		return shared_from_this();
	}

	type_t::ref type_struct_t::eval_core(env_t::ref env) const {
		return shared_from_this();
	}

	type_t::ref type_lambda_t::eval_core(env_t::ref env) const {
		auto new_body = body->eval_core(env);
		if (new_body != body) {
			return ::type_lambda(binding, body);
		}
		return shared_from_this();
	}

	type_t::ref type_tuple_t::eval_core(env_t::ref env) const {
		return shared_from_this();
	}

	type_t::ref type_args_t::eval_core(env_t::ref env) const {
		type_t::refs new_args;
		new_args.reserve(args.size());

		bool new_found = false;
		for (auto &arg : args) {
			auto new_arg = arg->eval_core(env);
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

	type_t::ref type_integer_t::eval_core(env_t::ref env) const {
		auto new_bit_size = bit_size->eval_core(env);
		auto new_signed = signed_->eval_core(env);
		if (new_bit_size != bit_size || new_signed != signed_) {
			return ::type_integer(new_bit_size, new_signed);
		}
		return shared_from_this();
	}

	type_t::ref type_injection_t::eval_core(env_t::ref env) const {
		auto new_module_type = module_type->eval_core(env);
		if (new_module_type != module_type) {
			return ::type_injection(new_module_type);
		}
		return shared_from_this();
	}

	type_t::ref type_extern_t::eval_core(env_t::ref env) const {
		return shared_from_this();
	}
}
