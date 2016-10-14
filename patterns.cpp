#include "zion.h"
#include "ast.h"
#include "code_id.h"
#include "llvm_utils.h"

bound_var_t::ref ast::when_block::resolve_instantiation(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref block_scope,
	   	local_scope_t::ref *,
	   	bool *returns) const
{
	local_scope_t::ref when_scope;
	auto pattern_value = value->resolve_instantiation(status, builder, block_scope, &when_scope, returns);
	if (!!status) {
		identifier::ref var_name;
		if (auto ref_expr = dyncast<const ast::reference_expr>(value)) {
			/* this is a single variable reference, which we can override in our pattern_blocks */
			// TODO: handle assignment (when x := f(a.b.c) ...) to allow for naming more complex expressions
			var_name = make_code_id(ref_expr->token);
		}

		llvm::Function *llvm_function_current = llvm_get_function(builder);
		// llvm::BasicBlock *merge_bb = nullptr;
		/* we have to keep track of whether we need a merge block
		 * because our nested branches could all return */
		std::vector<llvm::BasicBlock *> pattern_bbs;
		std::vector<bool> pattern_returns;
		for (int i = 0; i < pattern_blocks.size(); ++i) {
			pattern_bbs.push_back(llvm::BasicBlock::Create(builder.getContext(),
						string_format("pattern_%d", i).c_str(),
						llvm_function_current));
			pattern_returns.push_back(false);
		}
		// bool insert_merge_bb = false;

		auto iter = pattern_blocks.begin();
		pattern_blocks[0]->resolve_pattern_block(
				status,
				builder,
				pattern_value,
				var_name,
				when_scope,
				returns,
				++iter,
				pattern_blocks.end(),
				else_block);
		if (!!status) {
			// TODO: check whether all cases of the patter_value's type are handled
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref ast::pattern_block::resolve_pattern_block(
		status_t &status,
		llvm::IRBuilder<> &builder,
		bound_var_t::ref value,
		identifier::ref value_name,
		scope_t::ref block_scope,
		bool *returns,
		refs::const_iterator next_iter,
		refs::const_iterator end_iter,
		ptr<const ast::block> else_block) const
{
	return null_impl();
}
