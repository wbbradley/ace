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

using namespace bitter;

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
		
void check(identifier_t id, expr_t *expr, env_t &env) {
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

	env.extend(id, scheme, false /*allow_subscoping*/);

	if (debug_types) {
		log_location(id.location, "info: %s :: %s",
				id.str().c_str(), scheme->str().c_str());
	}
}
		
void initialize_default_env(env_t &env) {
	auto Int = type_id(make_iid("Int"));
	auto Float = type_id(make_iid("Float"));

	env.map["unit"] = scheme({}, {}, type_unit(INTERNAL_LOC()));
	env.map["true"] = scheme({}, {}, type_bool(INTERNAL_LOC()));
	env.map["false"] = scheme({}, {}, type_bool(INTERNAL_LOC()));
	env.map["__multiply_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
	env.map["__divide_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
	env.map["__subtract_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
	env.map["__add_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
	env.map["__multiply_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
	env.map["__divide_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
	env.map["__subtract_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
	env.map["__add_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
}

std::map<std::string, type_class_t *> check_type_classes(const std::vector<type_class_t *> &type_classes, env_t &env) {
	std::map<std::string, type_class_t *> type_class_map;

	/* introduce all the type class signatures into the env, and build up an
	 * index of type_class names */
	for (type_class_t *type_class : type_classes) {
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
						pair.second->rebind(bindings)->generalize({})->normalize(),
						false /*allow_duplicates*/);
			}
		} catch (user_error &e) {
			print_exception(e);
			/* and continue */
		}
	}
	return type_class_map;
}

void check_decls(const std::vector<decl_t *> &decls, env_t &env) {
	for (decl_t *decl : decls) {
		try {
			check(decl->var, decl->value, env);
		} catch (user_error &e) {
			print_exception(e);

			/* keep trying other decls, and pretend like this function gives back
			 * whatever the user wants... */
			env.extend(
					decl->var,
					type_arrow(
						type_variable(INTERNAL_LOC()),
						type_variable(INTERNAL_LOC()))->generalize(env)->normalize(),
					false /*allow_subscoping*/);
		}
	}
}

#define INSTANCE_ID_SEP "/"
identifier_t make_instance_id(std::string type_class_name, instance_t *instance) {
	return identifier_t{type_class_name + INSTANCE_ID_SEP + instance->type->repr(), instance->get_location()};
}

identifier_t make_instance_decl_id(std::string type_class_name, instance_t *instance, identifier_t decl_id) {
	return identifier_t{make_instance_id(type_class_name, instance).name + INSTANCE_ID_SEP + decl_id.name, decl_id.location};
}

identifier_t make_instance_dict_id(std::string type_class_name, instance_t *instance) {
	auto id = make_instance_id(type_class_name, instance);
	return identifier_t{"dict" INSTANCE_ID_SEP + id.name, id.location};
}

std::vector<decl_t *> check_instances(
		const std::vector<instance_t *> &instances,
		const std::map<std::string, type_class_t *> &type_class_map,
		env_t &env)
{
	std::vector<decl_t *> instance_decls;

	for (instance_t *instance : instances) {
		try {
			auto iter = type_class_map.find(instance->type_class_id.name);
			if (iter == type_class_map.end()) {
				auto error = user_error(instance->type_class_id.location,
						"could not find type class for instance %s %s",
						instance->type_class_id.str().c_str(),
						instance->type->str().c_str());
				auto leaf_name = split(instance->type_class_id.name, ".").back();
				for (auto type_class_pair : type_class_map) {
					auto type_class = type_class_pair.second;
					if (type_class->id.name.find(leaf_name) != std::string::npos) {
						error.add_info(type_class->id.location, "did you mean %s?",
								type_class->id.str().c_str());
					}
				}
				throw error;
			}

			/* first put an instance requirement on any superclasses of the associated
			 * type_class */
			type_class_t *type_class = iter->second;

			std::set<std::string> names_checked;

			/* make a template for the type that the instance implementation should
			 * conform to */
			types::type_t::map subst;
			subst[type_class->type_var_id.name] = instance->type->generalize({})->instantiate(INTERNAL_LOC());

			// check whether this instance properly implements the given type class
			for (auto pair : type_class->overloads) {
				auto name = pair.first;
				auto type = pair.second;
				bool found = false;
				for (auto decl : instance->decls) {
					assert(name.find(".") != std::string::npos);
					assert(decl->var.name.find(".") != std::string::npos);
					if (decl->var.name == name) {
						found = true;
						if (in(name, names_checked)) {
							throw user_error(
									decl->get_location(),
									"name %s already duplicated in this instance",
									decl->var.str().c_str());
						}
						names_checked.insert(decl->var.name);

						env_t local_env{env};
						local_env.instance_requirements.resize(0);
						auto instance_decl_id = make_instance_decl_id(type_class->id.name, instance, decl->var);
						auto expected_scheme = type->rebind(subst)->generalize(local_env);
						auto type_instance = expected_scheme->instantiate(INTERNAL_LOC());

						auto instance_decl_expr = new as_t(decl->value, type_instance, false /*force_cast*/);
						check(
								instance_decl_id,
								instance_decl_expr,
								local_env);
						assert(local_env.instance_requirements.size() == 0);
						env.map[instance_decl_id.name] = expected_scheme;

						instance_decls.push_back(new decl_t(instance_decl_id, instance_decl_expr));
					}
				}
				if (!found) {
					throw user_error(pair.second->get_location(), "could not find decl for %s in instance %s %s",
							name.c_str(), type_class->id.str().c_str(), instance->type->str().c_str());
				}
			}

			/* check for unrelated declarations inside of an instance */
			for (auto decl : instance->decls) {
				if (!in(decl->var.name, names_checked)) {
					throw user_error(decl->var.location,
						   	"extraneous declaration %s found in instance %s %s (names_checked = {%s})",
							decl->var.str().c_str(),
							type_class->id.str().c_str(),
							instance->type->str().c_str(),
							join(names_checked, ", ").c_str());
				}
			}
		} catch (user_error &e) {
			log_location(
					instance->type_class_id.location,
					"checking that member functions of instance %s %s type check",
					instance->type_class_id.str().c_str(),
					instance->type->str().c_str());
			print_exception(e);
		}
	}
		return instance_decls;
}

void generate_instance_dictionaries(
		const std::vector<instance_t *> &instances,
		const std::map<std::string, type_class_t *> &type_class_map,
		std::map<std::string, decl_t *> &decl_map,
		env_t &env)
{
	for (auto instance : instances) {
		auto type_class_iter = type_class_map.find(instance->type_class_id.name);
		assert(type_class_iter != type_class_map.end());
		auto type_class = type_class_iter->second;
		auto instance_dict_name = make_instance_dict_id(type_class->id.name, instance).name;
		debug_above(7, log("trying to make a dictionary called %s", instance_dict_name.c_str()));

		std::vector<expr_t *> dims;
		for (auto superclass : type_class->superclasses) {
			identifier_t instance_decl_id = make_instance_dict_id(superclass, instance);
			dims.push_back(new var_t(instance_decl_id));
		}

		for (auto decl : instance->decls) {
			identifier_t instance_decl_id = make_instance_decl_id(type_class->id.name, instance, decl->var);
			dims.push_back(new var_t(instance_decl_id));
		}
		assert(!in(instance_dict_name, decl_map));
		decl_map[instance_dict_name] = new decl_t(identifier_t{instance_dict_name, instance->get_location()}, new tuple_t(instance->get_location(), dims));
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
		if (cmd == "test") {
			assert(alphabetize(0) == "a");
			assert(alphabetize(1) == "b");
			assert(alphabetize(2) == "c");
			assert(alphabetize(26) == "aa");
			assert(alphabetize(27) == "ab");
			return EXIT_SUCCESS;
		}
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
		} else if (cmd == "compile") {
			auto compilation = compiler::parse_program(user_program_name);
			if (compilation != nullptr) {
				program_t *program = compilation->program;

				env_t env;
				initialize_default_env(env);

				auto type_class_map = check_type_classes(program->type_classes, env);

				check_decls(program->decls, env);

				auto instance_decls = check_instances(program->instances, type_class_map, env);

				std::map<std::string, decl_t *> decl_map;

				for (auto decl : program->decls) {
					assert(!in(decl->var.name, decl_map));
					decl_map[decl->var.name] = decl;
				}

				/* the instance decls were already checked, but let's add them to the list of decls
				 * for the lowering step */
				for (auto decl : instance_decls) {
					assert(!in(decl->var.name, decl_map));
					decl_map[decl->var.name] = decl;
				}

				if (debug_compiled_env) {
					for (auto pair : env.map) {
						// std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->str() << C_RESET << std::endl;
						std::cout << pair.first << c_good(" :: ") << C_TYPE << pair.second->normalize()->str() << C_RESET << std::endl;
					}
				}

				generate_instance_dictionaries(program->instances, type_class_map, decl_map, env);

				if (debug_compiled_env) {
					for (auto pair : decl_map) {
						std::cout << pair.second->str() << std::endl;
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

