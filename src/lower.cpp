#include "lower.h"
#include "llvm_zion.h"
#include "llvm_utils.h"

namespace lower {
	llvm::Value *lower(
			llvm::IRBuilder<> &builder,
			gen::value_t::ref value,
			const gen::env_t &env)
	{
		debug_above(4, log("lower(..., %s, ...)", value->str().c_str()));

		value = gen::resolve_proxy(value);

		if (auto unit = dyncast<gen::unit_t>(value)) {
			assert(false);
			return nullptr;
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
			assert(false);
			return nullptr;
		} else if (auto builtin = dyncast<gen::builtin_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto argument = dyncast<gen::argument_t>(value)) {
			assert(false);
			return nullptr;
		} else if (auto global_ref = dyncast<gen::global_ref_t>(value)) {
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
			assert(false);
			return nullptr;
		} else if (auto tuple_deref = dyncast<gen::tuple_deref_t>(value)) {
			assert(false);
			return nullptr;
		}

		throw user_error(value->get_location(), "unhandled lower for %s :: %s", value->str().c_str());
	}

	int lower(std::string main_function, const gen::env_t &env) {
		llvm::LLVMContext context;
		llvm::Module *module = new llvm::Module("program", context);
		llvm::IRBuilder<> builder(context);

		try {
			for (auto pair: env) {
				for (auto overload: pair.second) {
					const std::string &name = pair.first;
					types::type_t::ref type = overload.first;
					gen::value_t::ref value = overload.second;

					log("emitting " c_id("%s") " :: %s = %s",
							name.c_str(),
							type->str().c_str(),
							value->str().c_str());
					llvm::Value *llvm_value = lower(builder, overload.second, env);
					llvm_value->setName(pair.first + " :: " + overload.first->repr());
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
