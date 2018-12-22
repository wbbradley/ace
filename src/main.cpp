#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include "lexer.h"
#include "logger_decls.h"
#include "logger.h"
#include "tests.h"
#include "compiler.h"
#include "disk.h"
#include <sys/wait.h>
#include <chrono>
#include "ast.h"
#include "unification.h"
#include "env.h"

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
		execvp(executable.c_str(), const_cast<char **>(&args[0]));
	}

	return 0;
}

void handle_sigint(int sig) {
	print_stacktrace(stderr, 100);
	exit(2);
}
		
		
int main(int argc, char *argv[]) {
	//setenv("DEBUG", "8", 1 /*overwrite*/);
	signal(SIGINT, &handle_sigint);
	init_dbg();
	std::shared_ptr<logger> logger(std::make_shared<standard_logger>("", "."));
    std::string cmd;
	if (argc >= 2) {
		cmd = argv[1];
	} else {
		return usage();
	}

	if (argc >= 3) {

		compiler_t compiler(argv[2]);

		setenv("NO_STD_LIB", "1", 1 /*overwrite*/);
		setenv("NO_STD_MAIN", "1", 1 /*overwrite*/);
		setenv("NO_BUILTINS", "1", 1 /*overwrite*/);

		if (cmd == "find") {
			std::cout << resolve_module_filename(INTERNAL_LOC(), argv[2], "") << std::endl;
			return EXIT_SUCCESS;
		} else if (cmd == "parse") {
			if (compiler.parse_program()) {
				for (auto decl : compiler.program->decls) {
					log_location(log_info, decl->var.location, "%s = %s", decl->var.str().c_str(),
							decl->value->str().c_str());
				}
				return EXIT_SUCCESS;
			}
			return EXIT_FAILURE;
		} else if (cmd == "test") {
			assert(alphabetize(0) == "a");
			assert(alphabetize(1) == "b");
			assert(alphabetize(2) == "c");
			assert(alphabetize(26) == "aa");
			assert(alphabetize(27) == "ab");
		} else if (cmd == "compile") {

			if (compiler.parse_program()) {
#if 0
				for (auto decl : compiler.program->decls) {
					log_location(log_info, decl->var.location, "%s = %s", decl->var.str().c_str(),
							decl->value->str().c_str());
				}
#endif
				bitter::program_t *program = compiler.program;

				env_t env;
				location_t l_ = INTERNAL_LOC();
				env.map["+"] = forall({}, type_arrow(l_, type_int(l_), type_arrow(l_, type_int(l_), type_int(l_))));
				env.map["*"] = forall({}, type_arrow(l_, type_int(l_), type_arrow(l_, type_int(l_), type_int(l_))));
				env.map["-"] = forall({}, type_arrow(l_, type_int(l_), type_arrow(l_, type_int(l_), type_int(l_))));
				env.map["/"] = forall({}, type_arrow(l_, type_int(l_), type_arrow(l_, type_int(l_), type_int(l_))));
				env.map["unit"] = forall({}, type_unit(l_));
				env.map["true"] = forall({}, type_bool(l_));
				env.map["false"] = forall({}, type_bool(l_));

				for (bitter::decl_t *decl : program->decls) {
					constraints_t constraints;
					try {
						// log("type checking %s", decl->var.str().c_str());
						types::type_t::ref ty = infer(decl->value, env, constraints);
						types::type_t::map subst = solver({}, constraints, env);

						ty = ty->rebind(subst);
						// log(">> %s", str(constraints).c_str());
						// log(">> %s", str(subst).c_str());
						auto scheme = ty->generalize(env)->normalize();
						env = env.extend(decl->var, scheme);

#if 0
						for (auto pair : subst) {
							std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->str() << C_RESET << std::endl;
						}

						for (auto pair : env.map) {
							std::cout << C_ID "------------------------------" C_RESET << std::endl;
							std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->normalize()->str() << C_RESET << std::endl;
						}
#endif
						log_location(log_info, decl->var.location, "info: %s :: %s", decl->var.str().c_str(), scheme->str().c_str());
					} catch (user_error &e) {
						print_exception(e);
						/* keep trying other decls, and pretend like this function gives back
						 * whatever the user wants... */
						env = env.extend(decl->var, type_arrow(INTERNAL_LOC(), type_variable(INTERNAL_LOC()), type_variable(INTERNAL_LOC()))->generalize(env)->normalize());
					}

				}
#if 0
				for (auto pair : env.map) {
					std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->str() << C_RESET << std::endl;
				}
				std::cout << "unification solver..." << std::endl;
#endif

#if 0
				try {
					types::type_t::map subst = solver({}, constraints, env);

					for (auto pair : subst) {
						std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->str() << C_RESET << std::endl;
					}

					for (auto pair : env.map) {
						std::cout << C_ID "------------------------------" C_RESET << std::endl;
						std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->str() << C_RESET << std::endl;
						std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->normalize()->str() << C_RESET << std::endl;
					}
				} catch (user_error &e) {
					print_exception(e);
					/* keep trying other decls... */
				}
#endif

				return EXIT_SUCCESS;
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

