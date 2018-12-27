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

const bool debug_compiled_env = getenv("SHOW_ENV") != nullptr;
const bool debug_types = getenv("SHOW_TYPES") != nullptr;

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
		
void check(identifier_t id, bitter::expr_t *expr, env_t &env) {
	constraints_t constraints;
	// std::cout << C_ID "------------------------------" C_RESET << std::endl;
	// log("type checking %s", id.str().c_str());
	types::type_t::ref ty = infer(expr, env, constraints);
	auto bindings = solver({}, constraints, env);

	// log("GOT ty = %s", ty->str().c_str());
	ty = ty->rebind(bindings);
	// log("REBOUND ty = %s", ty->str().c_str());
	// log(">> %s", str(constraints).c_str());
	// log(">> %s", str(subst).c_str());
	auto scheme = ty->generalize(env)->normalize();
	// log("NORMALIED ty = %s", n->str().c_str());

	env.extend(id, scheme);

	if (debug_types) {
		log_location(id.location, "info: %s :: %s",
				id.str().c_str(), scheme->str().c_str());
	}
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
					log_location(decl->var.location, "%s = %s", decl->var.str().c_str(),
							decl->value->str().c_str());
				}
				for (auto type_class : compilation->program->type_classes) {
					log_location(type_class->id.location, "%s", type_class->str().c_str());
				}
				for (auto instance : compilation->program->instances) {
					log_location(instance->type_class_id.location, "%s", instance->str().c_str());
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
			auto compilation = compiler::parse_program(user_program_name);
			if (compilation != nullptr) {
				bitter::program_t *program = compilation->program;

				env_t env;
				location_t l_ = INTERNAL_LOC();
				auto Int = type_id(make_iid("Int"));
				auto Float = type_id(make_iid("Float"));

				env.map["unit"] = scheme({}, {}, type_unit(l_));
				env.map["true"] = scheme({}, {}, type_bool(l_));
				env.map["false"] = scheme({}, {}, type_bool(l_));
				env.map["__multiply_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
				env.map["__divide_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
				env.map["__subtract_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
				env.map["__add_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
				env.map["__multiply_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
				env.map["__divide_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
				env.map["__subtract_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
				env.map["__add_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));

				std::map<std::string, bitter::type_class_t *> type_class_map;

				/* first, introduce all the type class signatures into the env, and build up an
				 * index of type_class names */
				for (bitter::type_class_t *type_class : program->type_classes) {
					try {
						if (in(type_class->id.name, type_class_map)) {
							auto error = user_error(type_class->id.location, "type class name %s is already taken", type_class->id.str().c_str());
							error.add_info(type_class_map[type_class->id.name]->get_location(),
									"see earlier type class declaration here");
							throw error;
						} else {
							type_class_map[type_class->id.name] = type_class;
						}

						auto predicates = type_class->superclasses;
						predicates.insert(type_class->id.name);

						types::type_t::map bindings;
						bindings[type_class->type_var_id.name] = type_variable(gensym(type_class->type_var_id.location), predicates);

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
						/* and continue */
					}
				}

				for (bitter::decl_t *decl : program->decls) {
					try {
						check(decl->var, decl->value, env);
					} catch (user_error &e) {
						print_exception(e);
						/* keep trying other decls, and pretend like this function gives back
						 * whatever the user wants... */
						env.extend(decl->var, type_arrow(INTERNAL_LOC(), type_variable(INTERNAL_LOC()), type_variable(INTERNAL_LOC()))->generalize(env)->normalize());
					}
				}

				for (bitter::instance_t *instance : program->instances) {
					try {
						log_location(
								instance->type_class_id.location,
								"checking that member functions of instance [%s %s] type check",
								instance->type_class_id.str().c_str(),
								instance->type->str().c_str());
						auto iter = type_class_map.find(instance->type_class_id.name);
						if (iter == type_class_map.end()) {
							auto error = user_error(instance->type_class_id.location,
									"could not find type class for instance %s %s",
									instance->type_class_id.str().c_str(),
									instance->type->str().c_str());
							auto leaf_name = split(instance->type_class_id.name, ".").back();
							for (auto type_class : program->type_classes) {
								if (type_class->id.name.find(leaf_name) != std::string::npos) {
									error.add_info(type_class->id.location, "did you mean %s?",
											type_class->id.str().c_str());
								}
							}
							throw error;
						}

						/* first put an instance requirement on any superclasses of the associated
						 * type_class */
						bitter::type_class_t *type_class = iter->second;
						// TODO: env.instance_requirements.push_back(type_class->superclasses * instance->type);

						std::set<std::string> names_checked;

						/* make a template for the type that the instance implementation should
						 * conform to */
						types::type_t::map subst;
						subst[type_class->type_var_id.name] = instance->type->generalize({})->instantiate(INTERNAL_LOC());

						// check whether this instance properly implements the given type class
						for (auto pair : type_class->overloads) {
							auto name = pair.first;
							auto type = pair.second;
							for (auto decl : instance->decls) {
								if (decl->var.name == split(name, ".").back()) {
									if (in(name, names_checked)) {
										throw user_error(
												decl->get_location(),
											   	"name %s already duplicated in this instance",
												decl->var.str().c_str());
									}
									names_checked.insert(decl->var.name);

									env_t local_env{env};
									local_env.instance_requirements.resize(0);
									check(
											identifier_t{instance->type->repr()+"."+decl->var.name, decl->var.location},
											new bitter::as_t(decl->value, type->rebind(subst)->generalize(local_env)->instantiate(INTERNAL_LOC()), false),
										   	local_env);
									assert(local_env.instance_requirements.size() == 0);
								}
							}
						}
						/* check for unrelated declarations inside of an instance */
						for (auto decl : instance->decls) {
							if (!in(decl->var.name, names_checked)) {
								throw user_error(decl->var.location, "extraneous declaration %s found in instance %s %s",
										decl->var.str().c_str(),
										type_class->id.str().c_str(),
										instance->type->str().c_str());
							}
						}
					} catch (user_error &e) {
						print_exception(e);
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

