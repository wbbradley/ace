#include "zion.h"
#include "bound_type.h"
#include "ast.h"
#include "llvm_utils.h"
#include "llvm_types.h"
#include "type_instantiation.h"

namespace ast {
	types::type::ref get_list_type(type_ref::ref type_ref) {
		assert(false);
		return nullptr;
	}

	type_ref_sum::type_ref_sum(type_ref::refs subtype_refs) :
		subtype_refs(subtype_refs)
	{
	}

	type_ref_standard::type_ref_standard(types::type::ref type) :
		type(type)
	{
	}

	type_ref_list::type_ref_list(type_ref::ref type_ref) : type_ref(type_ref) {
	}

	type_ref_tuple::type_ref_tuple(std::vector<type_ref::ref> type_refs) :
		type_refs(type_refs)
	{
	}

	types::type::ref type_ref_sum::get_type(
			status_t &status,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
   	{
		types::type::refs subtypes;
		for (auto subtype_ref : subtype_refs) {
			auto subtype = subtype_ref->get_type(status, scope, supertype_id,
					type_variables);
			if (!!status) {
				subtypes.push_back(subtype);

				std::list<identifier::ref> lambda_vars;
				atom::set generics;
				/* register the subtype -> supertype mapping in the type env for this
				 * subtype. */
				identifier::ref subtype_id = subtype->get_id();
				if (subtype_id != nullptr) {
					create_supertype_relationship(status, subtype,
							subtype_id, supertype_id, type_variables,
							scope, lambda_vars, generics);
					if (!status) {
						break;
					}
				} else {
					/* if you don't have a name for your subtype, you can't have a
					 * supertype expansion for it, because what would we call it? */
					debug_above(5, log(log_info, "not creating a supertype expansion for %s",
								subtype->str().c_str()));
				}
			} else {
				break;
			}
		}
		
		if (!!status) {
			types::type::ref sum_fn = ::type_sum(subtypes);
			for (auto iter = type_variables.rbegin();
					iter != type_variables.rend();
					++iter)
			{
				sum_fn = ::type_lambda(*iter, sum_fn);
			}

			return sum_fn;
		}

		assert(!status);
		return nullptr;
	}

	types::type::ref type_ref_standard::get_type(
			status_t &status,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
	{
		return type;
	}

	types::type::ref type_ref_list::get_type(
			status_t &status,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
	{
		auto element_type = type_ref->get_type(status, scope, supertype_id, type_variables);
		if (!!status) {
			return type_list_type(element_type);
		}

		assert(!status);
		return nullptr;
	}

	types::type::ref type_ref_tuple::get_type(
			status_t &status,
		   	scope_t::ref scope,
			identifier::ref supertype_id,
			identifier::refs type_variables) const
	{
		types::type::refs dimensions;
		for (auto &type_ref: type_refs) {
			auto dimension = type_ref->get_type(status, scope,
					supertype_id, type_variables);
			if (!!status) {
				dimensions.push_back(dimension);
			} else {
				break;
			}
		}

		if (!!status) {
			return ::type_product(pk_tuple, dimensions);
		}

		assert(!status);
		return nullptr;
	}
}
