#include "zion.h"
#include "ast.h"
#include "code_id.h"
#include "llvm_utils.h"
#include "type_checker.h"
#include "compiler.h"
#include "llvm_types.h"
#include <iostream>

bound_var_t::ref ast::when_block::resolve_instantiation(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
	   	scope_t::ref block_scope,
	   	local_scope_t::ref *,
	   	bool *returns) const
{
	local_scope_t::ref when_scope;
	auto pattern_value = value->resolve_instantiation(status, builder, block_scope, &when_scope, returns);
	scope_t::ref current_scope = (when_scope != nullptr) ? when_scope : block_scope;
	runnable_scope_t::ref runnable_scope = dyncast<runnable_scope_t>(current_scope);
	if (!!status) {
		identifier::ref var_name;
		if (auto ref_expr = dyncast<const ast::reference_expr>(value)) {
			/* this is a single variable reference, which we can override in our pattern_blocks */
			// TODO: handle assignment (when x := f(a.b.c) ...) to allow for naming more complex expressions
			var_name = make_code_id(ref_expr->token);
		}

		/* recursively handle nested "else" conditions of the pattern match */
		auto iter = pattern_blocks.begin();
		pattern_blocks[0]->resolve_pattern_block(
				status,
				builder,
				pattern_value,
				var_name,
				runnable_scope,
				returns,
				++iter,
				pattern_blocks.end(),
				else_block);

		if (!!status) {
			// TODO: check whether all cases of the patter_value's type are handled
			return nullptr;
		}
	}

	assert(!status);
	return nullptr;
}

bound_var_t::ref gen_type_check(
		status_t &status,
		llvm::IRBuilder<> &builder,
		ast::item::ref node,
		runnable_scope_t::ref scope,
		identifier::ref value_name,
		bound_var_t::ref value,
		types::type::ref type,
		local_scope_t::ref *new_scope)
{
	// TODO: normalize the type to get the signature (consider just rebinding
	// into a temporary zero-based namespace), etc...
	type = type->rebind(scope->get_type_variable_bindings());

	// TODO: check for ftv's

	if (!!status) {
		/* in case we are in a generic function, we will need to assess our
		 * type*/
		// TODO: type = type->rebind(scope->get_unification_bindings());
		// scope->dump(std::cerr);
		std::cerr << std::endl;
		auto bound_type = upsert_bound_type(status, builder, scope, type);

		if (!!status) {
			auto program_scope = scope->get_program_scope();
			atom signature = bound_type->get_type()->get_signature();
			auto type_id_wanted = bound_var_t::create(
					INTERNAL_LOC(),
					string_format("typeid(%s)", value_name->str().c_str()),
					program_scope->get_bound_type({TYPEID_TYPE}),
					llvm_create_int32(builder, (int32_t)signature.iatom),
					value_name,
					false/*is_lhs*/);

			debug_above(2, log(log_info, "generating a runtime type check "
						"for type %s with signature value %d (for '%s') (type is %s)",
						type->str().c_str(), (int)signature.iatom,
						signature.c_str(), type->str().c_str()));
			bound_var_t::ref type_id = call_typeid(status, scope, node,
					value_name, builder, value);

			if (!!status) {
				auto get_typeid_eq_function = program_scope->get_bound_variable(
						status, node, "__type_id_eq_type_id");

				assert(get_typeid_eq_function != nullptr);
				if (!!status) {
					/* generate a new scope with the value_name containing a new
					 * variable to overwrite the prior scoped variable's type with
					 * the new checked type */
					*new_scope = scope->new_local_scope(string_format("when %s %s",
								value_name->str().c_str(),
								node->str().c_str()));

					/* replace this bound variable with a version of itself with a new type */
					(*new_scope)->put_bound_variable(status, value_name->get_name(),
							bound_var_t::create(
								value_name->get_location(),
								value_name->get_name(),
								bound_type,
								/* perform a safe runtime cast of this value */
								value->llvm_value,
								value_name,
								false /*is_lhs*/));

					/* call the type_id comparator function */
					return create_callsite(
							status,
							builder,
							scope,
							node,
							get_typeid_eq_function,
							value_name->get_name(),
							value_name->get_location(),
							{type_id, type_id_wanted});
				}
			}
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
		runnable_scope_t::ref scope,
		bool *returns,
		refs::const_iterator next_iter,
		refs::const_iterator end_iter,
		ptr<const ast::block> else_block) const
{
	/* if scope allows us to set up new variables inside if conditions */
	local_scope_t::ref if_scope;

	bool if_block_returns = false, else_block_returns = false;

	assert(type_ref != nullptr);

	auto type_id_name = make_type_id_code_id(
			value_name->get_location(),
			value_name->get_name());

	assert(token.text == "is");
	auto cast_type = type_ref->get_type(status, builder, scope, type_id_name, {});

	if (!!status) {
		/* evaluate the condition for branching */
		bound_var_t::ref condition_value = gen_type_check(status, builder,
				shared_from_this(), scope, value_name, value,
				cast_type, &if_scope);

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
						pattern_block_next->resolve_pattern_block(status, builder,
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
	}

	assert(!status);
    return nullptr;
}
