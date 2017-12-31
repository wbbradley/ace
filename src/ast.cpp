#include "zion.h"
#include "ast.h"
#include "type_checker.h"
#include "disk.h"
#include "scopes.h"
#include "utils.h"
#include "lexer.h"

const char *skstr(syntax_kind_t sk) {
	switch (sk) {

#define sk_case(x) case sk_##x: return (":" #x);

		sk_case(null)

#define OP sk_case
#include "sk_ops.h"
#undef OP

		sk_case(expression)
		sk_case(statement)
	};
	return "<error>";
}

namespace ast {
	void log_named_item_create(const char *type, const std::string &name) {
		if (name.size() > 0) {
			debug_lexer(log(log_info, "creating a " c_ast("%s") " named " c_var("%s"),
						type, name.c_str()));
		} else {
			debug_lexer(log(log_info, "creating a " c_ast("%s"), type));
		}
	}

	module_t::module_t(const std::string filename, bool global) : global(global), filename(filename) {
	}

	std::string module_t::get_canonical_name() const {
		return decl->get_canonical_name();
	}

	token_t module_decl_t::get_name() const {
		return name;
	}

	std::string module_decl_t::get_canonical_name() const {
		static std::string ext = ".zion";
		if (global) {
			return GLOBAL_SCOPE_NAME;
		} else if (name.text == "_") {
			/* this name is too generic, let's use the leaf filename */
			std::string filename = name.location.filename;
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

	item_t::~item_t() throw() {
	}

	typeid_expr_t::typeid_expr_t(ptr<expression_t> expr) : expr(expr) {
	}

	sizeof_expr_t::sizeof_expr_t(types::type_t::ref type) : type(type) {
	}

	type_decl_t::type_decl_t(identifier::refs type_variables) :
		type_variables(type_variables)
	{
	}

	type_sum_t::type_sum_t(types::type_t::ref type) :
		type(type)
	{
	}

	dimension_t::dimension_t(std::string name, types::type_t::ref type) :
		name(name), type(type)
	{
	}

	type_product_t::type_product_t(
			bool native,
			types::type_t::ref type,
			identifier::set type_variables) :
		native(native),
		type(type),
		type_variables(type_variables)
	{
	}

    std::string var_decl_t::get_symbol() const {
        return {token.text};
    }

    location_t var_decl_t::get_location() const  {
        return token.location;
    }

    types::type_t::ref var_decl_t::get_type() const {
        return type;
    }

    bool var_decl_t::has_initializer() const {
        return initializer != nullptr;
    }

    bound_var_t::ref var_decl_t::resolve_initializer(
            status_t &status,
            llvm::IRBuilder<> &builder,
            scope_t::ref scope,
            life_t::ref life) const
    {
        auto bound_var = initializer->resolve_expression(status, builder, scope, life, false /*as_ref*/);
		if (!!status) {
			assert(!bound_var->is_ref());
			return bound_var;
		}

		assert(!status);
		return nullptr;
    }

	std::string function_decl_t::get_function_name() const {
		if (token.tk == tk_string) {
			return unescape_json_quotes(token.text);
		} else {
			assert(token.tk == tk_identifier);
			return token.text;
		}
	}

	void function_decl_t::set_function_name(token_t new_token) {
		assert(token.tk == tk_string || token.tk == tk_identifier);
		token = new_token;
	}
}
