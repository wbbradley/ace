#include "zion.h"
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
#include "code_id.h"

/* When we encounter the Empty declaration, we have to instantiate something.
 * When we create Empty() with term __obj__{__tuple__}. We don't bother
 * associating anything with the base type. We also create a bound type with
 * term 'Empty' that just maps to the raw __obj__{__tuple__} one.
 *
 * When we encounter Just, we create an unchecked data ctor, which would look
 * like:
 *
 *     def Just(any X) Just{any X}
 *
 * if it needed to have an AST. And, importantly, we do not create a type for
 * Just yet because it's not fully bound.
 * 
 * When we encounter a bound instance of the base type, like:
 * 
 *     var m Maybe{int} = ...
 *
 * we instantiate all the data ctors that are not yet instantiated.
 *
 * In the case of self-references like:
 *
 * type IntList is Node(int, IntList) or Done
 *
 * We notice that the base type is not parameterized. So, we immediately create
 * the base sum type IntList that maps to term __or__{Node{int, IntList},
 * Done} where the LLVM representation of this is just a raw var_t pointer that
 * can later be upcast, based on pattern matching on the type_id.
 */

bound_var_t::ref bind_ctor_to_scope(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		identifier::ref id,
		ast::item_t::ref node,
		types::type_function_t::ref function)
{
	assert(!!status);
	assert(id != nullptr);
	assert(function != nullptr);
	bool is_instantiation = bool(dyncast<generic_substitution_scope_t>(scope));
	assert(is_instantiation);
	/* create or find an existing ctor function that satisfies the term of
	 * this node */
	debug_above(5, log(log_info, "finding/creating data ctor for " c_type("%s") " with return type %s",
			id->str().c_str(), function->return_type->str().c_str()));

	debug_above(5, log(log_info, "function return_type %s expands to %s",
				function->return_type->str().c_str(),
				eval(function->return_type, scope->get_typename_env())->str().c_str()));

	bound_type_t::refs args = upsert_bound_types(status, builder, scope,
			function->args->args);

	if (!!status) {
		/* now that we know the parameter types, let's see what the term looks
		 * like */
		debug_above(5, log(log_info, "ctor type should be %s",
					function->str().c_str()));

		if (function->return_type != nullptr) {
			/* now we know the type of the ctor we want to create. let's check
			 * whether this ctor already exists. if so, we'll just return it. if
			 * not, we'll generate it. */
			auto tuple_pair = instantiate_tagged_tuple_ctor(status, builder,
					scope, function->inbound_context, id, node,
					function->return_type);

			if (!!status) {
				debug_above(5, log(log_info, "created a ctor %s", tuple_pair.first->str().c_str()));
				return tuple_pair.first;
			}
		} else {
			user_error(status, node->get_location(),
				   	"constructor is not returning a product type: %s",
					function->str().c_str());
		}
	}

	assert(!status);
	return nullptr;
}

void get_generics_and_lambda_vars(
		status_t &status,
	   	types::type_t::ref subtype,
		identifier::refs type_variables,
	   	scope_t::ref scope,
		std::list<identifier::ref> &lambda_vars,
		atom::set &generics)
{
	assert(generics.size() == 0);
	assert(lambda_vars.size() == 0);
	debug_above(5, log(log_info, "get_generics_and_lambda_vars(%s, %s)",
				subtype->str().c_str(),
				::str(type_variables).c_str()));

	/* create a type that takes the used type variables in the data ctor and
	 * returns placement in given type variable order */
	/* instantiate the necessary components of a data ctor */
	generics = to_atom_set(type_variables);

	/* ensure that there are no duplicate type variables */
	if (generics.size() != type_variables.size()) {
		/* this is a fail because there are some reused type variables, find
		 * them and report on them */
		atom::set seen;
		for (auto type_variable : type_variables) {
			atom name = type_variable->get_name();
			if (seen.find(name) == seen.end()) {
				seen.insert(name);
			} else {
				user_error(status, type_variable->get_location(),
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

		atom::set unbound_vars = subtype->get_ftvs();
		for (auto type_var : type_variables) {
			if (in(type_var->get_name(), unbound_vars)) {
				/* this variable is referenced by the current data ctor (the
				 * subtype), therefore it has opinions about its role in the
				 * supertype */
				lambda_vars.push_front(type_var);
			}
		}
	}
}

types::type_t::ref instantiate_data_ctor_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		types::type_t::ref unbound_type,
		identifier::refs type_variables,
		scope_t::ref scope,
		ptr<const ast::item_t> node,
		identifier::ref id,
		identifier::ref supertype_id)
{
	auto qualified_id = make_iid_impl(scope->make_fqn(id->get_name().str()), id->get_location());

	/* get the name of the ctor */
	atom tag_name = id->get_name();

	/* create the tag type */
	auto tag_type = type_id(qualified_id);

	/* create the basic struct type */
	ptr<const types::type_struct_t> struct_ = dyncast<const types::type_struct_t>(unbound_type);
	assert(struct_ != nullptr);

	/* lambda_vars tracks the order of the lambda variables we'll accept as we abstract our
	 * supertype expansion */
	std::list<identifier::ref> lambda_vars;
	atom::set generics;

	get_generics_and_lambda_vars(status, struct_, type_variables, scope,
			lambda_vars, generics);

	if (!status) {
		return nullptr;
	}

	/* now build the actual typename expansion we'll put in the typename env */
	/**********************************************/
	/* Register a data ctor for this struct_ type */
	/**********************************************/
	assert(!!status);

	if (struct_->dimensions.size() != 0) {
		assert(id->get_name() == tag_name);

		/* we're declaring a ctor at module scope */
		if (auto module_scope = dyncast<module_scope_t>(scope)) {

			/* create the actual expanded type signature of this type */
			types::type_t::ref type = type_ref(struct_);

			/* make sure we allow for parameterized expansion */
			for (auto lambda_var : lambda_vars) {
				type = type_lambda(lambda_var, type);
			}

			/* let's create the return type (an unexpanded operator) that will be the codomain of the ctor fn. */
			auto ctor_return_type = tag_type;
			for (auto lambda_var : lambda_vars) {
				ctor_return_type = type_operator(ctor_return_type, type_variable(lambda_var));
			}

			/* for now assume all ctors return refs */
			debug_above(4, log(log_info, "return type for %s will be %s",
						id->str().c_str(), ctor_return_type->str().c_str()));

			/* we need to register this constructor as an override for the name `tag_name` */
			debug_above(2, log(log_info, "adding %s as an unchecked generic data_ctor",
						id->str().c_str()));

			types::type_function_t::ref data_ctor_sig = type_function(
					scope->get_inbound_context(),
					type_args(struct_->dimensions),
					ctor_return_type);

			module_scope->get_program_scope()->put_unchecked_variable(tag_name,
					unchecked_data_ctor_t::create(id, node,
						module_scope, data_ctor_sig));
			return type;
		} else {
			user_error(status, node->token.location, "local type definitions are not yet impl");
		}
	} else {
		assert(!"is this still getting called?");

		/* it's a nullary enumeration or "tag", let's create a global value to represent
		 * this tag. */
		types::type_t::ref type = struct_;
		for (auto lambda_var : lambda_vars) {
			type = type_lambda(lambda_var, type);
		}

		/* enum values must have a supertype, right? */
		assert(supertype_id != nullptr);

		/* start by making a type for the tag */
		bound_type_t::ref bound_tag_type = bound_type_t::create(
				tag_type,
				id->get_location(),
				/* all tags use the var_t* type */
				scope->get_program_scope()->get_bound_type({"__var_ref"})->get_llvm_type());

		bound_var_t::ref tag = llvm_create_global_tag(
				builder, scope, bound_tag_type, tag_name, id);

		/* record this singleton for use later */
		scope->put_bound_variable(status, tag_name, tag);

		if (!!status) {
			debug_above(7, log(log_info, "instantiated nullary data ctor %s in scope %s",
						tag->str().c_str(), scope->get_name().c_str()));
			return type;
		}
	}

	assert(!status);
	return nullptr;
}

void ast::type_product_t::register_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref id_,
		identifier::refs type_variables,
		scope_t::ref scope) const
{
	debug_above(5, log(log_info, "creating product type for %s", str().c_str()));

	atom name = id_->get_name();
	auto location = id_->get_location();

	if (auto found_type = scope->get_bound_type(id_->get_name())) {
		/* simple check for an already bound monotype */
		user_error(status, location, "symbol " c_id("%s") " was already defined",
				name.c_str());
		user_message(log_warning, status, found_type->get_location(),
				"previous version of %s defined here",
				found_type->str().c_str());
	} else {
		auto env = scope->get_typename_env();
		auto env_iter = env.find(name);
		if (env_iter == env.end()) {
			/* instantiate_data_ctor_type has the side-effect of creating an
			 * unchecked data ctor for the type */
			auto data_ctor_type = instantiate_data_ctor_type(status, builder, type,
					type_variables, scope, shared_from_this(), id_, nullptr);

			if (!!status) {
				/* register the typename in the current environment */
				debug_above(7, log(log_info, "registering type " c_type("%s") " in scope %s",
							name.c_str(), scope->get_name().c_str()));
				scope->put_typename(status, scope->make_fqn(name.str()), data_ctor_type);

				/* success */
				return;
			}
		} else {
			/* simple check for an already bound typename env variable */
			user_error(status, location,
					"symbol " c_id("%s") " is already taken in typename env by %s",
					name.c_str(),
					env_iter->second->str().c_str());
		}
	}

	assert(!status);
}

void ast::type_sum_t::register_type(
		status_t &status,
		llvm::IRBuilder<> &builder,
		identifier::ref id,
		identifier::refs type_variables,
		scope_t::ref scope) const
{
	debug_above(3, log(log_info, "creating subtypes to %s with type variables [%s]",
				token.text.c_str(),
				join(type_variables, ", ").c_str()));

	scope->put_typename(status, scope->make_fqn(id->get_name().str()), type);
}
