#include "zion.h"
#include "bound_type.h"
#include "ast.h"
#include "llvm_utils.h"
#include "llvm_types.h"

namespace ast {
	types::term::ref get_list_term(type_ref::ref type_ref) {
		assert(false);
		return nullptr;
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

	types::term::ref type_ref_named::get_type_term(identifier::refs type_variables) const {
		return term;
	}

	types::term::ref type_ref_list::get_type_term(identifier::refs type_variables) const {
		return get_list_term(type_ref);
	}

	types::term::ref type_ref_tuple::get_type_term(identifier::refs type_variables) const {
		types::term::refs terms;
		for (auto &type_ref: type_refs) {
			terms.push_back(type_ref->get_type_term(type_variables));
		}

		return types::term_product(pk_tuple, terms);
	}

	types::term::ref type_ref_generic::get_type_term(identifier::refs type_variables) const {
		return term;
	}
}
