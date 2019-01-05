#include "translate.h"
#include <unordered_set>
#include "patterns.h"

using namespace bitter;

expr_t *texpr(
		const defn_id_t &for_defn_id,
		bitter::expr_t *expr,
		const std::unordered_set<std::string> &bound_vars,
		const std::function<types::type_t::ref (bitter::expr_t *)> &get_type,
		std::unordered_map<bitter::expr_t *, types::type_t::ref> &typing,
		std::set<defn_id_t> &needed_defns)
{
	/* the job of this function is to create a new ast that is constrained to monomorphically typed
	 * nodes */
	auto type = get_type(expr);
	debug_above(6, log("monomorphizing %s to have type %s", expr->str().c_str(), type->str().c_str()));
	if (auto literal = dcast<literal_t *>(expr)) {
		typing[literal] = type;
		return literal;
	} else if (auto var = dcast<var_t*>(expr)) {
		typing[var] = type;
		if (!in(var->id.name, bound_vars)) {
			auto defn_id = defn_id_t{var->id, type->generalize({})->normalize()};
			debug_above(6, log(c_id("%s") " depends on " c_id("%s"), for_defn_id.str().c_str(), defn_id.str().c_str()));
			needed_defns.insert(defn_id);
		}
		return var;
	} else if (auto lambda = dcast<lambda_t*>(expr)) {
		auto operator_type = safe_dyncast<const types::type_operator_t>(type);
		auto new_bound_vars = bound_vars;
		new_bound_vars.insert(lambda->var.name);
		auto new_body = texpr(
				for_defn_id,
				lambda->body,
				new_bound_vars,
				get_type,
				typing,
				needed_defns);
		auto new_lambda = new lambda_t(lambda->var, nullptr, nullptr, new_body);
		typing[new_lambda] = type;
		return new_lambda;
	} else if (auto application = dcast<application_t*>(expr)) {
		auto a = texpr(
				for_defn_id,
				application->a,
				bound_vars,
				get_type,
				typing,
				needed_defns);
		auto b = texpr(
				for_defn_id,
				application->b,
				bound_vars,
				get_type,
				typing,
				needed_defns);
		auto new_app = new application_t(a, b);
		typing[new_app] = type;
		return new_app;
	} else if (auto let = dcast<let_t*>(expr)) {
		assert(false);
	} else if (auto fix = dcast<fix_t*>(expr)) {
		assert(false);
	} else if (auto condition = dcast<conditional_t*>(expr)) {
		auto cond = texpr(
				for_defn_id,
				condition->cond,
				bound_vars,
				get_type,
				typing,
				needed_defns);
		auto truthy = texpr(
				for_defn_id,
				condition->truthy,
				bound_vars,
				get_type,
				typing,
				needed_defns);
		auto falsey = texpr(
				for_defn_id,
				condition->falsey,
				bound_vars,
				get_type,
				typing,
				needed_defns);
		auto new_conditional = new conditional_t(cond, truthy, falsey);
		typing[new_conditional] = type;
		return new_conditional;
	} else if (auto block = dcast<block_t*>(expr)) {
		assert(false);
	} else if (auto return_ = dcast<return_statement_t*>(expr)) {
		return new return_statement_t(
				texpr(
					for_defn_id,
					return_->value,
					bound_vars,
					get_type,
					typing,
					needed_defns));
	} else if (auto tuple = dcast<tuple_t*>(expr)) {
		std::vector<expr_t *> dims;
		for (auto dim : tuple->dims) {
			dims.push_back(texpr(
						for_defn_id,
						dim,
						bound_vars,
						get_type,
						typing,
						needed_defns));
		}
		auto new_tuple = new tuple_t(tuple->get_location(), dims);
		typing[new_tuple] = type;
		return new_tuple;
	} else if (auto match = dcast<match_t*>(expr)) {
		return translate_match_expr(
				for_defn_id,
				match,
				bound_vars,
				get_type,
				typing,
				needed_defns);
	} else if (auto as = dcast<as_t*>(expr)) {
		auto expr = texpr(
				for_defn_id,
				as->expr,
				bound_vars,
				get_type,
				typing,
				needed_defns);
		if (as->force_cast) {
			auto new_as = new as_t(expr, type, true /*force_cast*/);
			typing[new_as] = type;
			return new_as;
		} else {
			/* eliminate non-forceful casts */
			return texpr(
					for_defn_id,
					as->expr,
					bound_vars,
					get_type,
					typing,
					needed_defns);
		}
	}
	assert(false);
	return nullptr;
}

translation_t::ref translate(
		const defn_id_t &for_defn_id,
		bitter::expr_t *expr,
		const std::unordered_set<std::string> &bound_vars,
	   	const std::function<types::type_t::ref (bitter::expr_t *)> &get_type,
		std::set<defn_id_t> &needed_defns)
{
	std::unordered_map<bitter::expr_t *, types::type_t::ref> typing;
	expr_t *translated_expr = texpr(
				for_defn_id,
				expr,
				bound_vars,
				get_type,
				typing,
				needed_defns);
	return std::make_shared<translation_t>(translated_expr, typing);
}

std::string translation_t::str() const {
	return string_format("%s :: %s", expr->str().c_str(), get(typing, expr, {})->str().c_str());
}

location_t translation_t::get_location() const {
	return expr->get_location();
}
