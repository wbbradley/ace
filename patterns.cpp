#include "zion.h"
#include "ast.h"
#include "code_id.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "compiler.h"
#include "llvm_types.h"
#include <iostream>

void ast::when_block_t::resolve_statement(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref block_scope,
		life_t::ref life,
	   	local_scope_t::ref *,
	   	bool *returns) const
{
	assert(life->life_form == lf_statement);

	auto pattern_value = value->resolve_expression(status, builder,
			block_scope, life, true /*as_ref*/);
	scope_t::ref current_scope = block_scope;
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(current_scope);
	if (!!status) {
		identifier::ref var_name;
		if (auto ref_expr = dyncast<const ast::reference_expr_t>(value)) {
			/* this is a single variable reference, which we can override in our pattern_blocks */
			// TODO: handle assignment (when x := f(a.b.c) ...) as a special form
			// to allow for naming more complex expressions
			var_name = make_code_id(ref_expr->token);
		} else {
            user_error(status, value->get_location(), "pattern matching on non variable-reference expressions is not yet impl");
        }

        if (!!status) {
            /* recursively handle nested "else" conditions of the pattern match */
            auto iter = pattern_blocks.begin();
            pattern_blocks[0]->resolve_pattern_block(
                    status,
                    builder,
                    pattern_value,
                    var_name,
                    runnable_scope,
					life,
                    returns,
                    ++iter,
                    pattern_blocks.end(),
                    else_block);

            if (!!status) {
                // TODO: check whether all cases of the pattern_value's type are handled
                return;
            }
        }
	}

	assert(!status);
	return;
}

bound_var_t::ref gen_type_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ast::item_t::ref node,
		scope_t::ref scope,
		life_t::ref life,
		identifier::ref value_name,
		bound_var_t::ref value,
		bound_type_t::ref bound_type,
		local_scope_t::ref *new_scope)
{
	assert(life->life_form == lf_statement);

	auto program_scope = scope->get_program_scope();
	atom signature = bound_type->get_type()->get_signature();
	auto type_id_wanted = bound_var_t::create(
			INTERNAL_LOC(),
			string_format("typeid(%s)", value_name->str().c_str()),
			program_scope->get_bound_type({TYPEID_TYPE}),
			llvm_create_int32(builder, (int32_t)signature.iatom),
			value_name);

	debug_above(2, log(log_info, "generating a runtime type check "
				"for type %s with signature value %d (for '%s') (type is %s)",
				bound_type->str().c_str(), (int)signature.iatom,
				signature.c_str(), bound_type->get_type()->str().c_str()));
	bound_var_t::ref type_id = call_typeid(status, scope, life, node,
			value_name, builder, value);

	if (!!status) {
		auto get_typeid_eq_function = program_scope->get_bound_variable(
				status, node, "__type_id_eq_type_id");

		assert(get_typeid_eq_function != nullptr);
		if (!!status) {
			if (new_scope != nullptr) {
				if (auto runnable_scope = dyncast<runnable_scope_t>(scope)) {
					/* generate a new scope with the value_name containing a new
					 * variable to overwrite the prior scoped variable's type with
					 * the new checked type */
					*new_scope = runnable_scope->new_local_scope(string_format("when %s %s",
								value_name->str().c_str(),
								node->str().c_str()));

					/* replace this bound variable with a version of itself with a new type */
					(*new_scope)->put_bound_variable(status, value_name->get_name(),
							bound_var_t::create(
								value_name->get_location(),
								value_name->get_name(),
								bound_type,
								/* perform a safe runtime cast of this value */
								value->get_llvm_value(),
								value_name));
				}
			}

			if (!!status) {
				/* call the type_id comparator function */
				return create_callsite(
						status,
						builder,
						scope,
						life,
						get_typeid_eq_function,
						value_name->get_name(),
						value_name->get_location(),
						{type_id, type_id_wanted});
			}
		}
	}

	assert(!status);
	return nullptr;
}

void ast::pattern_block_t::resolve_pattern_block(
		status_t &status,
		llvm::IRBuilder<> &builder,
		bound_var_t::ref value,
		identifier::ref value_name,
		runnable_scope_t::ref scope,
		life_t::ref life,
		bool *returns,
		refs::const_iterator next_iter,
		refs::const_iterator end_iter,
		ptr<const ast::block_t> else_block) const
{
	assert(value != nullptr);
	assert(value_name != nullptr);

	/* if scope allows us to set up new variables inside if conditions */
	local_scope_t::ref if_scope;

	assert(token.text == "is");
	auto type_to_match = this->type->rebind(
				scope->get_type_variable_bindings());

	if (!!status) {
		/* get the bound type for this type pattern */
		bound_type_t::ref bound_type = upsert_bound_type(status, builder, scope,
				type_to_match);

		if (!!status) {
			/* check whether this type is __unreachable */
			if (!bound_type->is_concrete()) {
				/* it looks like this type is too abstract to understand. that means
				 * our code cannot possibly expect to need to pattern match against
				 * it. let's skip it */
				if (next_iter != end_iter) {
					auto pattern_block_next = *next_iter;
					return pattern_block_next->resolve_pattern_block(status, builder,
							value, value_name, scope, life,
							returns, ++next_iter, end_iter,
							else_block);
				} else if (else_block != nullptr) {
					return else_block->resolve_statement(status, builder,
							scope, life, nullptr, returns);
				}

				/* we've got nothing else to match on, so, let's bail */
				return;
			}

			/* evaluate the condition for branching */
			bound_var_t::ref condition_value = gen_type_check(status, builder,
					shared_from_this(), scope, life, value_name, value,
					bound_type, &if_scope);

			if (!!status) {
				assert(condition_value->is_int());

				llvm::Value *llvm_condition_value = condition_value->get_llvm_value();

				if (!!status) {
					/* test that the if statement doesn't return */
					llvm::Function *llvm_function_current = llvm_get_function(builder);

					/* generate some new blocks */
					llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(builder.getContext(), "pattern.is", llvm_function_current);
					llvm::BasicBlock *merge_bb = nullptr;

					/* we have to keep track of whether we need a merge block
					 * because our nested branches could all return */
					bool insert_merge_bb = false;
					bool else_block_returns = false;

					if ((next_iter != end_iter) || (else_block != nullptr)) {
						/* we've got an else block, so let's create an "else" basic block. */
						llvm::BasicBlock *else_bb = llvm::BasicBlock::Create(builder.getContext(), "pattern.else", llvm_function_current);

						/* put the merge block after the else block */
						merge_bb = llvm::BasicBlock::Create(builder.getContext(), "pattern.merge");

						/* create the actual branch instruction */
						llvm_create_if_branch(status, builder, scope,
								0, nullptr, llvm_condition_value, then_bb, else_bb);

						if (!!status) {
							builder.SetInsertPoint(else_bb);
							if (next_iter != end_iter) {
								auto pattern_block_next = *next_iter;
								pattern_block_next->resolve_pattern_block(status, builder,
										value, value_name, scope, life,
										&else_block_returns, ++next_iter, end_iter,
										else_block);
							} else {
								else_block->resolve_statement(status, builder,
										scope, life, nullptr, &else_block_returns);
							}

							if (!else_block_returns) {
								/* keep track of the fact that we have to have a
								 * merged block to land in after the else block */
								insert_merge_bb = true;

								/* go ahead and jump there */
								if (!builder.GetInsertBlock()->getTerminator()) {
									builder.CreateBr(merge_bb);
								}
							}
						}
					} else {
						/* since there is no else block it cannot return */
						else_block_returns = false;

						/* keep track of the fact that we have to have a merged
						 * block to land in after the if block */
						insert_merge_bb = true;

						/* put the merge block after the if block */
						merge_bb = llvm::BasicBlock::Create(builder.getContext(), "pattern.merge");

						/* we don't have an else block, so we can just continue on */
						llvm_create_if_branch(status, builder, scope,
								0, nullptr, llvm_condition_value, then_bb, merge_bb);
					}

					if (!!status) {
						/* let's generate code for the "then" block */
						builder.SetInsertPoint(then_bb);
						bool if_block_returns = false;
						block->resolve_statement(status, builder,
								if_scope ? if_scope : scope, life, nullptr,
								&if_block_returns);
						if (!!status) {
							if (!if_block_returns) {
								insert_merge_bb = true;
								if (!builder.GetInsertBlock()->getTerminator()) {
									builder.CreateBr(merge_bb);
								}
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
							return;
						}
					}
				}
			}
		}
	}

	assert(!status);
    return;
}
