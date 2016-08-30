#include "zion.h"
#include "ast.h"
#include "type_checker.h"
#include "disk.h"
#include "scopes.h"
#include "utils.h"

const char *skstr(syntax_kind_t sk) {
	switch (sk) {

#define sk_case(x) case sk_##x: return (":" #x);

		sk_case(nil)

#define OP sk_case
#include "sk_ops.h"
#undef OP

		sk_case(base_expr)
		sk_case(expression)
		sk_case(statement)
	};
	return "<error>";
}

namespace ast {
	void log_named_item_create(const char *type, const std::string &name) {
		if (name.size() > 0) {
			debug_above(9, log(log_info, "creating a " c_ast("%s") " named " c_var("%s"),
						type, name.c_str()));
		} else {
			debug_above(9, log(log_info, "creating a " c_ast("%s"), type));
		}
	}

	module::module(const atom filename) : filename(filename) {
	}

	std::string module::get_canonical_name() const {
		return decl->get_canonical_name();
	}

	zion_token_t module_decl::get_name() const {
		return name;
	}

	std::string module_decl::get_canonical_name() const {
		static std::string ext = ".zion";
		if (name.text == "_") {
			/* this name is too generic, let's use the leaf filename */
			std::string filename = name.location.filename.str();
			auto leaf = leaf_from_file_path(filename);
			if (ends_with(leaf, ext)) {
				return leaf.substr(0, leaf.size() - ext.size());
			} else {
				return leaf;
			}
		} else {
			return name.text;
		}
	}

	data_ctor::data_ctor(atom::set type_variables, std::vector<type_ref::ref> type_ref_params) :
		type_variables(type_variables), type_ref_params(type_ref_params)
	{
	}

#if 0
	bound_type_t::ref type_expr::resolve_type(
			status_t &status,
			llvm::IRBuilder<> &builder,
			scope_t::ref scope) const
   	{
		/* instantiate this type and ensure that it has a matching llvm
		 * type object, also make sure that it exists in this scope */
		bool generic = false;
		auto symbol = get_type_name(&generic);
		return scope->resolve_type_alias(status, shared_from_this(), symbol);
	}

	status_t type_expr::get_unchecked_type(
			llvm::IRBuilder<> &builder,
		   	scope_t::ref scope,
		   	unchecked_type_t::map &generics,
		   	unchecked_type_t::ref &result_type) const
   	{
		status_t status;

		bool generic = false;
		auto type_name = get_type_name(&generic);

		if (generic && !scope->has_type(type_name)) {
			/* this is a generic. let's assign an unchecked type to this name */
			if (generics.find(type_name) == generics.end()) {
				unchecked_type_t::add_item(generics, type_name, shared_from_this());
			}

			result_type = generics[type_name];
		} else {
			ptr<inference::term> type_term;
			status |= get_type_expr_type_term(builder, *this, scope, type_term);
			if (!!status) {
				result_type = unchecked_type_t::create(type_name,
					   	safe_infer_type(builder, status, *this, scope, type_term),
						shared_from_this());
			}
		}

		return status;
	}
#endif

	item::~item() throw() {
	}

	type_alias::type_alias(type_ref::ref type_ref, atom::set type_variables) :
		type_variables(type_variables),
		type_ref(type_ref)
	{
	}

	type_decl::type_decl(atom::many type_variables) :
		type_variables(type_variables)
	{
	}

	type_sum::type_sum(std::vector<data_ctor::ref> data_ctors) :
		data_ctors(data_ctors)
	{
	}

	dimension::dimension(atom name, type_ref::ref type_ref) :
		name(name), type_ref(type_ref)
	{
	}

	type_product::type_product(std::vector<dimension::ref> dimensions, atom::set type_variables) :
		type_variables(type_variables),
		dimensions(dimensions)
	{
	}

}
