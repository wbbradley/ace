#include "infer.h"
#include "ast.h"
#include "builtins.h"
#include "user_error.h"
#include "env.h"

using namespace bitter;

void append(constraints_t &constraints, types::type_t::ref a, types::type_t::ref b, constraint_info_t info) {
	debug_above(6, log("constraining %s to %s", a->str().c_str(), b->str().c_str()));
	assert(a != nullptr);
	assert(b != nullptr);
	constraints.push_back({a, b, info});
}

types::type_t::ref infer(
		bitter::expr_t *expr,
		env_t::ref env,
	   	constraints_t &constraints)
{
	if (auto literal = dcast<literal_t *>(expr)) {
		switch (literal->token.tk) {
		case tk_integer:
			return type_id(identifier_t{INT_TYPE, literal->token.location});
		case tk_float:
			return type_id(identifier_t{FLOAT_TYPE, literal->token.location});
		case tk_string:
			return type_id(identifier_t{STR_TYPE, literal->token.location});
		case tk_char:
			return type_id(identifier_t{CHAR_TYPE, literal->token.location});
		default:
			throw user_error(literal->token.location, "unsupported type of literal");
		}
	} else if (auto var = dcast<var_t*>(expr)) {
		return env.lookup_env(var->id);
	} else if (auto lambda = dcast<lambda_t*>(expr)) {
		auto tv = type_variable(lambda->var.location);
		auto return_type = type_variable(lambda->var.location);
		auto local_env = env.extend(lambda->var, return_type, forall({}, tv));
		auto body_type = infer(lambda->body, local_env, constraints);
		// append(constraints, return_type, body_type, {"return types must match return statements", lambda->get_location()});
		return type_arrow(lambda->get_location(), tv, return_type);
	} else if (auto application = dcast<application_t*>(expr)) {
		auto t1 = infer(application->a, env, constraints);
		auto t2 = infer(application->b, env, constraints);
		auto tv = type_variable(expr->get_location());
		append(constraints, t1, type_arrow(application->get_location(), t2, tv), {"function calls must match types", application->get_location()});
		return tv;
	} else if (auto let = dcast<let_t*>(expr)) {
		return infer(
				let->body, 
				env.extend(
					let->var,
					infer(let->value, env, constraints)->generalize(env)),
				constraints);
	} else if (auto fix = dcast<fix_t*>(expr)) {
		auto tv = type_variable(fix->get_location());
		append(constraints, type_arrow(fix->get_location(), tv, tv), infer(fix->f, env, constraints), {"fixpoint", fix->get_location()});
		return tv;
	} else if (auto condition = dcast<conditional_t*>(expr)) {
		auto t1 = infer(condition->cond, env, constraints);
		auto t2 = infer(condition->truthy, env, constraints);
		auto t3 = infer(condition->falsey, env, constraints);
		append(constraints, t1, type_bool(condition->cond->get_location()), {"conditions must be bool", condition->get_location()});
		append(constraints, t2, t3, {"both branches of conditionals must match types with each other", condition->falsey->get_location()});
		return t2;
	} else if (auto block = dcast<block_t*>(expr)) {
		for (int i=0; i<block->statements.size(); ++i) {
			auto expr = block->statements[i];
			auto t1 = infer(expr, env, constraints);
			if (auto return_statement = dcast<return_statement_t*>(expr)) {
				if (i != block->statements.size()-1) {
					if (auto return_statement = dcast<return_statement_t*>(expr)) {
						throw user_error(return_statement->get_location(), "there are statements after a return statement");
					}
				}
			} else {
				append(constraints, t1, type_unit(block->get_location()), {"statements must return unit type", block->get_location()});
			}
		}
		return type_unit(block->get_location());
	} else if (auto return_ = dcast<return_statement_t*>(expr)) {
		auto t1 = infer(return_->value, env, constraints);
		append(constraints, t1, env.return_type, {"return type expressions must match the type of the containing lambda", return_->get_location()});
		return type_unit(return_->get_location());
	}

	throw user_error(expr->get_location(), "unhandled inference for %s",
			expr->str().c_str());
}

std::string constraint_info_t::str() const {
	return string_format("%s at %s", reason.c_str(), location.str().c_str());
}

constraint_t constraint_t::rebind(const types::type_t::map &env) const {
	return {a->rebind(env), b->rebind(env), info};
}
