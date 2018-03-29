#include "zion.h"
#include "llvm_zion.h"
#include "llvm_utils.h"
#include <sstream>
#include "logger_decls.h"
#include "dbg.h"

bool test_llvm_builder() {
	/* basic sanity check that the builder integration is working. */
	llvm::LLVMContext context;
	llvm::Module *module = new llvm::Module("top", context);
	llvm::IRBuilder<> builder(context); 

	llvm::StructType *ver0 = llvm_create_struct_type(
			builder, "ver0", std::vector<llvm::Type*>{builder.getInt32Ty(),
			builder.getInt32Ty()});

	llvm::FunctionType *funcType = llvm::FunctionType::get(builder.getVoidTy(), {ver0->getPointerTo()}, false);
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

	llvm::Function::arg_iterator args = mainFunc->arg_begin();
	printf("%s\n", llvm_print(args->getType()).c_str());
	std::vector<llvm::Value *> gep_path = std::vector<llvm::Value *>{
		builder.getInt32(0),
		builder.getInt32(1),
	};

	printf("type of GEP is %s\n",
		   	llvm_print(
				builder.CreateInBoundsGEP(args, gep_path)->getType()).c_str());

	builder.CreateRetVoid();

	FILE *fp = fopen("jit.llir", "wt");
	fprintf(fp, "%s\n", llvm_print_module(*module).c_str());
	fclose(fp);

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
