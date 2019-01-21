#include "ast.h"
#include "types.h"
#include "gen.h"
#include "ptr.h"
#include "user_error.h"

namespace gen {
	struct ip_guard_t {
		builder_t &builder;
		builder_t::saved_state saved;

		ip_guard_t(builder_t &builder) : builder(builder), saved(builder.save_ip()) {
			saved = builder.save_ip();
		}
		~ip_guard_t() {
			builder.restore_ip(saved);
		}
	};

	types::type_t::ref tuple_type(const std::vector<value_t::ref> &dims) {
		types::type_t::refs terms;
		for (auto dim: dims) {
			terms.push_back(dim->type);
		}
		return type_tuple(terms);
	}

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
		std::string str() const {
			return string_format("{%s}", join(defn_ids, ", ").c_str());
		}
	};

	void get_free_vars(const bitter::expr_t *expr, const tracked_types_t &typing, const std::unordered_set<std::string> &bindings, free_vars_t &free_vars) {
		debug_above(7, log("get_free_vars(%s, {%s}, ...)", expr->str().c_str(), join(bindings, ", ").c_str()));
		if (auto literal = dcast<const bitter::literal_t *>(expr)) {
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
		} else if (auto builtin = dcast<const bitter::builtin_t*>(expr)) {
			for (auto expr: builtin->exprs) {
				get_free_vars(expr, typing, bindings, free_vars);
			}
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


	value_t::ref get_env_var(const env_t &env, identifier_t id, types::scheme_t::ref scheme) {
		auto iter = env.find(id.name);
		if (iter != env.end()) {
			auto value = iter->second;
			if (value == nullptr) {
				throw user_error(id.location, "we need a definition for %s", id.str().c_str());
			}
			return value;
		}
		auto defn_id = defn_id_t{id, scheme};
		iter = env.find(defn_id.repr());
		if (iter == env.end()) {
			return std::make_shared<global_ref_t>(defn_id.repr_id(), scheme->instantiate(INTERNAL_LOC()));
		}
#if 0
			auto error = user_error(id.location, "could not find variable %s", id.str().c_str());
			error.add_info(id.location, "env is\n%s",
					join_with(env, "\n", [](std::pair<std::string, value_t::ref> pair) {
						if (auto function = dyncast<function_t>(pair.second)) {
							std::stringstream ss;
							function->render(ss);
							return ss.str();
						} else {
							return string_format("%s: %s", pair.first.c_str(), pair.second ? pair.second->str().c_str() : "<none>");
						}
						}).c_str());
			throw error;
#endif
		auto value = iter->second;
		if (value == nullptr) {
			throw user_error(id.location, "we need a definition for %s", defn_id.str().c_str());
		}
		return value;
	}

	void set_env_var(env_t &env, std::string name, value_t::ref value) {
		assert(name.size() != 0);
		assert(!in(name, env));
		defn_id_t defn_id{{name, value->get_location()}, value->type->generalize({})->normalize()};
		env[defn_id.repr()] = value;
	}

	function_t::ref builder_t::create_function(
			std::string name,
		   	identifiers_t param_ids,
		   	location_t location,
		   	types::type_t::ref type)
   	{
		auto function = std::make_shared<function_t>(module, name, location, type);
		types::type_t::refs terms;
		unfold_binops_rassoc(ARROW_TYPE_OPERATOR, type, terms);
		assert(terms.size() > param_ids.size());
		for (int i=0; i<param_ids.size(); ++i) {
			auto param_type = terms[0];
			terms.erase(terms.begin());

			log("creating argument %s :: %s for %s",
					param_ids[i].str().c_str(),
					param_type->str().c_str(),
					name.c_str());
			function->args.push_back(
					std::make_shared<argument_t>(param_ids[i], param_type, i, function));
		}

		set_env_var(module->env, name, function);
		return function;
	}

	value_t::ref gen_lambda(
			builder_t &builder,
			const bitter::lambda_t *lambda,
			types::type_t::ref type,
			const tracked_types_t &typing,
			const env_t &env,
			const std::unordered_set<std::string> &globals)
	{
		auto lambda_type = safe_dyncast<const types::type_operator_t>(type);
		auto param_type = lambda_type->oper;
		auto return_type = lambda_type->operand;

		/* see if we need to lift any free variables into a closure */
		free_vars_t free_vars;
		get_free_vars(lambda, typing, globals, free_vars);

		function_t::ref function = builder.create_function(
				bitter::fresh(),
				{lambda->var},
				lambda->get_location(),
				type);

		builder_t new_builder(function);
		new_builder.create_block("entry");

		/* put the param in scope */
		auto new_env = env;
		new_env[lambda->var.name] = function->args.back();

		value_t::ref closure;

		if (free_vars.count() != 0) {
			/* this is a closure, and as such requires that we capture the free_vars from our
			 * current environment */
			log("we need closure by value of %s", free_vars.str().c_str());

			std::vector<value_t::ref> dims;

			/* the closure includes a reference to this function */
			dims.push_back(function);

			for (auto defn_id : free_vars.defn_ids) {
				/* add a copy of each closed over variable */
				dims.push_back(get_env_var(env, defn_id.id, defn_id.scheme));
			}

			closure = builder.create_tuple(lambda->get_location(), dims);

			/* let's add the closure argument to this function */
			function->args.push_back(
					std::make_shared<argument_t>(identifier_t{"closure", INTERNAL_LOC()}, closure->type, 1, function));

			// new_env["__self"] = builder.create_gep(function->args.back(), {0});
			int arg_index = 0;
			for (auto defn_id : free_vars.defn_ids) {
				/* inject the closed over vars into the new environment within the closure */
				auto new_closure_var = new_builder.create_tuple_deref(
						defn_id.id.location,
						function->args.back(),
						arg_index + 1);
				new_env[defn_id.id.name] = new_closure_var;
				++arg_index;
			}
		} else {
			/* this can be considered a top-level function that takes no closure env */
		}

		gen(new_builder, lambda->body, typing, new_env, globals);
		return free_vars.count() != 0 ? closure : function;
	}

	value_t::ref gen(
			builder_t &builder,
			const bitter::expr_t *expr,
			const tracked_types_t &typing,
			const env_t &env,
			const std::unordered_set<std::string> &globals)
	{
		try {
			auto type = get(typing, expr, {});
			if (type == nullptr) {
				log_location(log_error, expr->get_location(), "expression lacks typing %s", expr->str().c_str());
				dbg();
			}

			debug_above(8, log("gen(..., %s, ..., ...)", expr->str().c_str()));
			if (auto literal = dcast<const bitter::literal_t *>(expr)) {
				return builder.create_literal(literal->token, type);
			} else if (auto static_print = dcast<const bitter::static_print_t*>(expr)) {
				assert(false);
			} else if (auto var = dcast<const bitter::var_t*>(expr)) {
				return get_env_var(env, var->id, type->generalize({})->normalize());
			} else if (auto lambda = dcast<const bitter::lambda_t*>(expr)) {
				return gen_lambda(builder, lambda, type, typing, env, globals);
			} else if (auto application = dcast<const bitter::application_t*>(expr)) {
				return builder.create_call(
						gen(builder, application->a, typing, env, globals),
						{gen(builder, application->b, typing, env, globals)});
			} else if (auto let = dcast<const bitter::let_t*>(expr)) {
				assert(false);
			} else if (auto fix = dcast<const bitter::fix_t*>(expr)) {
				assert(false);
			} else if (auto condition = dcast<const bitter::conditional_t*>(expr)) {
				auto cond = gen(builder, condition->cond, typing, env, globals);
				block_t::ref truthy_branch = builder.create_block("truthy", false /*insert_in_new_block*/);
				block_t::ref falsey_branch = builder.create_block("falsey", false /*insert_in_new_block*/);
				block_t::ref merge_branch = builder.create_block("merge", false /*insert_in_new_block*/);

				builder.create_cond_branch(cond, truthy_branch, falsey_branch);

				builder.set_insertion_block(truthy_branch);
				value_t::ref truthy_value = gen(builder, condition->truthy, typing, env, globals);
				builder.merge_value_into(condition->truthy->get_location(), truthy_value, merge_branch);

				builder.set_insertion_block(falsey_branch);
				value_t::ref falsey_value = gen(builder, condition->falsey, typing, env, globals);
				builder.merge_value_into(condition->falsey->get_location(), falsey_value, merge_branch);

				builder.set_insertion_block(merge_branch);

				if (auto phi_node = builder.get_current_phi_node()) {
					return phi_node;
				} else {
					return builder.create_unit(condition->get_location());
				}
			} else if (auto break_ = dcast<const bitter::break_t*>(expr)) {
				assert(false);
			} else if (auto while_ = dcast<const bitter::while_t*>(expr)) {
				assert(false);
			} else if (auto block = dcast<const bitter::block_t*>(expr)) {
				size_t inst_counter = block->statements.size() - 1;

				value_t::ref block_value;
				for (auto statement: block->statements) {
					auto value = gen(builder, statement, typing, env, globals);
					if (inst_counter == 0) {
						block_value = value;
					}
				}
				return block_value != nullptr ? block_value : builder.create_unit(block->get_location());
			} else if (auto return_ = dcast<const bitter::return_statement_t*>(expr)) {
				return builder.create_return(gen(builder, return_->value, typing, env, globals));
			} else if (auto tuple = dcast<const bitter::tuple_t*>(expr)) {
				std::vector<value_t::ref> dim_values;
				for (auto dim : tuple->dims) {
					dim_values.push_back(gen(builder, dim, typing, env, globals));
				}
				return builder.create_tuple(tuple->get_location(), dim_values);
			} else if (auto tuple_deref = dcast<const bitter::tuple_deref_t*>(expr)) {
				auto td = gen(builder, tuple_deref->expr, typing, env, globals);
				log_location(
						tuple_deref->expr->get_location(),
					   	"created tuple deref %s from %s",
					   	td->str().c_str(),
					   	tuple_deref->expr->str().c_str());
				return builder.create_tuple_deref(tuple_deref->get_location(), td, tuple_deref->index);
			} else if (auto as = dcast<const bitter::as_t*>(expr)) {
				assert(as->force_cast);
				return builder.create_cast(
						as->get_location(),
						gen(builder, as->expr, typing, env, globals),
					   	as->scheme->instantiate(INTERNAL_LOC()));
			} else if (auto sizeof_ = dcast<const bitter::sizeof_t*>(expr)) {
				assert(false);
			} else if (auto match = dcast<const bitter::match_t*>(expr)) {
				assert(false);
			} else if (auto builtin = dcast<const bitter::builtin_t*>(expr)) {
				std::vector<value_t::ref> values;
				for (auto expr: builtin->exprs) {
					values.push_back(gen(builder, expr, typing, env, globals));
				}
				return builder.create_builtin(builtin->var->id, values, type);
			}

			throw user_error(expr->get_location(), "unhandled ssa-gen for %s :: %s", expr->str().c_str(), type->str().c_str());
		} catch (user_error &e) {
			e.add_info(expr->get_location(), "while in gen phase for %s", expr->str().c_str());
			throw;
		}
	}

	void builder_t::set_insertion_block(block_t::ref new_block) {
		block = new_block;
		function = new_block->parent.lock();
		module = function->parent.lock();
	}

	builder_t builder_t::save_ip() const {
		return *this;
	}

	void builder_t::restore_ip(const builder_t &builder) {
		*this = builder;
	}

	block_t::ref builder_t::create_block(std::string name, bool insert_in_new_block) {
		assert(function != nullptr);
		function->blocks.push_back(
				std::make_shared<block_t>(function, name.size() == 0 ? bitter::fresh() : name));
		if (insert_in_new_block) {
			block = function->blocks.back();
		}
		return function->blocks.back();
	}

	void builder_t::insert_instruction(instruction_t::ref instruction) {
		assert(block != nullptr);
		std::stringstream ss;
		instruction->render(ss );
		log("adding instruction %s", ss.str().c_str());
		block->instructions.push_back(instruction);
	}

	void builder_t::merge_value_into(location_t location, value_t::ref incoming_value, block_t::ref merge_block) {
		assert(block != nullptr);
		assert(block != merge_block);
		if (!type_equality(incoming_value->type, type_unit(INTERNAL_LOC()))) {
			phi_node_t::ref phi_node = merge_block->get_phi_node();
			phi_node->add_incoming_value(incoming_value, block);
		}
		create_branch(location, merge_block);
	}

	void phi_node_t::add_incoming_value(value_t::ref value, block_t::ref incoming_block) {
		for (auto pair : incoming_values) {
			if (pair.second == incoming_block) {
				throw user_error(value->get_location(), "there is already a value from this incoming block");
			} else if (pair.first == value) {
				throw user_error(value->get_location(), "this value is being added as an incoming value twice");
			}
		}
		incoming_values.push_back({value, incoming_block});
	}

	phi_node_t::ref block_t::get_phi_node() {
		if (instructions.size() != 0) {
			if (auto phi_node = dyncast<phi_node_t>(instructions.front())) {
				return phi_node;
			}
		}
		return nullptr;
	}

	phi_node_t::ref builder_t::get_current_phi_node() {
		assert(block != nullptr);
		return block->get_phi_node();
	}

	value_t::ref builder_t::create_builtin(identifier_t id, const value_t::refs &values, types::type_t::ref type) {
		log("creating builtin %s for %s with type %s",
				id.str().c_str(),
				join_str(values, ", ").c_str(),
				type->str().c_str());
		auto builtin = std::make_shared<builtin_t>(id.location, block, id, values, type);
		insert_instruction(builtin);
		return builtin;
	}

	value_t::ref builder_t::create_literal(token_t token, types::type_t::ref type) {
		return std::make_shared<literal_t>(token, type);
	}

	value_t::ref builder_t::create_call(value_t::ref callable, const value_t::refs &params) {
		auto callsite = std::make_shared<callsite_t>(callable->get_location(), block, callable, params);
		insert_instruction(callsite);
		return callsite;
	}

	value_t::ref builder_t::create_cast(location_t location, value_t::ref value, types::type_t::ref type) {
		auto cast = std::make_shared<cast_t>(location, block, value, type);
		insert_instruction(cast);
		return cast;
	}

	value_t::ref builder_t::create_tuple(location_t location, const std::vector<value_t::ref> &dims) {
		auto tuple = std::make_shared<tuple_t>(location, block, dims);
		insert_instruction(tuple);
		return tuple;
	}

	value_t::ref builder_t::create_unit(location_t location) {
		return create_tuple(location, {});
	}

	value_t::ref builder_t::create_tuple_deref(location_t location, value_t::ref value, int index) {
		auto tuple_deref = std::make_shared<tuple_deref_t>(location, block, value, index);
		insert_instruction(tuple_deref);
		return tuple_deref;
	}

	value_t::ref builder_t::create_branch(location_t location, block_t::ref goto_block) {
		auto goto_ = std::make_shared<goto_t>(location, block, goto_block);
		insert_instruction(goto_);
		return goto_;
	}

	value_t::ref builder_t::create_cond_branch(value_t::ref cond, block_t::ref truthy_branch, block_t::ref falsey_branch) {
		auto cond_branch = std::make_shared<cond_branch_t>(cond->get_location(), block, cond, truthy_branch, falsey_branch);
		insert_instruction(cond_branch);
		return cond_branch;
	}

	value_t::ref builder_t::create_return(value_t::ref expr) {
		auto return_ = std::make_shared<return_t>(expr->get_location(), block, expr);
		insert_instruction(return_);
		return return_;
	}

	std::ostream &cond_branch_t::render(std::ostream &os) const {
		return os << "if " << cond->str() << " then goto " << truthy_branch->name << " else goto " << falsey_branch->name;
	}

	std::ostream &goto_t::render(std::ostream &os) const {
		return os << "goto " << branch->name;
	}

	std::string callsite_t::get_value_name(location_t location) const {
		return lhs_name;
	}

	std::ostream &callsite_t::render(std::ostream &os) const {
		os << C_ID << lhs_name << C_RESET << " := " << callable->str() << "(";
		return os << join_str(params) << ")";
	}

	std::string phi_node_t::get_value_name(location_t location) const {
		return lhs_name;
	}

	std::ostream &phi_node_t::render(std::ostream &os) const {
		os << C_ID << lhs_name << C_RESET << " := " << C_WARN "phi" C_RESET "(";
		os << join_with(incoming_values, ", ", [](const std::pair<value_t::ref, block_t::ref> &pair) {
				return string_format("%s, %s", pair.first->str().c_str(), pair.second->name.c_str());
				});
		return os << ")";
	}

	std::string cast_t::get_value_name(location_t location) const {
		return lhs_name;
	}

	std::ostream &cast_t::render(std::ostream &os) const {
		return os << C_ID << lhs_name << C_RESET << " := " << value->str() + " as! " + type->str();
	}

	std::string load_t::get_value_name(location_t location) const {
		return lhs_name;
	}

	std::ostream &load_t::render(std::ostream &os) const {
		return os << C_ID << lhs_name << C_RESET << " := load " << rhs->str() << " :: " + rhs->type->str();
	}

	std::ostream &store_t::render(std::ostream &os) const {
		return os << "store " << rhs->str() + " :: " + rhs->type->str() + " at address " + lhs->str() + " :: " + lhs->type->str();
	}

	std::string instruction_t::get_value_name(location_t location) const {
		std::stringstream ss;
		throw user_error(location, "attempt to treat instruction %s as a value", (render(ss), ss.str().c_str()));
	}

	std::string builtin_t::get_value_name(location_t location) const {
		return lhs_name;
	}

	std::ostream &builtin_t::render(std::ostream &os) const {
		os << C_ID << lhs_name << C_RESET << " := " << id.str();
		if (values.size() != 0) {
			os << "(";
			os << join_str(values, ", ");
			os << ")";
		}
		return os;
	}

	std::ostream &return_t::render(std::ostream &os) const {
		return os << C_CONTROL "return " C_RESET << value->str();
	}

	std::string global_ref_t::str() const {
		return C_WARN "@" + name + C_RESET;
	}

	std::string literal_t::str() const {
		return token.text + " :: " + type->str().c_str();
	}

	std::string argument_t::str() const {
		return C_ID + name + C_RESET;
		//return string_format("arg%d", index);
	}

	std::string function_t::str() const {
		return C_GOOD "@" + name + C_RESET;
	}

	std::ostream &function_t::render(std::ostream &os) const {
		auto lambda_type = safe_dyncast<const types::type_operator_t>(type);
		types::type_t::refs terms;
		unfold_binops_rassoc(ARROW_TYPE_OPERATOR, type, terms);
		assert(terms.size() > 1);
		auto param_type = terms[0];
		terms.erase(terms.begin());
		auto return_type = type_arrows(terms);

		os << "fn " C_GOOD << name << C_RESET "(" << join_with(args, ", ", [](const std::shared_ptr<argument_t> &arg) {
					return arg->str() + " :: " + arg->type->str();
					});
		os << ") " << return_type->str();

		if (blocks.size() != 0) {
			os << " {" << std::endl;
			for (auto block: blocks) {
				os << block->name << ":" << std::endl;
				for (auto inst: block->instructions) {
					os << "\t";
					inst->render(os);
					os << std::endl;
				}
			}
			os << "}" << std::endl;
		}
		return os;
	}

	std::string tuple_t::get_value_name(location_t location) const {
		return lhs_name;
	}

	std::ostream &tuple_t::render(std::ostream &os) const {
		return os << C_ID << lhs_name << C_RESET << " := make_tuple(" << join_str(dims, ", ") << ")";
	}

	std::ostream &tuple_deref_t::render(std::ostream &os) const {
		return os << C_ID << lhs_name << C_RESET " := " << value->str() << "[" << index << "] :: " << type->str();
	}

	std::string tuple_deref_t::get_value_name(location_t location) const {
		return lhs_name;
	}

	std::string instruction_t::str() const {
		return C_ID + get_value_name(INTERNAL_LOC()) + C_RESET;
	}
}
