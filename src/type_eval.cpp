#include "types.h"
#include "dbg.h"
#include "unification.h"
#include "ast.h"
#include "user_error.h"
#include "env.h"

namespace types {
	auto type_true = type_id(make_iid("true"));
	auto type_false = type_id(make_iid("false"));

	type_t::ref type_t::eval_core(env_t::ref env) const {
		return shared_from_this();
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

		auto type = env.maybe_lookup_env(id);
		if (type != nullptr && type != shared_from_this() && type->repr() != repr() /*hack?*/) {
			return type->eval_core(env);
		}

		return shared_from_this();
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
}
