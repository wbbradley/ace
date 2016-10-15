#include "zion.h"
#include "ast.h"
#include "code_id.h"
#include "llvm_utils.h"
#include "type_checker.h"

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

bound_var_t::ref gen_type_check(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	bound_var_t::ref value,
		types::term::ref type_term,
	   	local_scope_t::ref *new_scope)
{
	return null_impl();
}

bound_var_t::ref ast::pattern_block::resolve_pattern_block(
		status_t &status,
		llvm::IRBuilder<> &builder,
		bound_var_t::ref value,
		identifier::ref value_name,
		scope_t::ref scope,
		bool *returns,
		refs::const_iterator next_iter,
		refs::const_iterator end_iter,
		ptr<const ast::block> else_block) const
{
	/* if scope allows us to set up new variables inside if conditions */
	local_scope_t::ref if_scope;

	bool if_block_returns = false, else_block_returns = false;

	assert(type_ref != nullptr);

	assert(token.text == "is");

	/* evaluate the condition for branching */
	bound_var_t::ref condition_value = gen_type_check(status, builder, value,
			type_ref->get_type_term({}), &if_scope);

	if (!!status) {
		assert(condition_value->is_int());

		llvm::Value *llvm_condition_value = condition_value->llvm_value;

		if (!!status) {
			/* test that the if statement doesn't return */
			llvm::Function *llvm_function_current = llvm_get_function(builder);

			/* generate some new blocks */
			llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(builder.getContext(), "then", llvm_function_current);
			llvm::BasicBlock *merge_bb = nullptr;

			/* we have to keep track of whether we need a merge block
			 * because our nested branches could all return */
			bool insert_merge_bb = false;

			if ((next_iter != end_iter) || (else_block != nullptr)) {
				/* we've got an else block, so let's create an "else" basic block. */
				llvm::BasicBlock *else_bb = llvm::BasicBlock::Create(builder.getContext(), "else", llvm_function_current);

				/* put the merge block after the else block */
				merge_bb = llvm::BasicBlock::Create(builder.getContext(), "ifcont");

				/* create the actual branch instruction */
				llvm_create_if_branch(builder, llvm_condition_value, then_bb, else_bb);

				builder.SetInsertPoint(else_bb);
				if (next_iter != end_iter) {
					auto pattern_block_next = *next_iter;
					pattern_block_next->resolve_pattern_block( status, builder,
							value, value_name, scope,
							&else_block_returns, ++next_iter, end_iter,
							else_block);
				} else {
					else_block->resolve_instantiation(status, builder,
							scope, nullptr, &else_block_returns);
				}

				if (!else_block_returns) {
					/* keep track of the fact that we have to have a
					 * merged block to land in after the else block */
					insert_merge_bb = true;

					/* go ahead and jump there */
					builder.CreateBr(merge_bb);
				}
			} else {
				/* since there is no else block it cannot return */
				else_block_returns = false;

				/* keep track of the fact that we have to have a merged
				 * block to land in after the if block */
				insert_merge_bb = true;

				/* put the merge block after the if block */
				merge_bb = llvm::BasicBlock::Create(builder.getContext(), "ifcont");

				/* we don't have an else block, so we can just continue on */
				llvm_create_if_branch(builder, llvm_condition_value, then_bb, merge_bb);
			}

			if (!!status) {
				/* let's generate code for the "then" block */
				builder.SetInsertPoint(then_bb);
				block->resolve_instantiation(status, builder, if_scope ? if_scope : scope, nullptr, &if_block_returns);
				if (!!status) {
					if (!if_block_returns) {
						insert_merge_bb = true;
						builder.CreateBr(merge_bb);
						builder.SetInsertPoint(merge_bb);
					}
					
					if (insert_merge_bb) {
						/* we know we'll need to fall through to the merge
						 * block, let's add it to the end of the function
						 * and let's set it as the next insert point. */
						llvm_function_current->getBasicBlockList().push_back(merge_bb);
						builder.SetInsertPoint(merge_bb);
					}

					/* track whether the branches return */
					*returns |= (if_block_returns && else_block_returns);

					assert(!!status);
					return nullptr;
				}
			}
		}
	}

	assert(!status);
    return nullptr;
}
