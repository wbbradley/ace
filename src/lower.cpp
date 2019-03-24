#include "lower.h"
#include "llvm_utils.h"
#include "types.h"

namespace lower {
	typedef std::map<std::string, std::map<types::type_t::ref, llvm::Value *, types::compare_type_t>> env_t;

	llvm::Value *maybe_get_llvm_value(const env_t &env, std::string name, types::type_t::ref type) {
		type = types::unitize(type);
		return get(env, name, type, static_cast<llvm::Value *>(nullptr));
	}

	llvm::Value *get_llvm_value(const env_t &env, std::string name, types::type_t::ref type) {
		auto llvm_value = maybe_get_llvm_value(env, name, type);
		if (llvm_value == nullptr) {
			auto error = user_error(INTERNAL_LOC(), "we need an llvm definition for %s :: %s", name.c_str(), type->str().c_str());
			for (auto pair : env) {
				for (auto overload: pair.second) {
					error.add_info(INTERNAL_LOC(), "%s :: %s = %s",
							pair.first.c_str(),
							overload.first->str().c_str(),
							llvm_print(overload.second).c_str());
				}
			}
			print_exception(error, 10);
			dbg();
			throw error;
		}
		return llvm_value;
	}

	void set_llvm_value(env_t &env, std::string name, types::type_t::ref type, llvm::Value *llvm_value, bool allow_shadowing) {
		debug_above(5, log("setting env[%s][%s] = %s", name.c_str(),
				   	type->str().c_str(),
				   	llvm_print(llvm_value).c_str()));
		assert(name.size() != 0);
		auto existing_llvm_value = get(env, name, type, static_cast<llvm::Value *>(nullptr));
		if (existing_llvm_value == nullptr) {
			env[name][type] = llvm_value;
		} else {
			if (allow_shadowing) {
				env[name][type] = llvm_value;
			} else {
				/* what now? probably shouldn't have happened */
				assert(false);
			}
		}
	}

	llvm::Constant *lower_decl(
			std::string name,
			llvm::IRBuilder<> &builder,
			llvm::Module *llvm_module,
			gen::value_t::ref value,
			env_t &env)
	{
		debug_above(4, log("lower_decl(%s, ..., %s :: %s, ...)", name.c_str(), value->str().c_str(), value->type->str().c_str()));

		assert(value != nullptr);

		if (auto unit = dyncast<gen::unit_t>(value)) {
			return llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo());
		} else if (auto literal = dyncast<gen::literal_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto phi_node = dyncast<gen::phi_node_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto cast = dyncast<gen::cast_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto function = dyncast<gen::function_t>(value)) {
			types::type_t::refs type_terms;
			unfold_binops_rassoc(ARROW_TYPE_OPERATOR, function->type, type_terms);
			if (function->args.size() != 0 && function->args.back()->name == "__closure") {
				/* this function will not be called directly, it will be packaged into a closure */
				type_terms.insert(type_terms.end() - 1, type_id(make_iid("__closure_t")));
			}
			return llvm_start_function(builder, llvm_module, type_terms, name + " :: " + function->type->repr());
		} else if (auto builtin = dyncast<gen::builtin_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto argument = dyncast<gen::argument_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto goto_ = dyncast<gen::goto_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto cond_branch = dyncast<gen::cond_branch_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto callsite = dyncast<gen::callsite_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto return_ = dyncast<gen::return_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto load = dyncast<gen::load_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto store = dyncast<gen::store_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto tuple = dyncast<gen::tuple_t>(value)) {
			llvm::Type *llvm_type = get_llvm_type(builder, tuple->type);
			llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(llvm_type->getPointerElementType());
			assert(llvm_struct_type != nullptr);

			std::vector<llvm::Constant *> llvm_struct_data;
			for (auto dim: tuple->dims) {
				llvm::Value *llvm_value = maybe_get_llvm_value(env,
															   dim->name,
															   dim->type);
				if (llvm_value == nullptr) {
					log("%s does not exist, going to try to recurse for it...");
					llvm_value = lower_decl(
						dim->name,
						builder,
						llvm_module,
						dim,
						env);
				}

				if (llvm_value != nullptr) {
					llvm::Constant *llvm_dim_const = llvm::dyn_cast<llvm::Constant>(llvm_value);
					if (llvm_dim_const == nullptr) {
						throw user_error(dim->get_location(),
										 "non-constant global dim element found %s",
										 dim->name.c_str());
					}
					llvm_struct_data.push_back(llvm_dim_const);
				} else {
					throw user_error(dim->get_location(),
									 "unable to find llvm_value for %s",
									 dim->str().c_str());
				}
			}

			log("found %d elements for struct", (int) llvm_struct_data.size());
			return llvm_create_struct_instance(
				name,
				llvm_module,
				llvm_struct_type, 
				llvm_struct_data);
		} else if (auto tuple_deref = dyncast<gen::tuple_deref_t>(value)) {
			assert(false);
			return nullptr;
		}
		dbg();

		throw user_error(value->get_location(), "unhandled lower for %s", value->str().c_str());
		return nullptr;
	}

	void lower_block(
		llvm::IRBuilder<> &builder,
		gen::block_t::ref block,
		std::map<std::string, llvm::Value *> &locals,
		const std::map<gen::block_t::ref, llvm::BasicBlock *, gen::block_t::comparator_t> &block_map,
		std::map<gen::block_t::ref, bool, gen::block_t::comparator_t> &blocks_visited,
		const env_t &env);

#define assert_not_impl() do { std::cout << llvm_print_module(*llvm_get_module(builder)) << std::endl; assert(false); } while (0)

	llvm::Value *lower_builtin(llvm::IRBuilder<> &builder, const std::string &name, const std::vector<llvm::Value *> &params) {
		log("lowering builtin %s(%s)...",
			name.c_str(),
			join_with(params, ", ", [](llvm::Value *lv) { return llvm_print(lv); }).c_str());

		if (name == "__builtin_word_size") {
			/* scheme({}, {}, Int) */
		} else if (name == "__builtin_min_int") {
			/* scheme({}, {}, Int) */
		} else if (name == "__builtin_max_int") {
			/* scheme({}, {}, Int) */
		} else if (name == "__builtin_multiply_int") {
			/* scheme({}, {}, type_arrows({Int, Int, Int})) */
		} else if (name == "__builtin_divide_int") {
			/* scheme({}, {}, type_arrows({Int, Int, Int})) */
		} else if (name == "__builtin_subtract_int") {
			/* scheme({}, {}, type_arrows({Int, Int, Int})) */
		} else if (name == "__builtin_add_int") {
			/* scheme({}, {}, type_arrows({Int, Int, Int})) */
		} else if (name == "__builtin_negate_int") {
			/* scheme({}, {}, type_arrows({Int, Int})) */
			return builder.CreateNeg(params[0]);
		} else if (name == "__builtin_abs_int") {
			/* scheme({}, {}, type_arrows({Int, Int})) */
		} else if (name == "__builtin_multiply_float") {
			/* scheme({}, {}, type_arrows({Float, Float, Float})) */
		} else if (name == "__builtin_divide_float") {
			/* scheme({}, {}, type_arrows({Float, Float, Float})) */
		} else if (name == "__builtin_subtract_float") {
			/* scheme({}, {}, type_arrows({Float, Float, Float})) */
		} else if (name == "__builtin_add_float") {
			/* scheme({}, {}, type_arrows({Float, Float, Float})) */
		} else if (name == "__builtin_abs_float") {
			/* scheme({}, {}, type_arrows({Float, Float})) */
		} else if (name == "__builtin_int_to_float") {
			/* scheme({}, {}, type_arrows({Int, Float})) */
		} else if (name == "__builtin_negate_float") {
			/* scheme({}, {}, type_arrows({Float, Float})) */
		} else if (name == "__builtin_add_ptr") {
			/* scheme({"a"}, {}, type_arrows({tp_a, Int, tp_a})) */
			return builder.CreateGEP(params[0], std::vector<llvm::Value *>{params[1]});
		} else if (name == "__builtin_ptr_eq") {
			/* scheme({"a"}, {}, type_arrows({tp_a, tp_a, Bool})) */
		} else if (name == "__builtin_ptr_ne") {
			/* scheme({"a"}, {}, type_arrows({tp_a, tp_a, Bool})) */
		} else if (name == "__builtin_ptr_load") {
			/* scheme({"a"}, {}, type_arrows({tp_a, tv_a})) */
			return builder.CreateLoad(params[0]);
		} else if (name == "__builtin_get_dim") {
			/* scheme({"a", "b"}, {}, type_arrows({tv_a, Int, tv_b})) */
		} else if (name == "__builtin_get_ctor_id") {
			/* scheme({"a"}, {}, type_arrows({tv_a, Int})) */
		} else if (name == "__builtin_int_eq") {
			/* scheme({}, {}, type_arrows({Int, Int, Bool})) */
		} else if (name == "__builtin_int_ne") {
			/* scheme({}, {}, type_arrows({Int, Int, Bool})) */
		} else if (name == "__builtin_int_lt") {
			/* scheme({}, {}, type_arrows({Int, Int, Bool})) */
		} else if (name == "__builtin_int_lte") {
			/* scheme({}, {}, type_arrows({Int, Int, Bool})) */
		} else if (name == "__builtin_int_gt") {
			/* scheme({}, {}, type_arrows({Int, Int, Bool})) */
		} else if (name == "__builtin_int_gte") {
			/* scheme({}, {}, type_arrows({Int, Int, Bool})) */
		} else if (name == "__builtin_float_eq") {
			/* scheme({}, {}, type_arrows({Float, Float, Bool})) */
		} else if (name == "__builtin_float_ne") {
			/* scheme({}, {}, type_arrows({Float, Float, Bool})) */
		} else if (name == "__builtin_float_lt") {
			/* scheme({}, {}, type_arrows({Float, Float, Bool})) */
		} else if (name == "__builtin_float_lte") {
			/* scheme({}, {}, type_arrows({Float, Float, Bool})) */
		} else if (name == "__builtin_float_gt") {
			/* scheme({}, {}, type_arrows({Float, Float, Bool})) */
		} else if (name == "__builtin_float_gte") {
			/* scheme({}, {}, type_arrows({Float, Float, Bool})) */
		} else if (name == "__builtin_print") {
			/* scheme({}, {}, type_arrows({String, type_unit(INTERNAL_LOC())})) */
		} else if (name == "__builtin_exit") {
			/* scheme({}, {}, type_arrows({Int, type_bottom()})) */
		} else if (name == "__builtin_calloc") {
			/* scheme({"a"}, {}, type_arrows({Int, tp_a})) */
		} else if (name == "__builtin_store_ref") {
			/* scheme({"a"}, {}, type_arrows({ type_operator(type_id(make_iid(REF_TYPE_OPERATOR)), tv_a), tv_a, type_unit(INTERNAL_LOC())})) */
		} else if (name == "__builtin_store_ptr") {
			/* scheme({"a"}, {}, type_arrows({ type_operator(type_id(make_iid(PTR_TYPE_OPERATOR)), tv_a), tv_a, type_unit(INTERNAL_LOC())})) */
		}

		log("Need an impl for " c_id("%s"), name.c_str());
		assert_not_impl();
		return nullptr;
	}

	llvm::Value *lower_literal(
		llvm::IRBuilder<> &builder,
		types::type_t::ref type,
		const token_t &token)
	{
		log("emitting literal %s :: %s",
			token.str().c_str(),
			type->str().c_str());
		if (type_equality(type, type_id(make_iid(INT_TYPE)))) {
			return builder.getZionInt(atoll(token.text.c_str()));
		}

		assert_not_impl();
		return nullptr;
	}

	llvm::Value *lower_value(
		llvm::IRBuilder<> &builder,
		gen::value_t::ref value,
		std::map<std::string, llvm::Value *> &locals,
		const std::map<gen::block_t::ref, llvm::BasicBlock *, gen::block_t::comparator_t> &block_map,
		std::map<gen::block_t::ref, bool, gen::block_t::comparator_t> &blocks_visited,
		const env_t &env)
	{
		assert(value != nullptr);

		/* make sure that the block that this value is defined in has been evaluated */
		lower_block(builder, value->parent.lock(), locals, block_map, blocks_visited, env);

		llvm::Value *llvm_previously_computed_value = get(locals, value->name, (llvm::Value *)nullptr);
		if (llvm_previously_computed_value != nullptr) {
			return llvm_previously_computed_value;
		}

		std::stringstream ss;
		value->render(ss);
		log("Lowering value %s", ss.str().c_str());
		if (auto unit = dyncast<gen::unit_t>(value)) {
			return llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo());
		} else if (auto literal = dyncast<gen::literal_t>(value)) {
			return lower_literal(builder, literal->type, literal->token);
		} else if (auto phi_node = dyncast<gen::phi_node_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto cast = dyncast<gen::cast_t>(value)) {
			auto llvm_inner_value = lower_value(builder, cast->value, locals, block_map, blocks_visited, env);
			auto llvm_value = builder.CreateBitCast(llvm_inner_value,
													get_llvm_type(builder, cast->type));
			locals[cast->name] = llvm_value;
			return llvm_value;
		} else if (auto function = dyncast<gen::function_t>(value)) {
			auto llvm_value = get(env, function->name, function->type, (llvm::Value *)nullptr);
			assert(llvm_value != nullptr);
			return llvm_value;
		} else if (auto builtin = dyncast<gen::builtin_t>(value)) {
			std::vector<llvm::Value *> params;
			for (auto param: builtin->params) {
				params.push_back(lower_value(builder, param, locals, block_map, blocks_visited, env));
			}

			return lower_builtin(builder, builtin->id.name, params);
		} else if (auto argument = dyncast<gen::argument_t>(value)) {
			assert_not_impl();
			auto llvm_value = get(locals, argument->name, (llvm::Value *)nullptr);
			assert(llvm_value != nullptr);
			return llvm_value;
		} else if (auto goto_ = dyncast<gen::goto_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto cond_branch = dyncast<gen::cond_branch_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto callsite = dyncast<gen::callsite_t>(value)) {
			std::vector<llvm::Value *> llvm_params;
			for (auto param: callsite->params) {
				llvm_params.push_back(lower_value(builder, param, locals, block_map, blocks_visited, env));
			}
			llvm::Value *llvm_callee = lower_value(builder, callsite->callable, locals, block_map, blocks_visited, env);
			auto llvm_callsite = llvm_create_call_inst(builder, llvm_callee, llvm_params);
			locals[callsite->name] = llvm_callsite;
			return llvm_callsite;
		} else if (auto return_ = dyncast<gen::return_t>(value)) {
			llvm::Value *llvm_return_value = lower_value(builder, return_->value, locals, block_map, blocks_visited, env);
			return builder.CreateRet(llvm_return_value);
		} else if (auto load = dyncast<gen::load_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto store = dyncast<gen::store_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto tuple = dyncast<gen::tuple_t>(value)) {
			std::vector<llvm::Value *> llvm_dims;
			for (auto dim: tuple->dims) {
				llvm_dims.push_back(lower_value(builder, dim, locals, block_map, blocks_visited, env));
			}
			auto llvm_type = get_llvm_type(builder, tuple->type);
			std::vector<llvm::Type *> alloc_terms{builder.getInt64Ty()};
			log("need to allocated a tuple of type %s", llvm_print(llvm_type).c_str());
			auto llvm_module = llvm_get_module(builder);
			auto llvm_alloc_func_decl = llvm::cast<llvm::Function>(
				llvm_module->getOrInsertFunction(
					"malloc",
					llvm_create_function_type(
						builder,
						alloc_terms,
						builder.getInt8Ty()->getPointerTo())));
			return builder.CreateBitCast(
				builder.CreateCall(
					llvm_alloc_func_decl,
					std::vector<llvm::Value *>{llvm_sizeof_type(builder, llvm_type->getPointerElementType())}),
				llvm_type);
		} else if (auto tuple_deref = dyncast<gen::tuple_deref_t>(value)) {
			std::stringstream ss;
			tuple_deref->render(ss);
			log("tuple_deref = %s", ss.str().c_str());
			llvm::Value *llvm_value = lower_value(builder, tuple_deref->value, locals, block_map, blocks_visited, env);

			/*
			   llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(
			   llvm_value->getType()->getPointerElementType());
			   assert(llvm_struct_type != nullptr);
			   std::cout << "llvm_struct_type = " << llvm_print(llvm_struct_type) << std::endl;
			   */

			std::cout << "llvm_value = " << llvm_print(llvm_value) << std::endl;
			std::cout << "llvm_value->getType = " << llvm_print(llvm_value->getType()) << std::endl;
			assert(tuple_deref->index >= 0);
			auto gep_path = std::vector<llvm::Value *>{
				builder.getInt32(0),
					builder.getInt32(tuple_deref->index)};
			return builder.CreateLoad(builder.CreateInBoundsGEP(llvm_value, gep_path));
		}
		assert_not_impl();
		return nullptr;
	}

	void lower_block(
		llvm::IRBuilder<> &builder,
		gen::block_t::ref block,
		std::map<std::string, llvm::Value *> &locals,
		const std::map<gen::block_t::ref, llvm::BasicBlock *, gen::block_t::comparator_t> &block_map,
		std::map<gen::block_t::ref, bool, gen::block_t::comparator_t> &blocks_visited,
		const env_t &env)
	{
		if (block == nullptr) {
			/* maybe the value we are lowering doesn't need a block */
			return;
		}

		if (block_map.at(block) == builder.GetInsertBlock()) {
			/* we're already checking this block right now, so be cool */
			return;
		}

		auto visited_iter = blocks_visited.find(block);
		if (visited_iter == blocks_visited.end()) {
			assert(!blocks_visited[block]);

			/* mark this block as grey */
			blocks_visited[block] = false;

			llvm::IRBuilderBase::InsertPointGuard ipg(builder);
			builder.SetInsertPoint(block_map.at(block));
			for (auto instruction: block->instructions) {
				locals[instruction->name] = lower_value(
					builder,
					instruction,
					locals,
					block_map,
					blocks_visited,
					env);
			}

			/* mark this block as white */
			blocks_visited[block] = true;
		} else {
			/* if this assert fires, then a value that dominates its own use somehow */
			assert(visited_iter->second);
		}
	}

	void lower_function(
		llvm::IRBuilder<> &builder,
		llvm::Module *llvm_module,
		std::string name,
		types::type_t::ref type,
		gen::function_t::ref function,
		llvm::Value *llvm_value,
		env_t &env)
	{
		std::cout << "Lowering " << name << std::endl;
		function->render(std::cout);

		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		llvm::Function *llvm_function = llvm::dyn_cast<llvm::Function>(llvm_value);
		assert(llvm_function != nullptr);

		// std::vector<std::shared_ptr<gen::argument_t>> args;
		std::map<std::string, llvm::Value *> locals;
		auto arg_iterator = llvm_function->arg_begin();
		for (auto arg: function->args) {
			if (arg_iterator == llvm_function->arg_end()) {
				log("arguments for %s :: %s and %s don't match",
					function->str().c_str(),
					function->type->str().c_str(),
					llvm_print(llvm_function).c_str());
				log("function = %s(%s)",
					function->name.c_str(),
					join_str(function->args, ", ").c_str());

				assert_not_impl();
			}

			locals[arg->name] = arg_iterator++;
		}

		std::map<gen::block_t::ref, llvm::BasicBlock *, gen::block_t::comparator_t> block_map;
		std::map<gen::block_t::ref, bool, gen::block_t::comparator_t> blocks_visited;

		for (auto block: function->blocks) {
			block_map[block] = llvm::BasicBlock::Create(builder.getContext(), block->name, llvm_function);
		}

		for (auto block: function->blocks) {
			lower_block(builder, block, locals, block_map, blocks_visited, env);
		}
	}

	void lower_populate(
			llvm::IRBuilder<> &builder,
			llvm::Module *llvm_module,
			std::string name,
            types::type_t::ref type,
			gen::value_t::ref value,
            llvm::Value *llvm_value,
			env_t &env)
	{
		debug_above(4, log("lower_populate(%s, ..., %s, ...)", name.c_str(), value->str().c_str()));

		assert(value != nullptr);

		if (auto unit = dyncast<gen::unit_t>(value)) {
			assert(false);
			return;
		} else if (auto literal = dyncast<gen::literal_t>(value)) {
			assert(false);
			return;
		} else if (auto phi_node = dyncast<gen::phi_node_t>(value)) {
			assert(false);
			return;
		} else if (auto cast = dyncast<gen::cast_t>(value)) {
			assert(false);
			return;
		} else if (auto function = dyncast<gen::function_t>(value)) {
            lower_function(builder, llvm_module, name, type, function, llvm_value, env);
			std::cout << llvm_print(llvm_value) << std::endl;
			return;
		} else if (auto builtin = dyncast<gen::builtin_t>(value)) {
			assert(false);
			return;
		} else if (auto argument = dyncast<gen::argument_t>(value)) {
			assert(false);
			return;
		} else if (auto goto_ = dyncast<gen::goto_t>(value)) {
			assert(false);
			return;
		} else if (auto cond_branch = dyncast<gen::cond_branch_t>(value)) {
			assert(false);
			return;
		} else if (auto callsite = dyncast<gen::callsite_t>(value)) {
			assert(false);
			return;
		} else if (auto return_ = dyncast<gen::return_t>(value)) {
			assert(false);
			return;
		} else if (auto load = dyncast<gen::load_t>(value)) {
			assert(false);
			return;
		} else if (auto store = dyncast<gen::store_t>(value)) {
			assert(false);
			return;
		} else if (auto tuple = dyncast<gen::tuple_t>(value)) {
			assert(false);
			return;
		} else if (auto tuple_deref = dyncast<gen::tuple_deref_t>(value)) {
			assert(false);
			return;
		}

		throw user_error(value->get_location(), "unhandled lower for %s", value->str().c_str());
	}

	int lower(std::string main_function, const gen::env_t &env) {
		llvm::LLVMContext context;
		llvm::Module *module = new llvm::Module("program", context);
		llvm::IRBuilder<> builder(context);

		try {

			lower::env_t lower_env;
			for (auto pair: env) {
				for (auto overload: pair.second) {
					const std::string &name = pair.first;
					types::type_t::ref type = overload.first;
					gen::value_t::ref value = overload.second;

					if (maybe_get_llvm_value(lower_env, name, type) != nullptr) {
						continue;
					}
					log("emitting " c_id("%s") " :: %s = %s",
						name.c_str(),
						type->str().c_str(),
						value->str().c_str());
					llvm::Constant *llvm_decl = lower_decl(name, builder, module, overload.second, lower_env);
					if (llvm_decl != nullptr) {
						/* we were able to create a lowered version of `name` */
						set_llvm_value(lower_env, name, type, llvm_decl, false /*allow_shadowing*/);
					} else {
						assert(false);
					}
				}
			}
			for (auto pair: env) {
				for (auto overload: pair.second) {
					const std::string &name = pair.first;
					types::type_t::ref type = overload.first;
					gen::value_t::ref value = overload.second;

					llvm::Value *llvm_value = get_llvm_value(lower_env, name, type);
					lower_populate(builder, module, name, type, value, llvm_value, lower_env);
				}
			}
			std::cout << llvm_print_module(*module) << std::endl;
			std::cout << "Created " << lower_env.size() << " named variables." << std::endl;
			return EXIT_SUCCESS;
		} catch (user_error &e) {
			print_exception(e);
			return EXIT_FAILURE;
		}
	}
}
