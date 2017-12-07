#pragma once
#include "ast_decls.h"
#include "scopes.h"
#include <stack>

struct compiler_t;

/* the job of the scope_setup is to set up scopes for eventual name
 * resolution at a later phase */
void scope_error(const ast::item_t &item, const char *msg, ...);
status_t scope_setup_program(const ast::program_t &obj, compiler_t &compiler);
unchecked_var_t::ref scope_setup_function_defn(
		status_t &status,
		const ast::item_t &obj,
		atom symbol,
		module_scope_t::ref module_scope);
