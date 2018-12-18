#include "zion.h"
#include "logger.h"
#include <iostream>
#include <numeric>
#include <list>
#include "type_instantiation.h"
#include "bound_var.h"
#include "scopes.h"
#include "ast.h"
#include "llvm_types.h"
#include "llvm_utils.h"
#include "phase_scope_setup.h"
#include "types.h"

bound_var_t::ref bind_ctor_to_scope(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier_t id,
		std::string ctor_name,
		location_t location,
		types::type_function_t::ref function)
{
	assert(id != nullptr);
	assert(function != nullptr);

	/* create or find an existing ctor function that satisfies the term of
	 * this node */
	debug_above(5, log(log_info, "finding/creating data ctor for " c_type("%s") " with type %s",
				id->str().c_str(),
			   	function->str().c_str()));

	types::type_args_t::ref type_args = dyncast<const types::type_args_t>(function->args);
	assert(type_args != nullptr);

	/* let's create the data type from the type args in the ctor function */
	types::type_t::ref data_type = type_ptr(type_managed(type_struct(type_args)));

	bound_type_t::ref bound_data_type = upsert_bound_type(builder, scope, data_type);

	/* now that we know the parameter types, let's see what the term looks
	 * like */
	debug_above(5, log(log_info, "ctor type should be %s", function->str().c_str()));

	assert(function->return_type != nullptr);

	/* now we know the type of the ctor we want to create. let's check
	 * whether this ctor already exists. if so, we'll just return it. if
	 * not, we'll generate it. */
	auto ctor = upsert_tagged_tuple_ctor(builder, scope, id, ctor_name, location,
			data_type, function->return_type);

	debug_above(5, log(log_info, "created a ctor %s", ctor->str().c_str()));
	return ctor;
}

void get_generics_and_lambda_vars(
	   	types::type_t::ref subtype,
		identifiers_t type_variables,
	   	scope_t::ref scope,
		std::list<identifier_t> &lambda_vars,
		std::set<std::string> &generics)
{
	assert(generics.size() == 0);
	assert(lambda_vars.size() == 0);
	debug_above(5, log(log_info, "get_generics_and_lambda_vars(%s, [%s])",
				subtype->str().c_str(),
				join_str(type_variables, ", ").c_str()));

	/* create a type that takes the used type variables in the data ctor and
	 * returns placement in given type variable order */
	/* instantiate the necessary components of a data ctor */
	generics = to_atom_set(type_variables);

	/* ensure that there are no duplicate type variables */
	if (generics.size() != type_variables.size()) {
		/* this is a fail because there are some reused type variables, find
		 * them and report on them */
		std::set<std::string> seen;
		for (auto type_variable : type_variables) {
			std::string name = type_variable->get_name();
			if (seen.find(name) == seen.end()) {
				seen.insert(name);
			} else {
				throw user_error(type_variable->get_location(),
						"found duplicate type variable " c_id("%s"),
						name.c_str());
			}
		}
	} else {
		debug_above(5, log(log_info,
				   	"getting lambda_vars for value type %s",
					subtype->str().c_str()));

		/* if any of the type names are actually inbound type variables, take
		 * note of the order they are mentioned. tell us how to create the
		 * lambda we'll place into the type environment to represent the fact
		 * that this data ctor is a subtype of the supertype. and, tell us which
		 * types are parametrically bound to this subtype, and which are still
		 * quantified */

		std::set<std::string> unbound_vars = subtype->get_ftvs();
		for (auto type_var : type_variables) {
			/* this variable is referenced by the current data ctor (the
			 * subtype), therefore it has opinions about its role in the
			 * supertype */
			lambda_vars.push_front(type_var);
		}
		assert(lambda_vars.size() == type_variables.size());
	}
}

void instantiate_data_ctor_type(
		llvm::IRBuilder<> &builder,
		types::type_t::ref unbound_type,
		identifiers_t type_variables,
		scope_t::ref scope,
		std::shared_ptr<const ast::item_t> node,
		identifier_t id,
		bool native)
{
	indent_logger indent(node->get_location(), 5,
			string_format("instantiating data ctor %s", id->str().c_str()));
	/* get the name of the ctor */
	std::string tag_name = id->get_name();
	std::string fqn_tag_name = scope->make_fqn(tag_name);
	auto qualified_id = make_iid_impl(fqn_tag_name, id->get_location());

	/* create the tag type */
	auto tag_type = type_id(qualified_id);

	/* create the basic struct type */
	std::shared_ptr<const types::type_struct_t> struct_ = dyncast<const types::type_struct_t>(unbound_type);
	assert(struct_ != nullptr);

	/* lambda_vars tracks the order of the lambda variables we'll accept as we abstract our
	 * supertype expansion */
	std::list<identifier_t> lambda_vars;
	std::set<std::string> generics;

	get_generics_and_lambda_vars(struct_, type_variables, scope,
			lambda_vars, generics);

	/**********************************************/
	/* Register a data ctor for this struct_ type */
	/**********************************************/
	assert(id->get_name() == tag_name);

	/* we're declaring a ctor at module scope */
	if (auto module_scope = dyncast<module_scope_t>(scope)) {

		/* let's create the return type (an unexpanded operator) that will be the codomain of the ctor fn. */
		auto ctor_return_type = tag_type;
		for (auto lambda_var_iter = lambda_vars.rbegin(); lambda_var_iter != lambda_vars.rend(); ++lambda_var_iter) {
			ctor_return_type = type_operator(ctor_return_type, type_variable(*lambda_var_iter));
		}

		/* for now assume all ctors return refs */
		debug_above(4, log(log_info, "return type for %s will be %s",
					id->str().c_str(), ctor_return_type->str().c_str()));

		/* we need to register this constructor as an override for the name `tag_name` */
		debug_above(2, log(log_info, "adding %s as an unchecked generic data_ctor",
					id->str().c_str()));

		types::type_function_t::ref data_ctor_sig = type_function(
				id->get_location(),
				nullptr,
				type_args(types::without_refs(struct_->dimensions)),
				ctor_return_type);

		module_scope->get_program_scope()->put_unchecked_variable(tag_name,
				unchecked_data_ctor_t::create(id, node,
					module_scope, data_ctor_sig, native));

		/* now build the actual typename expansion we'll put in the typename env */
		/* 1. create the actual expanded type signature of this type */
		types::type_t::ref type;
		if (native) {
			type = struct_;
		} else {
			type = type_ptr(type_managed(struct_));
		}

		/* 2. make sure we allow for parameterized expansion */
		for (auto lambda_var : lambda_vars) {
			type = type_lambda(lambda_var, type);
		}

		scope->put_structural_typename(tag_name, type);

		return;
	} else {
		throw user_error(node->token.location, "local type definitions are not yet impl");
	}
}

void ast::type_product_t::register_type(
		llvm::IRBuilder<> &builder,
		identifier_t id_,
		identifiers_t type_variables,
		scope_t::ref scope) const
{
	debug_above(5, log(log_info, "creating product type for %s", str().c_str()));
	debug_above(7, log(log_info, "%s has type %s", id_->get_name().c_str(), parsed_type.str().c_str()));

	std::string name = id_->get_name();
	auto location = id_->get_location();

	if (!native) {
		if (dyncast<const program_scope_t>(scope) == nullptr && !is_valid_udt_initial_char(name[0])) {
			throw user_error(location, "type names must begin with an uppercase letter");
		}
	}

	delegate_t delegate{builder};
	auto type = parsed_type.get_type(delegate, scope);
	/* instantiate a lazily bound data ctor, and inject the typename for this type into the
	 * type environment */
	auto existing_type = scope->get_type(name, true /*allow_structural_types*/);
	if (existing_type == nullptr) {
		/* instantiate_data_ctor_type has the side-effect of creating an
		 * unchecked data ctor for the type */
		instantiate_data_ctor_type(builder, type,
				type_variables, scope, shared_from_this(), id_, native);
		return;
	} else {
		/* simple check for an already bound typename env variable */
		auto error = user_error(location,
				"symbol " c_id("%s") " is already taken in typename env by %s",
				name.c_str(),
				existing_type->str().c_str());
		error.add_info(existing_type->get_location(),
				"previous version of %s defined here",
				type->str().c_str());
		throw error;
	}
}

types::type_t::map bottom_out_unreferenced_vars(const std::set<std::string> &seen, const identifiers_t &all) {
	types::type_t::map bindings;
	for (auto a : all) {
		const auto &name = a->get_name();
		if (!in(name, seen)) {
			bindings[name] = type_bottom();
		}
	}
	return bindings;
}

void ast::data_type_t::register_type(
		llvm::IRBuilder<> &builder,
		identifier_t id,
		identifiers_t type_variables,
		scope_t::ref scope) const
{
	debug_above(3, log(log_info, "registering data type %s", str().c_str()));
	auto module_scope = scope->get_module_scope();

	if (dyncast<const program_scope_t>(scope) == nullptr && !is_valid_udt_initial_char(id->get_name()[0])) {
		throw user_error(id->get_location(), "type names must begin with an uppercase letter");
	}

	auto existing_type = scope->get_type(id->get_name(), true /*allow_structural_types*/);
	if (existing_type == nullptr) {
		/* good, we haven't seen this symbol before */
		std::vector<types::type_variable_t::ref> vars;
		for (auto var : type_variables) {
			vars.push_back(type_variable(var));
		}

		/* create the data type's environment entry */
		types::type_t::ref expansion = type_data(
				token_t(id->get_location(), tk_identifier, scope->make_fqn(id->get_name())),
				vars,
			   	ctor_pairs);

		for (auto iter = type_variables.rbegin(); iter != type_variables.rend(); ++iter) {
			auto &type_variable = *iter;
			expansion = type_lambda(type_variable, expansion);
		}
		scope->put_nominal_typename(id->get_name(), expansion);

		/* create the ctor return type */
		identifier_t data_type_id = make_iid_impl(
					scope->make_fqn(id->get_name()),
					id->get_location());

		types::type_t::ref ctor_return_type = type_id(data_type_id);
		for (auto type_variable : type_variables) {
			ctor_return_type = type_operator(ctor_return_type, ::type_variable(type_variable));
		}

		for (auto &ctor_pair : ctor_pairs) {
			identifier_t ctor_id = make_iid_impl(ctor_pair.first.text, get_location());
			if (ctor_pair.second->args.size() == 0) {
				bound_type_t::ref bound_tag_type = upsert_bound_type(builder, scope,
						ctor_return_type->rebind(
							bottom_out_unreferenced_vars({}, type_variables)));
				bound_var_t::ref tag = llvm_create_global_tag(
						builder, scope, bound_tag_type, ctor_id->get_name(),
						ctor_id);
				/* record this tag variable for use later */
				scope->put_bound_variable(ctor_id->get_name(), tag);

				debug_above(7, log(log_info, "instantiated nullary data ctor %s", tag->str().c_str()));
			} else {
				/* create and register an unchecked data ctor */
				types::type_function_t::ref data_ctor_sig = type_function(
						id->get_location(),
					   	nullptr, 
						ctor_pair.second,
					   	ctor_return_type->rebind(
							bottom_out_unreferenced_vars(ctor_pair.second->get_ftvs(), type_variables)));

				module_scope->put_unchecked_variable(
						ctor_id->get_name(),
						unchecked_data_ctor_t::create(ctor_id, shared_from_this(),
							module_scope, data_ctor_sig, false /*native*/));
			}
		}
	} else {
		auto error = user_error(id->get_location(), "data types cannot be registered twice");
		error.add_info(existing_type->get_location(), "see prior type registered here");
		throw error;
	}
}

void ast::type_link_t::register_type(
		llvm::IRBuilder<> &builder,
		identifier_t id,
		identifiers_t type_variables,
		scope_t::ref scope) const
{
	auto type = scope->get_type(id->get_name(), true /*allow_structural_types*/);
	if (type == nullptr) {
		debug_above(3, log("registering type link for %s link", id->get_name().c_str()));

		/* first construct the inner type which will basically be a call back to the outer type.
		 * type_links are constructed recursively - being defined by themselves - since they are not
		 * defined inside the language. */
		types::type_t::ref inner = type_id(id);
		for (auto type_variable : type_variables) {
			inner = type_operator(inner, ::type_variable(type_variable));
		}

		/* now construct the lambda that points back to the type */
		auto type = type_extern(inner);
		for (auto iter = type_variables.rbegin();
				iter != type_variables.rend();
				++iter)
		{
			type = ::type_lambda(*iter, type);
		}

		scope->put_structural_typename(id->get_name(), type);
	} else {
		auto error = user_error(id->get_location(), "type links cannot be registered twice");
		error.add_info(type->get_location(), "see prior type registered here");
		throw error;
	}
}

void ast::type_alias_t::register_type(
		llvm::IRBuilder<> &builder,
		identifier_t supertype_id,
		identifiers_t type_variables,
	   	scope_t::ref scope) const
{
	debug_above(5, log(log_info, "creating type alias for " c_id("%s") " %s",
				supertype_id->get_name().c_str(),
				str().c_str()));

	std::list<identifier_t> lambda_vars;
	std::set<std::string> generics;

	delegate_t delegate{builder};
	auto type = parsed_type.get_type(delegate, scope);

	get_generics_and_lambda_vars(type, type_variables, scope, lambda_vars, generics);
	types::type_t::ref final_type = type;
	for (auto lambda_var : lambda_vars) {
		final_type = type_lambda(lambda_var, type);
	}
	auto existing_type = scope->get_type(scope->make_fqn(token.text), true /*allow_structural_types*/);
	if (existing_type == nullptr) {
		scope->put_nominal_typename(token.text, final_type);
	} else {
		// debug_above(5, log(log_info, "skipping type alias creation of %s", str().c_str()));
		// assert(iter->second->get_signature() == final_type->get_signature());
		auto error = user_error(type->get_location(), "type aliases cannot be registered twice (regarding " c_id("%s") ")",
				str().c_str());
		error.add_info(existing_type->get_location(), "see prior type registered here");
		throw error;
	}
}

