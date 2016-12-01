#include "zion.h"
#include "bound_type.h"
#include "ast.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "type_instantiation.h"

namespace ast {
	types::term::ref get_list_term(type_ref::ref type_ref) {
		assert(false);
		return nullptr;
	}

	type_ref_sum::type_ref_sum(type_ref::refs subtypes) :
		subtypes(subtypes)
	{
	}

	type_ref_named::type_ref_named(types::term::ref term) :
		term(term)
	{
	}

	type_ref_list::type_ref_list(type_ref::ref type_ref) : type_ref(type_ref) {
	}

	type_ref_tuple::type_ref_tuple(std::vector<type_ref::ref> type_refs) :
		type_refs(type_refs)
	{
	}

	type_ref_generic::type_ref_generic(types::term::ref term) :
		term(term)
	{
	}

	types::term::ref type_ref_sum::get_type_term(
			status_t &status,
		   	llvm::IRBuilder<> &builder,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
   	{
		types::term::refs subtypes_terms;
		for (auto subtype : subtypes) {
			auto subtype_term = subtype->get_type_term(status, builder, scope,
					supertype_id, type_variables);
			subtypes_terms.push_back(subtype_term);

			std::list<identifier::ref> lambda_vars;
			atom::set generics;
			/* register the subtype -> supertype mapping in the type env for this
			 * subtype. */
			create_supertype_relationship(status, subtype_term,
					subtype_term->get_id(), supertype_id, type_variables,
					scope, lambda_vars, generics);
		}

		types::term::ref term_sum = types::term_sum(subtypes_terms);
		for (auto iter=type_variables.rbegin();
				iter != type_variables.rend();
				++iter)
		{
			term_sum = types::term_lambda(*iter, term_sum);
		}

		/* return the declaration of this sum type. */
		types::term::ref term_sum_binder = types::term_sum_binder(builder,
				scope, types::term_id(supertype_id), shared_from_this(),
				term_sum);

		return term_sum_binder;
	}

	types::term::ref type_ref_named::get_type_term(
			status_t &status,
		   	llvm::IRBuilder<> &builder,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
	{
		return term;
	}

	types::term::ref type_ref_list::get_type_term(
			status_t &status,
		   	llvm::IRBuilder<> &builder,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
	{
		return get_list_term(type_ref);
	}

	types::term::ref type_ref_tuple::get_type_term(
			status_t &status,
		   	llvm::IRBuilder<> &builder,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
	{
		types::term::refs terms;
		for (auto &type_ref: type_refs) {
			terms.push_back(type_ref->get_type_term(status, builder, scope,
						supertype_id, type_variables));
		}

		return types::term_product(pk_tuple, terms);
	}

	types::term::ref type_ref_generic::get_type_term(
			status_t &status,
		   	llvm::IRBuilder<> &builder,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
	{
		return term;
	}
}
