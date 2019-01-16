#include "ast.h"
#include "types.h"
#include "gen.h"
#include "ptr.h"
#include "user_error.h"

namespace gen {
	struct free_vars_t {
		std::set<defn_id_t> defn_ids;
		int count() const {
			return defn_ids.size();
		}
		void add(identifier_t id, types::type_t::ref type) {
			assert(type != nullptr);
			defn_id_t defn_id{id, type->generalize({})->normalize()};
			defn_ids.insert(defn_id);
		}
	};

	void get_free_vars(const bitter::expr_t *expr, const tracked_types_t &typing, const std::unordered_set<std::string> &bindings, free_vars_t &free_vars) {
		debug_above(8, log("get_free_vars(%s, {%s}, ...)", expr->str().c_str(), join(bindings, ", ").c_str()));
		if (auto literal = dcast<const literal_t *>(expr)) {
		} else if (auto static_print = dcast<const bitter::static_print_t*>(expr)) {
		} else if (auto var = dcast<const bitter::var_t*>(expr)) {
			if (!in(var->id.name, bindings)) {
				free_vars.add(var->id, get(typing, expr, {}));
			}
		} else if (auto lambda = dcast<const bitter::lambda_t*>(expr)) {
			auto new_bindings = bindings;
			new_bindings.insert(lambda->var.name);
			get_free_vars(lambda->body, typing, new_bindings, free_vars);
		} else if (auto application = dcast<const bitter::application_t*>(expr)) {
			get_free_vars(application->a, typing, bindings, free_vars);
			get_free_vars(application->b, typing, bindings, free_vars);
		} else if (auto let = dcast<const bitter::let_t*>(expr)) {
			// TODO: allow let-rec
			get_free_vars(let->value, typing, bindings, free_vars);
			auto new_bound_vars = bindings;
			new_bound_vars.insert(let->var.name);
			get_free_vars(let->body, typing, new_bound_vars, free_vars);
		} else if (auto fix = dcast<const bitter::fix_t*>(expr)) {
			get_free_vars(fix->f, typing, bindings, free_vars);
		} else if (auto condition = dcast<const bitter::conditional_t*>(expr)) {
			get_free_vars(condition->cond, typing, bindings, free_vars);
			get_free_vars(condition->truthy, typing, bindings, free_vars);
			get_free_vars(condition->falsey, typing, bindings, free_vars);
		} else if (auto break_ = dcast<const bitter::break_t*>(expr)) {
		} else if (auto while_ = dcast<const bitter::while_t*>(expr)) {
			get_free_vars(while_->condition, typing, bindings, free_vars);
			get_free_vars(while_->block, typing, bindings, free_vars);
		} else if (auto block = dcast<const bitter::block_t*>(expr)) {
			for (auto statement: block->statements) {
				get_free_vars(statement, typing, bindings, free_vars);
			}
		} else if (auto return_ = dcast<const bitter::return_statement_t*>(expr)) {
			get_free_vars(return_->value, typing, bindings, free_vars);
		} else if (auto tuple = dcast<const bitter::tuple_t*>(expr)) {
			for (auto dim: tuple->dims) {
				get_free_vars(dim, typing, bindings, free_vars);
			}
		} else if (auto tuple_deref = dcast<const bitter::tuple_deref_t*>(expr)) {
			get_free_vars(tuple_deref->expr, typing, bindings, free_vars);
		} else if (auto as = dcast<const bitter::as_t*>(expr)) {
			get_free_vars(as->expr, typing, bindings, free_vars);
		} else if (auto sizeof_ = dcast<const bitter::sizeof_t*>(expr)) {
		} else if (auto match = dcast<const bitter::match_t*>(expr)) {
			get_free_vars(match->scrutinee, typing, bindings, free_vars);
			for (auto pattern_block: match->pattern_blocks) {
				auto new_bindings = bindings;
				pattern_block->predicate->get_bound_vars(new_bindings);
				get_free_vars(pattern_block->result, typing, new_bindings, free_vars);
			}
		} else {
			assert(false);
		}
	}


	block_t::ref gen_block(builder_t &builder, const bitter::expr_t *expr, const tracked_types_t &typing, env_t &cenv) {
		debug_above(8, log("block(%s, ..., ...)", expr->str().c_str()));
		if (auto literal = dcast<const literal_t *>(expr)) {
			throw user_error(expr->get_location(), "literal %s has no effect here", expr->str().c_str());
		} else if (auto static_print = dcast<const bitter::static_print_t*>(expr)) {
			assert(false);
		} else if (auto var = dcast<const bitter::var_t*>(expr)) {
			throw user_error(expr->get_location(), "variable reference %s has no effect here", expr->str().c_str());
		} else if (auto lambda = dcast<const bitter::lambda_t*>(expr)) {
			throw user_error(expr->get_location(), "function %s has no effect here", expr->str().c_str());
		} else if (auto application = dcast<const bitter::application_t*>(expr)) {
			/*
			 * a b ==> eval b then eval a then create a call instruction. so we need a three subgraphs
			 * where the b eval subgraph falls into a Phi node (potentially) in the a eval subgraph then */
		} else if (auto let = dcast<const bitter::let_t*>(expr)) {
		} else if (auto fix = dcast<const bitter::fix_t*>(expr)) {
		} else if (auto condition = dcast<const bitter::conditional_t*>(expr)) {
		} else if (auto break_ = dcast<const bitter::break_t*>(expr)) {
		} else if (auto while_ = dcast<const bitter::while_t*>(expr)) {
		} else if (auto block = dcast<const bitter::block_t*>(expr)) {
		} else if (auto return_ = dcast<const bitter::return_statement_t*>(expr)) {
		} else if (auto tuple = dcast<const bitter::tuple_t*>(expr)) {
		} else if (auto tuple_deref = dcast<const bitter::tuple_deref_t*>(expr)) {
		} else if (auto as = dcast<const bitter::as_t*>(expr)) {
		} else if (auto sizeof_ = dcast<const bitter::sizeof_t*>(expr)) {
		} else if (auto match = dcast<const bitter::match_t*>(expr)) {
		}

		throw user_error(expr->get_location(), "unhandled block gen for %s", expr->str().c_str());
	}

	value_t::ref get_env_var(const env_t &env, identifier_t id) {
		auto iter = env.find(id.name);
		if (iter == env.end()) {
			throw user_error(id.location, "could not find variable %s", id.str().c_str());
		}
		return iter->second;
	}

	value_t::ref gen_literal(const token_t &token, types::type_t::ref type) {
		return std::make_shared<literal_t>(token, type);
	}

	function_t::ref gen_function(std::string name, identifier_t param_id, location_t location, types::type_t::ref type) {
		auto function = std::make_shared<function_t>(name, param_id, location, type);
		auto lambda_type = safe_dyncast<const types::type_operator_t>(type);
		auto param_type = lambda_type->oper;
		auto return_type = lambda_type->operand;
		log("creating gen::function_t %s :: fn (%s %s) %s", name.c_str(), param_id.str().c_str(), param_type->str().c_str(), return_type->str().c_str());
		function->args.push_back(
				std::make_shared<argument_t>(param_id.location, param_type, 0, function));
		return function;
	}

	value_t::ref gen(builder_t &builder, const bitter::expr_t *expr, const tracked_types_t &typing, const env_t &env, const std::unordered_set<std::string> &globals) {
		auto type = get(typing, expr, {});
		assert(type != nullptr);

		debug_above(8, log("gen(..., %s, ..., ...)", expr->str().c_str()));
		if (auto literal = dcast<const literal_t *>(expr)) {
			return gen_literal(literal->token, type);
		} else if (auto static_print = dcast<const bitter::static_print_t*>(expr)) {
			assert(false);
		} else if (auto var = dcast<const bitter::var_t*>(expr)) {
			return get_env_var(env, var->id);
		} else if (auto lambda = dcast<const bitter::lambda_t*>(expr)) {
			auto lambda_type = safe_dyncast<const types::type_operator_t>(type);
			auto param_type = lambda_type->oper;
			auto return_type = lambda_type->operand;

			/* see if we need to lift any free variables into a closure */
			free_vars_t free_vars;
			get_free_vars(lambda->body, typing, globals, free_vars);

			if (free_vars.count() == 0) {
				/* this can be considered a top-level function that takes no closure env */
				auto function = gen_function("", lambda->var, lambda->get_location(), type);

				/* put the param in scope */
				auto new_env = env;
				new_env[lambda->var.name] = function->args[0];

				builder_t builder(function);
				function->blocks.push_back(safe_dyncast<block_t>(
						gen(builder, lambda->body, typing, new_env, globals)));
				return function;
			} else {
				/* this is a closure, and as such requires that we capture the free_vars from our
				 * current environment */

				/* in order to have a current environment, we have to be inside of some sort of scope */
				// auto block = block(gen(lambda->body, typing, env, globals));
				assert(false);
			}
		} else if (auto application = dcast<const bitter::application_t*>(expr)) {
		} else if (auto let = dcast<const bitter::let_t*>(expr)) {
		} else if (auto fix = dcast<const bitter::fix_t*>(expr)) {
		} else if (auto condition = dcast<const bitter::conditional_t*>(expr)) {
		} else if (auto break_ = dcast<const bitter::break_t*>(expr)) {
		} else if (auto while_ = dcast<const bitter::while_t*>(expr)) {
		} else if (auto block = dcast<const bitter::block_t*>(expr)) {
		} else if (auto return_ = dcast<const bitter::return_statement_t*>(expr)) {
		} else if (auto tuple = dcast<const bitter::tuple_t*>(expr)) {
		} else if (auto tuple_deref = dcast<const bitter::tuple_deref_t*>(expr)) {
		} else if (auto as = dcast<const bitter::as_t*>(expr)) {
		} else if (auto sizeof_ = dcast<const bitter::sizeof_t*>(expr)) {
		} else if (auto match = dcast<const bitter::match_t*>(expr)) {
		}

		throw user_error(expr->get_location(), "unhandled ssa-gen for %s :: %s", expr->str().c_str(), type->str().c_str());
	}

	builder_t builder_t::save_ip() const {
		return *this;
	}

	void builder_t::restore_ip(const builder_t &builder) {
		*this = builder;
	}

	std::string load_t::str() const {
		return "(load " + rhs->str() + " :: " + rhs->type->str() + ") " + type->str().c_str();
	}

	std::string store_t::str() const {
		return "store " + rhs->str() + " :: " + rhs->type->str() + " at address " + lhs->str() + " :: " + lhs->type->str();
	}

	std::string literal_t::str() const {
		return token.text + " :: " + type->str().c_str();
	}

	std::string argument_t::str() const {
		return string_format("arg%d :: %s", index, type->str().c_str());
	}

	std::string function_t::str() const {
		auto lambda_type = safe_dyncast<const types::type_operator_t>(type);
		auto param_type = lambda_type->oper;
		auto return_type = lambda_type->operand;

		std::stringstream ss;
		ss << "fn " << name << "(" << param_id.str() << " " << param_type->str() << ") " << return_type->str();
		if (blocks.size() != 0) {
			ss << " {" << std::endl;
			ss << "\tTODO: show blocks" << std::endl;
			ss << "}" << std::endl;
		}
		return ss.str();
	}
}
