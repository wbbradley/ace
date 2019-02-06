#include "lower.h"
#include "llvm_zion.h"

namespace lower {
	int run(std::string main_function, const gen::env_t &env) {
		llvm::LLVMContext context;
		llvm::Module *module = new llvm::Module("program", context);
		llvm::IRBuilder<> builder(context);

		for (auto pair: env) {
			for (auto overload: pair.second) {
				const std::string &name = pair.first;
				types::type_t::ref type = overload.first;
				gen::value_t::ref value = overload.second;

				log("emitting " c_id("%s") " :: %s = %s",
						name.c_str(),
						type->str().c_str(),
						value->str().c_str());
			}
		}

		return 0;
	}
}
