#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include <stack>
#include <unordered_map>

struct compiler;

/* the job of the scope_setup is to set up scopes for eventual name
 * resolution at a later phase */
void scope_error(const ast::item &item, const char *msg, ...);
status_t scope_setup_program(const ast::program &obj, compiler &compiler);
unchecked_var_t::ref scope_setup_function_defn(
		status_t &status,
		const ast::item &obj,
		atom symbol,
		module_scope_t::ref module_scope);
