#include "llvm_zion.h"
#include <sstream>
#include "logger_decls.h"
#include "dbg.h"

bool test_llvm_builder() {
	/* basic sanity check that the builder integration is working. */
	llvm::LLVMContext context;
	llvm::Module *module = new llvm::Module("top", context);
	llvm::IRBuilder<> builder(context); 

	llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getVoidTy(), false);
	llvm::Function *mainFunc = llvm::Function::Create(
			funcType, llvm::Function::ExternalLinkage, "main", module);

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(context, "entrypoint", mainFunc);
	builder.SetInsertPoint(entry);

	llvm::Value *helloWorld = builder.CreateGlobalStringPtr("hello world\n");

	std::vector<llvm::Type *> putsArgs;
	putsArgs.push_back(builder.getInt8Ty()->getPointerTo());
	llvm::ArrayRef<llvm::Type*> argsRef(putsArgs);

	llvm::FunctionType *putsType = llvm::FunctionType::get(builder.getInt32Ty(), argsRef, false);
	llvm::Constant *putsFunc = module->getOrInsertFunction("puts", putsType);

	builder.CreateCall(putsFunc, helloWorld);

	builder.CreateRetVoid();
	module->dump();

	std::stringstream ss;
	llvm::raw_os_ostream os(ss);
	if (!verifyModule(*module, &os)) {
		debug_above(8, log(log_info, "LLVM verification succeeded"));
		return true;
	} else {
		os.flush();
		debug_above(8, log(log_error, "LLVM verification failed:\n%s", ss.str().c_str()));
		return false;
	}
}
