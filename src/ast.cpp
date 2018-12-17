#include "zion.h"
#include "ast.h"
#include "type_checker.h"
#include "disk.h"
#include "scopes.h"
#include "utils.h"
#include "lexer.h"
#include "delegate.h"

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

	typeid_expr_t::typeid_expr_t(std::shared_ptr<expression_t> expr) : expr(expr) {
	}

	sizeof_expr_t::sizeof_expr_t(types::type_t::ref type) : parsed_type(type) {
	}

	type_decl_t::type_decl_t(identifier::refs type_variables) :
		type_variables(type_variables)
	{
	}

	dimension_t::dimension_t(std::string name, types::type_t::ref type) :
		name(name), parsed_type(type)
	{
	}

	type_product_t::type_product_t(
			bool native,
			types::type_t::ref type,
			identifier::set) :
		native(native),
		parsed_type(type)
	{
	}

	std::string var_decl_t::get_symbol() const {
		return {token.text};
	}

	location_t var_decl_t::get_location() const  {
		return token.location;
	}

	std::string parsed_type_t::str() const {
		return type->str();
	}

	location_t parsed_type_t::get_location() const {
		return type->get_location();
	}

	types::type_t::ref parsed_type_t::get_type(delegate_t &delegate, scope_t::ref scope) const {
		auto safe_delegate = delegate.get_type_delegate();
		if (type != nullptr) {
			return type->eval_typeof(safe_delegate, scope);
		} else {
			return nullptr;
		}
	}

	types::type_t::ref parsed_type_t::get_type(llvm::IRBuilder<> &builder, scope_t::ref scope) const {
		delegate_t delegate{builder};

		if (type != nullptr) {
			return type->eval_typeof(delegate, scope);
		} else {
			return nullptr;
		}
	}

	bool parsed_type_t::exists() const {
		return type != nullptr;
	}

	types::type_t::ref var_decl_t::get_type(delegate_t &delegate, scope_t::ref scope) const {
		return parsed_type.get_type(delegate, scope);
	}

	types::type_t::ref var_decl_t::get_type(llvm::IRBuilder<> &builder, scope_t::ref scope) const {
		return parsed_type.get_type(builder, scope);
	}

	std::string function_decl_t::get_function_name() const {
		if (token.tk == tk_string) {
			return unescape_json_quotes(token.text);
		} else {
			assert(token.tk == tk_identifier);
			return token.text;
		}
	}

	std::vector<token_t> function_decl_t::get_arg_tokens() const {
		types::type_function_t::ref function = safe_dyncast<types::type_function_t const>(types::without_closure(function_type));
		types::type_args_t::ref args = safe_dyncast<types::type_args_t const>(function->args);

		std::vector<token_t> arg_tokens;
		for (auto name : args->names) {
			arg_tokens.push_back(token_t{name->get_location(), tk_identifier, name->get_name()});
		}
		return arg_tokens;
	}
}
