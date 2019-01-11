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
#include "translate.h"

using namespace bitter;

const bool debug_compiled_env = getenv("SHOW_ENV") != nullptr;
const bool debug_types = getenv("SHOW_TYPES") != nullptr;
const bool debug_all_expr_types = getenv("SHOW_EXPR_TYPES") != nullptr;
const int max_tuple_size = (getenv("ZION_MAX_TUPLE") != nullptr) ? atoi(getenv("ZION_MAX_TUPLE")) : 16;

types::type_t::ref program_main_type = type_arrows({type_unit(INTERNAL_LOC()), type_id(make_iid("std.ExitCode"))});
types::scheme_t::ref program_main_scheme = scheme({}, {}, program_main_type);

int usage() {
	log(log_error, "available commands: test, compile");
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
	auto scheme = ty->generalize(env.get_predicate_map())->normalize();
	// log("NORMALIED ty = %s", n->str().c_str());

	// log_location(id.location, "adding %s to env as %s", id.str().c_str(), scheme->str().c_str());
	// log_location(id.location, "let %s = %s", id.str().c_str(), expr->str().c_str());
	env.extend(id, scheme, false /*allow_subscoping*/);

	if (debug_types) {
		log_location(id.location, "info: %s :: %s", id.str().c_str(), scheme->str().c_str());
	}
}
		
std::vector<std::string> alphabet(int count) {
	std::vector<std::string> xs;
	for (int i=0; i<count; ++i) {
		xs.push_back(alphabetize(i));
	}
	return xs;
}

void initialize_default_env(env_t &env) {
	auto Int = type_id(make_iid(INT_TYPE));
	auto Float = type_id(make_iid(FLOAT_TYPE));
	auto Bool = type_id(make_iid(BOOL_TYPE));
	auto Char = type_id(make_iid(CHAR_TYPE));
	auto String = type_operator(type_id(make_iid(VECTOR_TYPE)), Char);
	auto tv_a = type_variable(make_iid("a"));
	auto tp_a = type_ptr(tv_a);

	env.map["__builtin_multiply_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
	env.map["__builtin_divide_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
	env.map["__builtin_subtract_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
	env.map["__builtin_add_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
	env.map["__builtin_negate_int"] = scheme({}, {}, type_arrows({Int, Int}));
	env.map["__builtin_abs_int"] = scheme({}, {}, type_arrows({Int, Int}));
	env.map["__builtin_multiply_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
	env.map["__builtin_divide_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
	env.map["__builtin_subtract_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
	env.map["__builtin_add_float"] = scheme({}, {}, type_arrows({Float, Float, Float}));
	env.map["__builtin_abs_float"] = scheme({}, {}, type_arrows({Float, Float}));
	env.map["__builtin_int_to_float"] = scheme({}, {}, type_arrows({Int, Float}));
	env.map["__builtin_negate_float"] = scheme({}, {}, type_arrows({Float, Float}));
	env.map["__builtin_add_ptr"] = scheme({"a"}, {}, type_arrows({tp_a, Int, tp_a}));
	env.map["__builtin_ptr_eq"] = scheme({"a"}, {}, type_arrows({tp_a, tp_a, Bool}));
	env.map["__builtin_ptr_ne"] = scheme({"a"}, {}, type_arrows({tp_a, tp_a, Bool}));
	env.map["__builtin_ptr_load"] = scheme({"a"}, {}, type_arrows({tp_a, tv_a}));
	env.map["__builtin_int_eq"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
	env.map["__builtin_int_ne"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
	env.map["__builtin_int_lt"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
	env.map["__builtin_int_lte"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
	env.map["__builtin_int_gt"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
	env.map["__builtin_int_gte"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
	env.map["__builtin_float_eq"] = scheme({}, {}, type_arrows({Float, Float, Bool}));
	env.map["__builtin_float_ne"] = scheme({}, {}, type_arrows({Float, Float, Bool}));
	env.map["__builtin_float_lt"] = scheme({}, {}, type_arrows({Float, Float, Bool}));
	env.map["__builtin_float_lte"] = scheme({}, {}, type_arrows({Float, Float, Bool}));
	env.map["__builtin_float_gt"] = scheme({}, {}, type_arrows({Float, Float, Bool}));
	env.map["__builtin_float_gte"] = scheme({}, {}, type_arrows({Float, Float, Bool}));
	env.map["__builtin_print"] = scheme({}, {}, type_arrows({String, type_unit(INTERNAL_LOC())}));
	env.map["__builtin_exit"] = scheme({}, {}, type_arrows({Int, type_bottom()}));
	env.map["__builtin_calloc"] = scheme({"a"}, {}, type_arrows({Int, tp_a}));
	env.map["__builtin_store"] = scheme({"a"}, {}, type_arrows({
				type_operator(type_id(make_iid("std.Ref")), tv_a),
				tv_a,
				type_unit(INTERNAL_LOC())}));
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

void check_decls(std::string entry_point_name, const std::vector<decl_t *> &decls, env_t &env) {
	for (decl_t *decl : decls) {
		try {
			if (decl->var.name == entry_point_name) {
				/* make sure that the "main" function has the correct signature */
				check(
						decl->var,
					   	new as_t(
							decl->value,
							program_main_scheme,
							false /*force_cast*/),
						env);
			} else {
				check(decl->var, decl->value, env);
			}
		} catch (user_error &e) {
			print_exception(e);

			/* keep trying other decls, and pretend like this function gives back
			 * whatever the user wants... */
			env.extend(
					decl->var,
					type_arrow(
						type_variable(INTERNAL_LOC()),
						type_variable(INTERNAL_LOC()))->generalize(env.get_predicate_map())->normalize(),
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

void check_instance_for_type_class_overload(
		std::string name,
	   	types::type_t::ref type,
	   	instance_t *instance,
		type_class_t *type_class,
		const types::type_t::map &subst,
		std::set<std::string> &names_checked,
		std::vector<decl_t *> &instance_decls,
		std::map<defn_id_t, decl_t *> &overrides_map,
	   	env_t &env)
{
	bool found = false;
	for (auto decl : instance->decls) {
		assert(name.find(".") != std::string::npos);
		// assert(decl->var.name.find(".") != std::string::npos);
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
			auto expected_scheme = type->rebind(subst)->generalize(local_env.get_predicate_map());

			auto instance_decl_expr = new as_t(decl->value, expected_scheme, false /*force_cast*/);
			check(
					instance_decl_id,
					instance_decl_expr,
					local_env);
			debug_above(5, log("checking the instance fn %s gave scheme %s. we expected %s.",
						instance_decl_id.str().c_str(),
						local_env.map[instance_decl_id.name]->normalize()->str().c_str(),
						expected_scheme->normalize()->str().c_str()));

			if (!scheme_equality(
						local_env.map[instance_decl_id.name],
						expected_scheme->normalize()))
			{
				auto error = user_error(instance_decl_id.location, "instance component %s appears to be more constrained than the type class",
						decl->var.str().c_str());
				error.add_info(instance_decl_id.location, "instance component declaration has scheme %s",
						local_env.map[instance_decl_id.name]->normalize()->str().c_str());
				error.add_info(type->get_location(), "type class component declaration has scheme %s",
						expected_scheme->normalize()->str().c_str());
				throw error;
			}

			/* keep track of any instance requirements that were referenced inside the
			 * implementation of the type class instance components */
			if (local_env.instance_requirements.size() != 0) {
				for (auto ir : local_env.instance_requirements) {
					env.instance_requirements.push_back(ir);
				}
			}

			env.map[instance_decl_id.name] = expected_scheme;

			instance_decls.push_back(new decl_t(instance_decl_id, instance_decl_expr));
			auto defn_id = defn_id_t{decl->var, expected_scheme};
			overrides_map[defn_id] = instance_decls.back();
		}
	}
	if (!found) {
		throw user_error(type->get_location(), "could not find decl for %s in instance %s %s",
				name.c_str(), type_class->id.str().c_str(), instance->type->str().c_str());
	}
}

void check_instance_for_type_class_overloads(
	   	instance_t *instance,
		type_class_t *type_class,
		std::vector<decl_t *> &instance_decls,
		std::map<defn_id_t, decl_t *> &overrides_map,
	   	env_t &env)
{
	/* make a template for the type that the instance implementation should
	 * conform to */
	types::type_t::map subst;
	subst[type_class->type_var_id.name] = instance->type->generalize({})->instantiate(INTERNAL_LOC());

	/* check whether this instance properly implements the given type class */
	std::set<std::string> names_checked;

	for (auto pair : type_class->overloads) {
		auto name = pair.first;
		auto type = pair.second;
		check_instance_for_type_class_overload(
				name,
			   	type,
			   	instance,
			   	type_class,
			   	subst,
				names_checked,
				instance_decls,
				overrides_map,
			   	env/*, type_class->defaults*/);
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
}

std::vector<decl_t *> check_instances(
		const std::vector<instance_t *> &instances,
		const std::map<std::string, type_class_t *> &type_class_map,
		std::map<defn_id_t, decl_t *> &overrides_map,
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

			check_instance_for_type_class_overloads(
					instance,
					type_class,
					instance_decls,
					overrides_map,
					env);

		} catch (user_error &e) {
			e.add_info(
					instance->type_class_id.location,
					"while checking instance %s %s",
					instance->type_class_id.str().c_str(),
					instance->type->str().c_str());
			print_exception(e);
		}
	}
		return instance_decls;
}

bool instance_matches_requirement(instance_t *instance, const instance_requirement_t &ir, env_t &env) {
	// log("checking %s %s vs. %s %s", ir.type_class_name.c_str(), ir.type->str().c_str(), instance->type_class_id.name.c_str(), instance->type->str().c_str());
	auto pm = env.get_predicate_map();
	return instance->type_class_id.name == ir.type_class_name && scheme_equality(
			ir.type->generalize(pm)->normalize(),
			instance->type->generalize(pm)->normalize());
}

void check_instance_requirements(const std::vector<instance_t *> &instances, env_t &env) {
	for (auto ir : env.instance_requirements) {
		debug_above(8, log("checking instance requirement %s", ir.str().c_str()));
		std::vector<instance_t *> matching_instances;
		for (auto instance : instances) {
			if (instance_matches_requirement(instance, ir, env)) {
				matching_instances.push_back(instance);
			}
		}

		if (matching_instances.size() == 0) {
			throw user_error(ir.location, "could not find an instance that supports the requirement %s",
					ir.str().c_str());
		} else if (matching_instances.size() != 1) {
			auto error = user_error(ir.location, "found multiple instances implementing %s", ir.str().c_str());
			for (auto mi : matching_instances) {
				error.add_info(mi->get_location(), "matching instance found is %s %s",
						mi->type_class_id.str().c_str(),
						mi->type->str().c_str());
			}
			throw error;
		}
	}
}

class defn_map_t {
	std::map<defn_id_t, decl_t *> map;
	std::map<std::string, decl_t *> decl_map;

public:
	decl_t *maybe_lookup(defn_id_t defn_id) const {
		auto iter = map.find(defn_id);
		if (iter == map.end()) {
			decl_t *decl = nullptr;
			for (auto pair : map) {
				if (pair.first.id.name == defn_id.id.name) {
					if (scheme_equality(pair.first.scheme, defn_id.scheme)) {
						if (decl != nullptr) {
							throw user_error(defn_id.id.location, "found ambiguous instance method");
						}
						decl = pair.second;
					}
				}
			}
			if (decl != nullptr) {
				return decl;
			}

			auto iter_decl = decl_map.find(defn_id.id.name);
			if (iter_decl != decl_map.end()) {
				return iter_decl->second;
			}
			return nullptr;
		} else {
			return iter->second;
		}
	}

	decl_t *lookup(defn_id_t defn_id) const {
		auto iter = map.find(defn_id);
		if (iter == map.end()) {
			auto iter_decl = decl_map.find(defn_id.id.name);
			if (iter_decl != decl_map.end()) {
				return iter_decl->second;
			}
			auto error = user_error(defn_id.id.location, "symbol %s does not seem to exist", defn_id.id.str().c_str());
			std::stringstream ss;

			for (auto pair: decl_map) {
				ss << pair.first << " " << pair.second << std::endl;
			}
			error.add_info(defn_id.id.location, "%s", ss.str().c_str());
			throw error;
		} else {
			return iter->second;
		}
	}

	void populate(
			const std::map<std::string, decl_t *> &decl_map_,
			const std::map<defn_id_t, decl_t *> &overrides_map,
			const env_t &env)
	{
		decl_map = decl_map_;

		/* populate the definition map which is the main result of the first phase of compilation */
		for (auto pair : decl_map) {
			assert(pair.first == pair.second->var.name);
			auto scheme = env.lookup_env(pair.second->var)->generalize({})->normalize();
			auto defn_id = defn_id_t{pair.second->var, scheme};
			assert(!in(defn_id, map));
			debug_above(8, log("populating defn_map with %s", defn_id.str().c_str()));
			map[defn_id] = pair.second;
		}

		for (auto pair : overrides_map) {
			debug_above(8, log("populating defn_map with override %s", pair.first.str().c_str()));
			assert(!in(pair.first, map));
			map[pair.first] = pair.second;
		}
	}
};

struct phase_2_t {
	std::shared_ptr<compilation_t const> const compilation;
	types::scheme_t::map const typing;
	defn_map_t const defn_map;
	data_ctors_map_t const data_ctors_map;
};

phase_2_t compile(std::string user_program_name_) {
	auto compilation = compiler::parse_program(user_program_name_);
	if (compilation == nullptr) {
		exit(EXIT_FAILURE);
	}

	program_t *program = compilation->program;

	env_t env{
		{} /*map*/,
		nullptr /*return_type*/,
		{} /*instance_requirements*/,
		std::make_shared<std::unordered_map<bitter::expr_t *, types::type_t::ref>>(),
		compilation->data_ctors_map};

	initialize_default_env(env);

	auto type_class_map = check_type_classes(program->type_classes, env);

	check_decls(compilation->program_name + ".main", program->decls, env);
	std::map<defn_id_t, decl_t *> overrides_map;

	auto instance_decls = check_instances(program->instances, type_class_map, overrides_map, env);

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
			log("%s" c_good(" :: ") c_type("%s"),
					pair.first.c_str(),
					pair.second->normalize()->str().c_str());
		}
	}

	for (auto pair : decl_map) {
		assert(pair.first == pair.second->var.name);

		auto type = env.lookup_env(make_iid(pair.first));
		if (debug_compiled_env) {
			log("%s " c_good("::") " %s",
					pair.second->str().c_str(),
					env.map[pair.first]->str().c_str());
		}
	}

	try {
		check_instance_requirements(program->instances, env);
	} catch (user_error &e) {
		print_exception(e);
	}

	if (debug_all_expr_types) {
		log(c_good("All Expression Types"));
		for (auto pair : *env.tracked_types) {
			log_location(
					pair.first->get_location(),
				   	"%s :: %s", pair.first->str().c_str(),
				   	pair.second->generalize({})->str().c_str());
		}
	}

	defn_map_t defn_map;
	defn_map.populate(decl_map, overrides_map, env);
	return {compilation, env.map, defn_map, compilation->data_ctors_map};
}

void specialize(
		defn_map_t const &defn_map,
		types::scheme_t::map const &typing,
		data_ctors_map_t const &data_ctors_map,
		defn_id_t defn_id,
		/* output */ std::map<defn_id_t, translation_t::ref> &translation_map,
		/* output */ std::set<defn_id_t> &needed_defns)
{
	if (starts_with(defn_id.id.name, "__builtin_")) {
		return;
	}
	/* expected type schemes for specializations can have unresolved type variables. That indicates
	 * that an input to the function is irrelevant to the output. However, since Zion is eagerly
	 * evaluated and permits impure side effects, it may not be irrelevant to the overall program's
	 * semantics, so terms with ambiguous types cannot be thrown out altogether. They cannot have
	 * unresolved bounded type variables, because that would imply that we don't know which type
	 * class instance to choose within the inner specialization. */
	assert(defn_id.scheme->btvs() == 0);

	auto iter = translation_map.find(defn_id);
	if (iter != translation_map.end()) {
		if (iter->second == nullptr) {
			throw user_error(defn_id.get_location(), "recursion is not yet impl - and besides, it should be handled earlier in the compiler");
		}
		log("we have already specialized %s. it is %s", defn_id.str().c_str(), iter->second->str().c_str());
		return;
	}

	/* ... like a GRAY mark in the visited set... */
	translation_map[defn_id] = nullptr;
	try {

		debug_above(7, log(c_good("Specializing subprogram %s"), defn_id.str().c_str()));

		/* cross-check all our data sources */
#if 0
		auto existing_type = env.maybe_get_tracked_type(translation_map[defn_id]);
		auto existing_scheme = existing_type->generalize(env)->normalize();
		/* the env should be internally self-consistent */
		assert(scheme_equality(get(env.map, defn_id.id.name, {}), existing_scheme));
		/* the existing scheme for the unspecialized version should not match the one we are seeking
		 * because above we looked in our map for this specialization. */
		assert(!scheme_equality(existing_scheme, defn_id.specialization));
		assert(!scheme_equality(get(env.map, defn_id.id.name, {}), defn_id.specialization));
#endif

		/* start the process of specializing our decl */
		env_t env{typing /*map*/, nullptr /*return_type*/, {} /*instance_requirements*/, {} /*tracked_types*/, data_ctors_map};
		auto tracked_types = std::make_shared<std::unordered_map<bitter::expr_t *, types::type_t::ref>>();
		env.tracked_types = tracked_types;

		decl_t *decl_to_check = defn_map.maybe_lookup(defn_id);
		if (decl_to_check == nullptr) {
			throw user_error(defn_id.id.location, "could not find a definition for %s :: %s",
					defn_id.id.str().c_str(), defn_id.scheme->str().c_str());
		}

		expr_t *to_check = decl_to_check->value;
		auto as_defn = new as_t(to_check, defn_id.scheme, false);
		check(
				identifier_t{defn_id.str(), defn_id.id.location},
				as_defn,
				env);

		if (debug_compiled_env) {
			for (auto pair : *tracked_types) {
				log_location(
						pair.first->get_location(),
						"%s :: %s", pair.first->str().c_str(),
						pair.second->str().c_str());
			}
		}

		translation_env_t tenv{tracked_types, data_ctors_map};
		std::unordered_set<std::string> bound_vars;
		INDENT(6, string_format("----------- specialize %s ------------", defn_id.str().c_str()));
		auto translated_decl = translate(
				defn_id,
				as_defn,
				bound_vars,
				tenv,
				needed_defns);
		log_location(
				defn_id.id.location,
				"%s = %s",
				defn_id.id.str().c_str(),
				translated_decl->str().c_str());
		translation_map[defn_id] = translated_decl;
	} catch (...) {
		assert(translation_map[defn_id] == nullptr);
		translation_map.erase(defn_id);
		throw;
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
			log("%s", compiler::resolve_module_filename(INTERNAL_LOC(), user_program_name, ".zion").c_str());
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
			auto phase_2 = compile(user_program_name);

			if (user_error::errors_occurred()) {
				return EXIT_FAILURE;
			}
			decl_t *program_main = phase_2.defn_map.lookup({make_iid(phase_2.compilation->program_name + ".main"), program_main_scheme});

			std::set<defn_id_t> needed_defns;
			defn_id_t main_defn{program_main->var, program_main_scheme};
			needed_defns.insert(main_defn);

			std::map<defn_id_t, translation_t::ref> translation_map;
			while (needed_defns.size() != 0) {
				auto next_defn_id = *needed_defns.begin();
				try {
					specialize(
							phase_2.defn_map,
							phase_2.typing,
							phase_2.data_ctors_map,
							next_defn_id,
							translation_map,
							needed_defns);
				} catch (user_error &e) {
					print_exception(e);
					/* and continue */
				}
				needed_defns.erase(next_defn_id);
			}

			if (debug_compiled_env) {
				for (auto pair : translation_map) {
					log_location(pair.second->get_location(), "%s = %s",
							pair.first.str().c_str(),
							pair.second->str().c_str());
				}
			}
			return user_error::errors_occurred() ? EXIT_FAILURE : EXIT_SUCCESS;
		} else {
			panic(string_format("bad CLI invocation of %s", argv[0]));
		}
	} else {
		return usage();
	}

	return EXIT_SUCCESS;
}
