#include "zion.h"
#include <stdarg.h>
#include "phase_scope_setup.h"
#include "logger_decls.h"
#include "utils.h"
#include "compiler.h"
#include "ast.h"
#include "code_id.h"

/*
 * The idea here is that we need a phase that sets up a directed graph of name
 * resolution and adds names to the appropriate scopes.
 */

unchecked_var_t::ref scope_setup_module_symbol(
		const ast::item_t &obj,
		identifier::ref id,
		identifier::ref extends_module,
		module_scope_t::ref module_scope)
{
	program_scope_t::ref program_scope = module_scope->get_program_scope();
	auto unchecked_var = unchecked_var_t::create(id, obj.shared_from_this(), module_scope);
	if (id && id->get_name().size() != 0) {
		if (extends_module) {
			auto name = extends_module->get_name();
			if (name == GLOBAL_SCOPE_NAME) {
				return program_scope->put_unchecked_variable(id->get_name(), unchecked_var);
			} else {
				module_scope = program_scope->lookup_module(name);
				if (module_scope != nullptr) {
					return module_scope->put_unchecked_variable(id->get_name(), unchecked_var);
				} else {
					throw user_error(obj.token.location, "could not find module " c_module("%s") " to extend with " c_id("%s"),
							name.c_str(),
							id->get_name().c_str());
				}
			}
		} else {
			return module_scope->put_unchecked_variable(
					id->get_name(),
					unchecked_var_t::create(id, obj.shared_from_this(), module_scope));
		}
	} else {
		throw user_error(obj.token.location, "module-level function definition does not have a name");
	}
}

void scope_setup_type_def(
	   	const ast::type_def_t &obj,
	   	ptr<module_scope_t> module_scope)
{
	assert(obj.token.text.find(SCOPE_SEP) == std::string::npos);
	assert(obj.token.text.size() != 0);
	module_scope->put_unchecked_type(
			unchecked_type_t::create(obj.token.text, obj.shared_from_this(), module_scope));
}

void scope_setup_module(compiler_t &compiler, const ast::module_t &obj) {
	auto module_name = obj.decl->get_canonical_name();

	/* create this module's LLVM IR representation */
	module_scope_t::ref module_scope;

	if (obj.global) {
		module_scope = compiler.get_program_scope();
	} else {
		auto llvm_module = compiler.llvm_get_program_module();
		/* create a new scope for this module */
		module_scope = compiler.get_program_scope()->new_module_scope(
				module_name, llvm_module);
	}

   	compiler.set_module_scope(obj.module_key, module_scope);

	/* add any unchecked types, links, or variables to this module */
	for (auto &type_def : obj.type_defs) {
		scope_setup_type_def(*type_def, module_scope);
	}

	for (auto &function : obj.functions) {
		scope_setup_module_symbol(
				*function,
				make_iid_impl(function->decl->get_function_name(),
					function->decl->function_type->get_location()),
				function->decl->extends_module,
				module_scope);
	}

	for (auto &var_decl : obj.var_decls) {
		scope_setup_module_symbol(
				*var_decl,
				make_code_id(var_decl->token),
				var_decl->extends_module,
				module_scope);
	}
}

void scope_setup_program(const ast::program_t &obj, compiler_t &compiler) {
	/* create the outermost scope of the program */
	bool failures = false;
	location_t failure_location;
	for (auto &module : obj.modules) {
		assert(module != nullptr);
		try {
			scope_setup_module(compiler, *module);
		} catch (user_error &e) {
			if (!failures) {
				failures = true;
				failure_location = e.location;
			}
			print_exception(e);
		}
	}
	if (failures) {
		throw user_error(failure_location, "failure during scope setup");
	}
}
