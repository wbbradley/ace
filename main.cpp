#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include "lexer.h"
#include "logger_decls.h"
#include "logger.h"
#include "tests.h"
#include "llvm_utils.h"
#include "compiler.h"
#include "disk.h"
#include <sys/wait.h>

int usage() {
	log(log_error, "available commands: test, read-ir, compile, bc, run, fmt, bin");
	return EXIT_FAILURE;
}

int run_program(std::string executable, std::vector<const char *> args)  {
	pid_t pid = fork();

	if (pid == -1) {
		perror(string_format("unable to fork() child process %s",
					executable.c_str()).c_str());
	} else if (pid > 0) {
		/* parent */
		int status;

		// printf("Child has pid %ld\n", (long)pid);

		if (::wait(&status) == -1) {
			perror("wait()");
		} else {
			if (WIFEXITED(status)) {
				/* did the child terminate normally? */
				// printf("%ld exited with return code %d\n", (long)pid, WEXITSTATUS(status));
				return WEXITSTATUS(status);
			} else if (WIFSIGNALED(status)) {
				/* was the child terminated by a signal? */
				// printf("%ld terminated because it didn't catch signal number %d\n", (long)pid, WTERMSIG(status));
				return -1;
			}
		}
	} else {
		/* child */
		execvp(executable.c_str(), (char **)&args[0]);
	}

	return 0;
}

int main(int argc, char *argv[]) {
	ptr<logger> logger(make_ptr<standard_logger>(debug_else("", "zion.log"), "."));
    std::string cmd;
	if (argc >= 2) {
		cmd = argv[1];
	} else {
		return usage();
	}

	if (cmd == "test") {
		std::string filter = (argc == 3 ? argv[2] : "");
		std::vector<std::string> excludes;
		if (getenv("T")) {
			filter = getenv("T");
		}

		if (getenv("EXCLUDE")) {
			excludes = split(getenv("EXCLUDE"));
		}

		if (run_tests(filter, excludes)) {
			return EXIT_SUCCESS;
		} else {
			return EXIT_FAILURE;
		}
	} else if (argc >= 3) {
		status_t status;
		compiler_t compiler(argv[2], {".", "lib", "tests"});

		if (cmd == "read-ir") {
				compiler.llvm_load_ir(status, argv[2]);
            if (!!status) {
                return EXIT_SUCCESS;
            } else {
                return EXIT_FAILURE;
            }
        } else if (cmd == "find") {
            std::string filename;
            compiler.resolve_module_filename(status, INTERNAL_LOC(),
                    argv[2], filename);

			if (!!status) {
                std::cout << filename << std::endl;
                return EXIT_SUCCESS;
			}
			return EXIT_FAILURE;
        } else if (cmd == "compile") {
			compiler.build_parse_modules(status);

			if (!!status) {
				compiler.build_type_check_and_code_gen(status);
				if (!!status) {
					return EXIT_SUCCESS;
				}
			}
			return EXIT_FAILURE;
        } else if (cmd == "fmt") {
			compiler.build_parse_modules(status);

			if (!!status) {
				write_fp(stdout, "%s",
						compiler.dump_program_text(strip_zion_extension(argv[2])).c_str());
			}

			if (!!status) {
				return EXIT_SUCCESS;
			} else {
				return EXIT_FAILURE;
			}
        } else if (cmd == "run") {
			compiler.build_parse_modules(status);

			if (!!status) {
				compiler.build_type_check_and_code_gen(status);
				if (!!status) {
					using namespace llvm;
					// Create the JIT.  This takes ownership of the module.
					std::string ErrStr;

					auto TheExecutionEngine = EngineBuilder(std::unique_ptr<llvm::Module>(compiler.llvm_get_program_module()))
						.setErrorStr(&ErrStr).create();

					if (!TheExecutionEngine) {
						fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
						exit(1);
					}

					void *fn_main = TheExecutionEngine->getPointerToNamedFunction("main");
					log("compiled function pointer at 0x%08xll", (long long)fn_main);
#if 0
					FunctionPassManager OurFPM(TheExecutionEngine->Modules[0]);

					// Set up the optimizer pipeline.  Start with registering info about how the
					// target lays out data structures.
					OurFPM.add(new DataLayout(*TheExecutionEngine->getDataLayout()));
					// Provide basic AliasAnalysis support for GVN.
					OurFPM.add(createBasicAliasAnalysisPass());
					// Promote allocas to registers.
					OurFPM.add(createPromoteMemoryToRegisterPass());
					// Do simple "peephole" optimizations and bit-twiddling optzns.
					OurFPM.add(createInstructionCombiningPass());
					// Reassociate expressions.
					OurFPM.add(createReassociatePass());
					// Eliminate Common SubExpressions.
					OurFPM.add(createGVNPass());
					// Simplify the control flow graph (deleting unreachable blocks, etc).
					OurFPM.add(createCFGSimplificationPass());

					OurFPM.doInitialization();

					// Set the global so the code gen can use this.
					TheFPM = &OurFPM;

					auto executable_filename = compiler.get_executable_filename();
					int ret = compiler.emit_built_program(status, executable_filename);
					if (!!status && !ret) {
						std::vector<const char *> args;
						std::string final_exe = std::string("./") + executable_filename;
						args.push_back(final_exe.c_str());
						for (int i=3; i<argc; i++) {
							args.push_back(argv[i]);
						}
						args.push_back(nullptr);
						return run_program(final_exe, args);
					} else {
						return ret;
					}
#endif
				}
			}
			return EXIT_FAILURE;
		} else if (cmd == "obj") {
			compiler.build_parse_modules(status);

			if (!!status) {
				compiler.build_type_check_and_code_gen(status);
				if (!!status) {
					int ret = compiler.emit_object_file(status);
					if (!!status && !ret) {
						return EXIT_SUCCESS;
					} else {
						return ret;
					}
				}
			}
			return EXIT_FAILURE;
        } else if (cmd == "bc") {
			compiler.build_parse_modules(status);

			if (!!status) {
				compiler.build_type_check_and_code_gen(status);
				if (!!status) {
					auto executable_filename = compiler.get_executable_filename();
					int ret = compiler.emit_built_program(status, executable_filename);
					if (!!status && !ret) {
						return EXIT_SUCCESS;
					} else {
						return ret;
					}
				}
			}
			return EXIT_FAILURE;
		} else {
			panic(string_format("bad CLI invocation of %s", argv[0]));
		}
    } else {
		return usage();
	}

	return EXIT_SUCCESS;
}

