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
				std::cout << compiler.program;
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
			assert(alphabetize(0) == "a");
			assert(alphabetize(1) == "b");
			assert(alphabetize(2) == "c");
			assert(alphabetize(26) == "aa");
			assert(alphabetize(27) == "ab");

			if (compiler.parse_program()) {
				for (auto decl : compiler.program->decls) {
					log_location(log_info, decl->var.location, "%s = %s", decl->var.str().c_str(),
							decl->value->str().c_str());
				}
				bitter::program_t *program = compiler.program;

				env_t env;
				env.map["+"] = forall({}, type_arrow(type_int(), type_arrow(type_int(), type_int())));
				env.map["*"] = forall({}, type_arrow(type_int(), type_arrow(type_int(), type_int())));
				env.map["-"] = forall({}, type_arrow(type_int(), type_arrow(type_int(), type_int())));
				env.map["/"] = forall({}, type_arrow(type_int(), type_arrow(type_int(), type_int())));
				env.map["unit"] = forall({}, type_unit());
				env.map["true"] = forall({}, type_bool(INTERNAL_LOC()));
				env.map["false"] = forall({}, type_bool(INTERNAL_LOC()));

				constraints_t constraints;
				for (bitter::decl_t *decl : program->decls) {
					try {
						types::type_t::ref ty = infer(decl->value, env, constraints);
						env = env.extend(decl->var, forall({}, ty));
					} catch (user_error &e) {
						print_exception(e);
						/* keep trying other decls... */
					}
				}

				for (auto pair : constraints) {
					assert(pair.first != nullptr);
					assert(pair.second != nullptr);
					debug_above(9, log_location(log_info, pair.first->get_location(), "constraining %s to %s",
							   	pair.first->str().c_str(),
							   	pair.second->str().c_str()));
					debug_above(9, log_location(log_info, pair.second->get_location(), "constraining %s to %s",
							   	pair.first->str().c_str(),
							   	pair.second->str().c_str()));
				}
				for (auto pair : env.map) {
					std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->str() << C_RESET << std::endl;
				}
				std::cout << "unification..." << std::endl;
				types::type_t::map subst;
				for (auto pair : constraints) {
					try {
						auto s = unify(pair.first, pair.second);
						subst = compose(subst, s);
					} catch (user_error &e) {
						print_exception(e);
						/* keep trying other decls... */
					}
				}

				for (auto pair : subst) {
					std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->str() << C_RESET << std::endl;
				}

				env = env.rebind(subst);
				for (auto pair : env.map) {
					std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->normalize()->str() << C_RESET << std::endl;
				}

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

