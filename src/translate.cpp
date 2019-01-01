#include "translate.h"
#include <unordered_set>

using namespace bitter;

expr_t *texpr(
		bitter::expr_t *expr,
		const std::unordered_set<std::string> &bound_vars,
		const std::function<types::type_t::ref (bitter::expr_t *)> &get_type,
		std::unordered_map<bitter::expr_t *, types::type_t::ref> typing,
		std::list<defn_id_t> &needed_defns)
{
	/* the job of this function is to create a new ast that is constrained to monomorphically typed
	 * nodes */
	auto type = get_type(expr);
	log("monomorphizing %s to have type %s", expr->str().c_str(), type->str().c_str());
#if 0
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
		auto t1 = env.lookup_env(var->id);
		debug_above(8, log("instance of %s :: %s", var->id.str().c_str(), t1->str().c_str()));
		return t1;
	} else if (auto lambda = dcast<lambda_t*>(expr)) {
		auto operator_type = safe_dyncast<const types::type_operator_t>(type);
		auto tv = lambda->param_type != nullptr ? lambda->param_type : type_variable(lambda->var.location);
		auto return_type = type_variable(lambda->var.location);
		auto local_env = env_t{env};
		local_env.return_type = return_type;
		local_env.extend(lambda->var, scheme({}, {}, tv), true /*allow_subscoping*/);
		auto body_type = infer(lambda->body, local_env, constraints);
		if (lambda->return_type != nullptr) {
			append(
					constraints,
					return_type,
					lambda->return_type,
					{string_format("return type does not match type annotation :: %s", lambda->return_type->str().c_str()),
					lambda->return_type->get_location()});
		}
		return type_arrow(lambda->get_location(), tv, return_type);
	} else if (auto application = dcast<application_t*>(expr)) {
		auto t1 = infer(application->a, env, constraints);
		auto t2 = infer(application->b, env, constraints);
		auto tv = type_variable(expr->get_location());
		append(constraints, t1, type_arrow(application->get_location(), t2, tv),
				{string_format("(%s :: %s) applied to (%s :: %s) results in type %s",
						application->a->str().c_str(),
						t1->str().c_str(),
						application->b->str().c_str(),
						t2->str().c_str(),
						tv->str().c_str()),
				application->get_location()});
		return tv;
	} else if (auto let = dcast<let_t*>(expr)) {
		constraints_t local_constraints;
		auto t1 = infer(let->value, env, local_constraints);
		auto tracked_types = std::make_shared<std::unordered_map<bitter::expr_t *, types::type_t::ref>>();
		env_t local_env{{} /*map*/, nullptr /*return_type*/, {} /*instance_requirements*/, tracked_types};

		auto bindings = solver({}, local_constraints, local_env);
		auto schema = scheme({}, {}, t1);
		for (auto pair : *tracked_types) {
			env.track(pair.first, pair.second);
		}
		for (auto constraint: local_constraints) {
			constraints.push_back(constraint);
		}
		auto body_env = env_t{env};
		body_env.extend(let->var, schema, true /*allow_subscoping*/);
		auto t2 = infer(let->body, body_env, constraints)->rebind(bindings);
		log("the let variable is %s :: %s and the body is %s :: %s",
				let->var.str().c_str(),
				schema->str().c_str(),
				let->body->str().c_str(),
				t2->str().c_str());
		return t2;
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
				// append(constraints, t1, type_unit(block->get_location()), {"statements must return unit type", block->get_location()});
			}
		}
		return type_unit(block->get_location());
	} else if (auto return_ = dcast<return_statement_t*>(expr)) {
		auto t1 = infer(return_->value, env, constraints);
		append(constraints, t1, env.return_type,
				{
				string_format(
						"returning (%s " c_good("::") " %s and %s)",
						return_->value->str().c_str(), t1->str().c_str(), env.return_type->str().c_str()), 
				return_->get_location()});
		return type_bottom();
	} else if (auto tuple = dcast<tuple_t*>(expr)) {
		std::vector<types::type_t::ref> dimensions;
		for (auto dim : tuple->dims) {
			dimensions.push_back(infer(dim, env, constraints));
		}
		return type_tuple(dimensions);
	} else if (auto as = dcast<as_t*>(expr)) {
		auto t1 = infer(as->expr, env, constraints);
		if (!as->force_cast) {
			append(constraints, t1, as->type, {string_format("we can get type %s from %s",
					   	as->type->str().c_str(),
					   	as->expr->str().c_str()), as->get_location()});
		}
		return as->type;
	}
#endif
	return nullptr;
}

translation_t::ref translate(
		bitter::expr_t *expr,
		const std::unordered_set<std::string> &bound_vars,
	   	const std::function<types::type_t::ref (bitter::expr_t *)> &get_type,
		std::list<defn_id_t> &needed_defns)
{
	std::unordered_map<bitter::expr_t *, types::type_t::ref> typing;
	expr_t *translated_expr = texpr(expr, bound_vars, get_type, typing, needed_defns);
	return std::make_shared<translation_t>(translated_expr, typing);
}

std::string translation_t::str() const {
	return "<translation...>";
}

location_t translation_t::get_location() const {
	return expr->get_location();
}
