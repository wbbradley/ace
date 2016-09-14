#include "zion.h"
#include "ast.h"

bound_var_t::ref ast::when_block::resolve_instantiation(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref block_scope,
	   	local_scope_t::ref *new_scope,
	   	bool *returns) const
{
	return null_impl();
}
