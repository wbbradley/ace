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
		std::string user_program_name = argv[2];

		setenv("NO_STD_LIB", "1", 1 /*overwrite*/);
		setenv("NO_STD_MAIN", "1", 1 /*overwrite*/);
		setenv("NO_BUILTINS", "1", 1 /*overwrite*/);

		if (cmd == "find") {
			std::cout << compiler::resolve_module_filename(INTERNAL_LOC(), user_program_name, "") << std::endl;
			return EXIT_SUCCESS;
		} else if (cmd == "parse") {
			auto compilation = compiler::parse_program(user_program_name);
			if (compilation != nullptr) {
				for (auto decl : compilation->program->decls) {
					log_location(log_info, decl->var.location, "%s = %s", decl->var.str().c_str(),
							decl->value->str().c_str());
				}
				for (auto type_class : compilation->program->type_classes) {
					log_location(log_info, type_class->id.location, "%s", type_class->str().c_str());
				}
				for (auto instance : compilation->program->instances) {
					log_location(log_info, instance->type_class_id.location, "%s", instance->str().c_str());
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
			bool debug_compiled_env = getenv("SHOW_ENV") != nullptr;
			bool debug_types = getenv("SHOW_TYPES") != nullptr;
			auto compilation = compiler::parse_program(user_program_name);
			if (compilation != nullptr) {
				bitter::program_t *program = compilation->program;

				env_t env;
				location_t l_ = INTERNAL_LOC();
				env.map["unit"] = scheme({}, {}, type_unit(l_));
				env.map["true"] = scheme({}, {}, type_bool(l_));
				env.map["false"] = scheme({}, {}, type_bool(l_));

				/* first, introduce all the type class signatures into the env */
				for (bitter::type_class_t *type_class : program->type_classes) {
					auto predicates = type_class->superclasses;
					predicates.insert(type_class->id.name);

					types::type_t::map bindings;
					bindings[type_class->type_var_id.name] = type_variable(gensym(type_class->type_var_id.location), predicates);

					try {
						for (auto pair : type_class->overloads) {
							if (in(pair.first, env.map)) {
								auto error = user_error(pair.second->get_location(),
										"the name " c_id("%s") " is already in use", pair.first.c_str());
								error.add_info(env.map[pair.first]->get_location(), "see first declaration here");
								throw error;
							}

							env.extend(
									identifier_t{pair.first, pair.second->get_location()},
									pair.second->rebind(bindings)->generalize({})->normalize());
						}
					} catch (user_error &e) {
						print_exception(e);
					}
				}

				for (bitter::decl_t *decl : program->decls) {
					constraints_t constraints;
					try {
						// std::cout << C_ID "------------------------------" C_RESET << std::endl;
						// log("type checking %s", decl->var.str().c_str());
						types::type_t::ref ty = infer(decl->value, env, constraints);
						auto bindings = solver({}, constraints, env);

						// log("GOT ty = %s", ty->str().c_str());
						ty = ty->rebind(bindings);
						// log("REBOUND ty = %s", ty->str().c_str());
						// log(">> %s", str(constraints).c_str());
						// log(">> %s", str(subst).c_str());
						auto scheme = ty->generalize(env)->normalize();
						// log("NORMALIED ty = %s", n->str().c_str());

						env.extend(decl->var, scheme);

						if (debug_types) {
							log_location(log_info, decl->var.location, "info: %s :: %s",
									decl->var.str().c_str(), scheme->str().c_str());
						}
					} catch (user_error &e) {
						print_exception(e);
						/* keep trying other decls, and pretend like this function gives back
						 * whatever the user wants... */
						env.extend(decl->var, type_arrow(INTERNAL_LOC(), type_variable(INTERNAL_LOC()), type_variable(INTERNAL_LOC()))->generalize(env)->normalize());
					}

				}

				if (debug_compiled_env) {
					for (auto pair : env.map) {
						// std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->str() << C_RESET << std::endl;
						std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->normalize()->str() << C_RESET << std::endl;
					}
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

