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

	void lower_decl(
			std::string name,
			llvm::IRBuilder<> &builder,
			llvm::Module *llvm_module,
			gen::value_t::ref value,
			env_t &env)
	{
		debug_above(4, log("lower_decl(%s, ..., %s, ...)", name.c_str(), value->str().c_str()));

		value = gen::resolve_proxy(value);
		if (value == nullptr) {
			log("skipping %s", name.c_str());
			return;
		}

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
			types::type_t::refs type_terms;
			unfold_binops_rassoc(ARROW_TYPE_OPERATOR, function->type, type_terms);
			llvm::Function *llvm_function = llvm_start_function(builder, llvm_module, type_terms, name + " :: " + function->type->repr());
			set_llvm_value(env, name, function->type, llvm_function, false /*allow_shadowing*/);
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

		throw user_error(value->get_location(), "unhandled lower for %s :: %s", value->str().c_str());
	}

#define assert_not_impl() do { std::cout << llvm_print(llvm_get_function(builder)) << std::endl; assert(false); } while (0)
	llvm::Value *lower_value(
		llvm::IRBuilder<> &builder,
		gen::value_t::ref value_,
		std::map<std::string, llvm::Value *> &locals,
		const std::map<gen::block_t::ref, llvm::BasicBlock *, gen::block_t::comparator_t> &block_map,
		const env_t &env)
	{
		auto value = gen::resolve_proxy(value_);
		if (value == nullptr) {
			log("skipping %s", value_->name.c_str());
			return nullptr;
		}

		llvm::Value *llvm_previously_computed_value = get(locals, value->name, (llvm::Value *)nullptr);
		if (llvm_previously_computed_value != nullptr) {
			return llvm_previously_computed_value;
		}

		if (auto literal = dyncast<gen::literal_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto phi_node = dyncast<gen::phi_node_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto cast = dyncast<gen::cast_t>(value)) {
			auto llvm_inner_value = lower_value(builder, cast->value, locals, block_map, env);
			auto llvm_value = builder.CreateBitCast(
				llvm_inner_value, get_llvm_type(builder, cast->type));
			locals[cast->name] = llvm_value;
			return llvm_value;
		} else if (auto function = dyncast<gen::function_t>(value)) {
			auto llvm_value = get(env, function->name, function->type, (llvm::Value *)nullptr);
			assert(llvm_value != nullptr);
			return llvm_value;
		} else if (auto builtin = dyncast<gen::builtin_t>(value)) {
			assert_not_impl();
			return nullptr;
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
				llvm_params.push_back(lower_value(builder, param, locals, block_map, env));
			}
			llvm::Value *llvm_callee = lower_value(builder, callsite->callable, locals, block_map, env);
			auto llvm_callsite = llvm_create_call_inst(builder, llvm_callee, llvm_params);
			locals[callsite->name] = llvm_callsite;
			return llvm_callsite;
		} else if (auto return_ = dyncast<gen::return_t>(value)) {
			llvm::Value *llvm_return_value = lower_value(builder, return_->value, locals, block_map, env);
			return builder.CreateRet(llvm_return_value);
		} else if (auto load = dyncast<gen::load_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto store = dyncast<gen::store_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto tuple = dyncast<gen::tuple_t>(value)) {
			assert_not_impl();
			return nullptr;
		} else if (auto tuple_deref = dyncast<gen::tuple_deref_t>(value)) {
			assert_not_impl();
			return nullptr;
		}
		assert_not_impl();
		return nullptr;
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
		llvm::IRBuilderBase::InsertPointGuard ipg(builder);
		llvm::Function *llvm_function = llvm::dyn_cast<llvm::Function>(llvm_value);
		assert(llvm_function != nullptr);

		// std::vector<std::shared_ptr<gen::argument_t>> args;
		std::map<std::string, llvm::Value *> locals;
		llvm_function->args();
		auto arg_iterator = llvm_function->arg_begin();
		for (auto arg: function->args) {
			assert(arg_iterator != llvm_function->arg_end());
			locals[arg->name] = arg_iterator++;
		}

		std::map<gen::block_t::ref, llvm::BasicBlock *, gen::block_t::comparator_t> block_map;
		for (auto block: function->blocks) {
			block_map[block] = llvm::BasicBlock::Create(builder.getContext(), block->name, llvm_function);
		}
		std::cout << llvm_print(llvm_function) << std::endl;

		for (auto block: function->blocks) {
			builder.SetInsertPoint(block_map[block]);
			for (auto instruction: block->instructions) {
				locals[instruction->name] = lower_value(
					builder,
					instruction,
					locals,
					block_map,
					env);
			}
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

		value = gen::resolve_proxy(value);
		if (value == nullptr) {
			log("skipping %s", name.c_str());
			return;
		}

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

		throw user_error(value->get_location(), "unhandled lower for %s :: %s", value->str().c_str());
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

					log("emitting " c_id("%s") " :: %s = %s",
						name.c_str(),
						type->str().c_str(),
						value->str().c_str());
					lower_decl(pair.first, builder, module, overload.second, lower_env);
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
