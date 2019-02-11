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

	void set_env_value(env_t &env, std::string name, types::type_t::ref type, llvm::Value *llvm_value, bool allow_shadowing) {
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
		debug_above(4, log("lower(%s, ..., %s, ...)", name.c_str(), value->str().c_str()));

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
			set_env_value(env, name, function->type, llvm_function, false /*allow_shadowing*/);
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
			std::cout << llvm_print_module(*module) << std::endl;
			return EXIT_SUCCESS;
		} catch (user_error &e) {
			print_exception(e);
			return EXIT_FAILURE;
		}
	}
}
