#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>

#include "ast.h"
#include "compiler.h"
#include "disk.h"
#include "env.h"
#include "gen.h"
#include "lexer.h"
#include "logger.h"
#include "logger_decls.h"
#include "tests.h"
#include "translate.h"
#include "unification.h"

#define IMPL_SUFFIX "-impl"

using namespace bitter;

bool get_help = false;
bool fast_fail = true && (getenv("ZION_SHOW_ALL_ERRORS") == nullptr);
bool debug_compiled_env = getenv("SHOW_ENV") != nullptr;
bool debug_specialized_env = getenv("SHOW_ENV2") != nullptr;
bool debug_types = getenv("SHOW_TYPES") != nullptr;
bool debug_all_expr_types = getenv("SHOW_EXPR_TYPES") != nullptr;
bool debug_all_translated_defns = getenv("SHOW_DEFN_TYPES") != nullptr;
int max_tuple_size = (getenv("ZION_MAX_TUPLE") != nullptr)
                         ? atoi(getenv("ZION_MAX_TUPLE"))
                         : 16;

types::type_t::ref program_main_type = type_arrows(
    {type_unit(INTERNAL_LOC()), type_unit(INTERNAL_LOC())});
types::scheme_t::ref program_main_scheme = scheme({}, {}, program_main_type);

int run_program(std::string executable, std::vector<const char *> args) {
  pid_t pid = fork();

  if (pid == -1) {
    perror(
        string_format("unable to fork() child process %s", executable.c_str())
            .c_str());
  } else if (pid > 0) {
    /* parent */
    int status;

    // printf("Child has pid %ld\n", (long)pid);

    int ret = ::wait(&status);

    if (ret == -1) {
      perror("wait()");
    } else {
      if (WIFEXITED(status)) {
        /* did the child terminate normally? */
        // printf("%ld exited with return code %d\n", (long)pid,
        // WEXITSTATUS(status));
        return WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        /* was the child terminated by a signal? */
        // printf("%ld terminated because it didn't catch signal number %d\n",
        // (long)pid, WTERMSIG(status));
        return -1;
      }
    }
  } else {
    /* child */
    printf("running %s...\n", executable.c_str());
    int ret = execvp(("./" + executable).c_str(), const_cast<char **>(&args[0]));
    if (ret == -1) {
      printf("failed to launch %s %s. quitting...\n", executable.c_str(),
             join(args, " ").c_str());
      return EXIT_FAILURE;
    }
  }

  return 0;
}

void handle_sigint(int sig) {
  print_stacktrace(stderr, 100);
  exit(2);
}

void check(bool check_constraint_coverage,
           identifier_t id,
           expr_t *expr,
           env_t &env) {
  constraints_t constraints;
  // std::cout << C_ID "------------------------------" C_RESET << std::endl;
  debug_above(4, log("type checking %s = %s", id.str().c_str(), expr->str().c_str()));
  types::type_t::ref ty = infer(expr, env, constraints);
  types::type_t::map bindings = solver(
      check_constraint_coverage,
      make_context(id.location, "solving %s :: %s", id.name.c_str(),
                   ty->str().c_str()),
      constraints, env);

  // log("GOT ty = %s", ty->str().c_str());
  ty = ty->rebind(bindings);
  auto scheme = ty->generalize(env.get_predicate_map())->normalize();

  debug_above(3, log_location(id.location, "adding %s to env as %s",
                              id.str().c_str(), scheme->str().c_str()));
  // log_location(id.location, "let %s = %s", id.str().c_str(),
  // expr->str().c_str());
  env.extend(id, scheme, false /*allow_subscoping*/);
}

std::vector<std::string> alphabet(int count) {
  std::vector<std::string> xs;
  for (int i = 0; i < count; ++i) {
    xs.push_back(alphabetize(i));
  }
  return xs;
}

const types::scheme_t::map &get_builtins() {
  static std::unique_ptr<types::scheme_t::map> map;
  if (map == nullptr) {
    auto Unit = type_unit(INTERNAL_LOC());
    auto Int = type_id(make_iid(INT_TYPE));
    auto Float = type_id(make_iid(FLOAT_TYPE));
    auto Bool = type_id(make_iid(BOOL_TYPE));
    auto Char = type_id(make_iid(CHAR_TYPE));
    auto String = type_id(make_iid(STRING_TYPE));
    auto PtrToChar = type_operator(type_id(make_iid(PTR_TYPE_OPERATOR)), Char);
    auto tv_a = type_variable(make_iid("a"));
    auto tp_a = type_ptr(tv_a);
    auto tv_b = type_variable(make_iid("b"));
    auto tp_b = type_ptr(tv_b);

    map = std::make_unique<types::scheme_t::map>();

    // TODO: unify this map with the implementation of these in gen.cpp
    (*map)["__builtin_hello"] = scheme({}, {}, Unit);
    (*map)["__builtin_goodbye"] = scheme({}, {}, Unit);
    (*map)["__builtin_word_size"] = scheme({}, {}, Int);
    (*map)["__builtin_min_int"] = scheme({}, {}, Int);
    (*map)["__builtin_max_int"] = scheme({}, {}, Int);
    (*map)["__builtin_multiply_int"] = scheme({}, {},
                                              type_arrows({Int, Int, Int}));
    (*map)["__builtin_divide_int"] = scheme({}, {},
                                            type_arrows({Int, Int, Int}));
    (*map)["__builtin_subtract_int"] = scheme({}, {},
                                              type_arrows({Int, Int, Int}));
    (*map)["__builtin_add_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
    (*map)["__builtin_negate_int"] = scheme({}, {}, type_arrows({Int, Int}));
    (*map)["__builtin_abs_int"] = scheme({}, {}, type_arrows({Int, Int}));
    (*map)["__builtin_multiply_char"] = scheme({}, {},
                                               type_arrows({Char, Char, Char}));
    (*map)["__builtin_divide_char"] = scheme({}, {},
                                             type_arrows({Char, Char, Char}));
    (*map)["__builtin_subtract_char"] = scheme({}, {},
                                               type_arrows({Char, Char, Char}));
    (*map)["__builtin_add_char"] = scheme({}, {},
                                          type_arrows({Char, Char, Char}));
    (*map)["__builtin_negate_char"] = scheme({}, {}, type_arrows({Char, Char}));
    (*map)["__builtin_abs_char"] = scheme({}, {}, type_arrows({Char, Char}));
    (*map)["__builtin_multiply_float"] = scheme(
        {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_divide_float"] = scheme(
        {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_subtract_float"] = scheme(
        {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_add_float"] = scheme({}, {},
                                           type_arrows({Float, Float, Float}));
    (*map)["__builtin_abs_float"] = scheme({}, {}, type_arrows({Float, Float}));
    (*map)["__builtin_int_to_float"] = scheme({}, {},
                                              type_arrows({Int, Float}));
    (*map)["__builtin_negate_float"] = scheme({}, {},
                                              type_arrows({Float, Float}));
    (*map)["__builtin_ptr_add"] = scheme({"a"}, {},
                                         type_arrows({tp_a, Int, tp_a}));
    (*map)["__builtin_ptr_eq"] = scheme({"a"}, {},
                                        type_arrows({tp_a, tp_a, Bool}));
    (*map)["__builtin_ptr_ne"] = scheme({"a"}, {},
                                        type_arrows({tp_a, tp_a, Bool}));
    (*map)["__builtin_ptr_load"] = scheme({"a"}, {}, type_arrows({tp_a, tv_a}));
    (*map)["__builtin_get_dim"] = scheme({"a", "b"}, {},
                                         type_arrows({tv_a, Int, tv_b}));
    (*map)["__builtin_cmp_ctor_id"] = scheme({"a"}, {},
                                             type_arrows({tv_a, Int, Bool}));
    (*map)["__builtin_int_to_char"] = scheme({}, {}, type_arrows({Int, Char}));
    (*map)["__builtin_int_eq"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_ne"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_lt"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_lte"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_gt"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_gte"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_char_eq"] = scheme({}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_ne"] = scheme({}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_lt"] = scheme({}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_lte"] = scheme({}, {},
                                          type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_gt"] = scheme({}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_gte"] = scheme({}, {},
                                          type_arrows({Char, Char, Bool}));
    (*map)["__builtin_float_eq"] = scheme({}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_ne"] = scheme({}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_lt"] = scheme({}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_lte"] = scheme({}, {},
                                           type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_gt"] = scheme({}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_gte"] = scheme({}, {},
                                           type_arrows({Float, Float, Bool}));
    (*map)["__builtin_memcpy"] = scheme(
        {}, {},
        type_arrows({PtrToChar, PtrToChar, Int, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_memcmp"] = scheme(
        {}, {}, type_arrows({PtrToChar, PtrToChar, Int, Int}));
    (*map)["__builtin_write"] = scheme(
        {}, {}, type_arrows({Int, PtrToChar, Int, Unit}));
    (*map)["__builtin_write_char"] = scheme({}, {},
                                            type_arrows({Int, Char, Unit}));
    (*map)["__builtin_pass_test"] = scheme({}, {}, Unit);
    (*map)["__builtin_print"] = scheme(
        {}, {}, type_arrows({PtrToChar, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_print_int"] = scheme(
        {}, {}, type_arrows({Int, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_itoa"] = scheme({}, {}, type_arrows({Int, PtrToChar}));
    (*map)["__builtin_strlen"] = scheme({}, {}, type_arrows({PtrToChar, Int}));
    (*map)["__builtin_exit"] = scheme(
        {}, {}, type_arrows({Int, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_calloc"] = scheme({"a"}, {}, type_arrows({Int, tp_a}));
    (*map)["__builtin_store_ref"] = scheme(
        {"a"}, {},
        type_arrows({type_operator(type_id(make_iid(REF_TYPE_OPERATOR)), tv_a),
                     tv_a, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_store_ptr"] = scheme(
        {"a"}, {},
        type_arrows({type_operator(type_id(make_iid(PTR_TYPE_OPERATOR)), tv_a),
                     tv_a, type_unit(INTERNAL_LOC())}));
    for (auto pair : *map) {
      if (starts_with(pair.first, "__builtin,")) {
        assert(pair.second->instantiate(INTERNAL_LOC())->ftv_count() == 0);
      }
    }
  }
  return *map;
}

void initialize_default_env(env_t &env) {
  for (auto pair : get_builtins()) {
    env.map[pair.first] = pair.second;
  }
}

std::map<std::string, type_class_t *> check_type_classes(
    const std::vector<type_class_t *> &type_classes,
    env_t &env) {
  std::map<std::string, type_class_t *> type_class_map;

  /* introduce all the type class signatures into the env, and build up an
   * index of type_class names */
  for (type_class_t *type_class : type_classes) {
    try {
      if (in(type_class->id.name, type_class_map)) {
        auto error = user_error(type_class->id.location,
                                "type class name %s is already taken",
                                type_class->id.str().c_str());
        error.add_info(type_class_map[type_class->id.name]->get_location(),
                       "see earlier type class declaration here");
        throw error;
      } else {
        type_class_map[type_class->id.name] = type_class;
      }

      auto predicates = type_class->superclasses;
      predicates.insert(type_class->id.name);

      types::type_t::map bindings;
      bindings[type_class->type_var_id.name] = type_variable(
          gensym(type_class->type_var_id.location), predicates);

      for (auto pair : type_class->overloads) {
        if (in(pair.first, env.map)) {
          auto error = user_error(pair.second->get_location(),
                                  "the name " c_id("%s") " is already in use",
                                  pair.first.c_str());
          error.add_info(env.map[pair.first]->get_location(),
                         "see first declaration here");
          throw error;
        }

        env.extend(identifier_t{pair.first, pair.second->get_location()},
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

void check_decls(std::string entry_point_name,
                 const std::vector<decl_t *> &decls,
                 env_t &env) {
  for (decl_t *decl : decls) {
    try {
      if (decl->var.name == entry_point_name) {
        /* make sure that the "main" function has the correct signature */
        check(false /*check_constraint_coverage*/, decl->var,
              new as_t(decl->value, program_main_scheme, false /*force_cast*/),
              env);
      } else {
        check(false /*check_constraint_coverage*/, decl->var, decl->value, env);
      }
    } catch (user_error &e) {
      print_exception(e);

      /* keep trying other decls, and pretend like this function gives back
       * whatever the user wants... */
      env.extend(decl->var,
                 type_arrow(type_variable(INTERNAL_LOC()),
                            type_variable(INTERNAL_LOC()))
                     ->generalize(env.get_predicate_map())
                     ->normalize(),
                 false /*allow_subscoping*/);
    }
  }
}

#define INSTANCE_ID_SEP "/"
identifier_t make_instance_id(std::string type_class_name,
                              instance_t *instance) {
  return identifier_t{type_class_name + INSTANCE_ID_SEP +
                          instance->type->repr(),
                      instance->get_location()};
}

identifier_t make_instance_decl_id(std::string type_class_name,
                                   instance_t *instance,
                                   identifier_t decl_id) {
  return identifier_t{make_instance_id(type_class_name, instance).name +
                          INSTANCE_ID_SEP + decl_id.name,
                      decl_id.location};
}

identifier_t make_instance_dict_id(std::string type_class_name,
                                   instance_t *instance) {
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
    env_t &env) {
  bool found = false;
  for (auto decl : instance->decls) {
    assert(name.find(".") != std::string::npos);
    // assert(decl->var.name.find(".") != std::string::npos);
    if (decl->var.name == name) {
      found = true;
      if (in(name, names_checked)) {
        throw user_error(decl->get_location(),
                         "name %s already duplicated in this instance",
                         decl->var.str().c_str());
      }
      names_checked.insert(decl->var.name);

      env_t local_env{env};
      local_env.instance_requirements.resize(0);
      identifier_t instance_decl_id = make_instance_decl_id(
          type_class->id.name, instance, decl->var);
      types::scheme_t::ref expected_scheme = type->rebind(subst)->generalize(
          local_env.get_predicate_map());

      expr_t *instance_decl_expr = new as_t(decl->value, expected_scheme,
                                            false /*force_cast*/);
      check(false /*check_constraint_coverage*/, instance_decl_id,
            instance_decl_expr, local_env);
      debug_above(
          5,
          log("checking the instance fn %s gave scheme %s. we expected %s.",
              instance_decl_id.str().c_str(),
              local_env.map[instance_decl_id.name]->normalize()->str().c_str(),
              expected_scheme->normalize()->str().c_str()));

      if (!scheme_equality(local_env.map[instance_decl_id.name],
                           expected_scheme->normalize())) {
        auto error = user_error(instance_decl_id.location,
                                "instance component %s appears to be more "
                                "constrained than the type class",
                                decl->var.str().c_str());
        error.add_info(
            instance_decl_id.location,
            "instance component declaration has scheme %s",
            local_env.map[instance_decl_id.name]->normalize()->str().c_str());
        error.add_info(type->get_location(),
                       "type class component declaration has scheme %s",
                       expected_scheme->normalize()->str().c_str());
        throw error;
      }

      /* keep track of any instance requirements that were referenced inside the
       * implementation of the type class instance components */
      for (auto ir : local_env.instance_requirements) {
        env.instance_requirements.push_back(ir);
      }

      env.map[instance_decl_id.name] = expected_scheme;

      instance_decls.push_back(
          new decl_t(instance_decl_id, instance_decl_expr));
      auto defn_id = defn_id_t{decl->var, expected_scheme};
      overrides_map[defn_id] = instance_decls.back();
    }
  }
  if (!found) {
    throw user_error(type->get_location(),
                     "could not find decl for %s in instance %s %s",
                     name.c_str(), type_class->id.str().c_str(),
                     instance->type->str().c_str());
  }
}

void check_instance_for_type_class_overloads(
    instance_t *instance,
    type_class_t *type_class,
    std::vector<decl_t *> &instance_decls,
    std::map<defn_id_t, decl_t *> &overrides_map,
    env_t &env) {
  /* make a template for the type that the instance implementation should
   * conform to */
  types::type_t::map subst;
  subst[type_class->type_var_id.name] = instance->type->generalize({})
                                            ->instantiate(INTERNAL_LOC());

  /* check whether this instance properly implements the given type class */
  std::set<std::string> names_checked;

  for (auto pair : type_class->overloads) {
    auto name = pair.first;
    auto type = pair.second;
    check_instance_for_type_class_overload(
        name, type, instance, type_class, subst, names_checked, instance_decls,
        overrides_map, env /*, type_class->defaults*/);
  }

  /* check for unrelated declarations inside of an instance */
  for (auto decl : instance->decls) {
    if (!in(decl->var.name, names_checked)) {
      throw user_error(decl->var.location,
                       "extraneous declaration %s found in instance %s %s "
                       "(names_checked = {%s})",
                       decl->var.str().c_str(), type_class->id.str().c_str(),
                       instance->type->str().c_str(),
                       join(names_checked, ", ").c_str());
    }
  }
}

std::vector<decl_t *> check_instances(
    const std::vector<instance_t *> &instances,
    const std::map<std::string, type_class_t *> &type_class_map,
    std::map<defn_id_t, decl_t *> &overrides_map,
    env_t &env) {
  std::vector<decl_t *> instance_decls;

  for (instance_t *instance : instances) {
    try {
      type_class_t *type_class = get(type_class_map,
                                     instance->type_class_id.name,
                                     static_cast<type_class_t *>(nullptr));

      if (type_class == nullptr) {
        /* Error Handling */
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
      check_instance_for_type_class_overloads(
          instance, type_class, instance_decls, overrides_map, env);

    } catch (user_error &e) {
      e.add_info(
          instance->type_class_id.location, "while checking instance %s %s",
          instance->type_class_id.str().c_str(), instance->type->str().c_str());
      print_exception(e);
    }
  }
  return instance_decls;
}

bool instance_matches_requirement(instance_t *instance,
                                  const instance_requirement_t &ir,
                                  env_t &env) {
  // log("checking %s %s vs. %s %s", ir.type_class_name.c_str(),
  // ir.type->str().c_str(), instance->type_class_id.name.c_str(),
  // instance->type->str().c_str());
  auto pm = env.get_predicate_map();
  return instance->type_class_id.name == ir.type_class_name &&
         scheme_equality(ir.type->generalize(pm)->normalize(),
                         instance->type->generalize(pm)->normalize());
}

void check_instance_requirements(const std::vector<instance_t *> &instances,
                                 env_t &env) {
  for (auto ir : env.instance_requirements) {
    debug_above(8, log("checking instance requirement %s", ir.str().c_str()));
    std::vector<instance_t *> matching_instances;
    for (auto instance : instances) {
      if (instance_matches_requirement(instance, ir, env)) {
        matching_instances.push_back(instance);
      }
    }

    if (matching_instances.size() == 0) {
      throw user_error(
          ir.location,
          "could not find an instance that supports the requirement %s",
          ir.str().c_str());
    } else if (matching_instances.size() != 1) {
      auto error = user_error(ir.location,
                              "found multiple instances implementing %s",
                              ir.str().c_str());
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

  friend struct phase_2_t;

public:
  decl_t *maybe_lookup(defn_id_t defn_id) const {
    auto iter = map.find(defn_id);
    decl_t *decl = nullptr;
    if (iter != map.end()) {
      decl = iter->second;
    }

    for (auto pair : map) {
      if (pair.second == decl) {
        /* we've already chosen this one, we're just checking for dupes */
        continue;
      }
      if (pair.first.id.name == defn_id.id.name) {
        if (scheme_equality(pair.first.scheme, defn_id.scheme)) {
          if (decl != nullptr) {
            throw user_error(defn_id.id.location,
                             "found ambiguous instance method");
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
  }

  decl_t *lookup(defn_id_t defn_id) const {
    auto decl = maybe_lookup(defn_id);
    if (decl == nullptr) {
      auto error = user_error(defn_id.id.location,
                              "symbol %s does not seem to exist",
                              defn_id.id.str().c_str());
      std::stringstream ss;

      for (auto pair : decl_map) {
        ss << pair.first << " " << pair.second << std::endl;
      }
      error.add_info(defn_id.id.location, "%s", ss.str().c_str());
      throw error;
    } else {
      return decl;
    }
  }

  void populate(const std::map<std::string, decl_t *> &decl_map_,
                const std::map<defn_id_t, decl_t *> &overrides_map,
                const env_t &env) {
    decl_map = decl_map_;

    /* populate the definition map which is the main result of the first phase
     * of compilation */
    for (auto pair : decl_map) {
      assert(pair.first == pair.second->var.name);
      auto scheme =
          env.lookup_env(pair.second->var)->generalize({})->normalize();
      auto defn_id = defn_id_t{pair.second->var, scheme};
      assert(!in(defn_id, map));
      debug_above(8, log("populating defn_map with %s", defn_id.str().c_str()));
      map[defn_id] = pair.second;
    }

    for (auto pair : overrides_map) {
      debug_above(8, log("populating defn_map with override %s",
                         pair.first.str().c_str()));
      assert(!in(pair.first, map));
      map[pair.first] = pair.second;
    }
  }
};

struct phase_2_t {
  std::shared_ptr<compilation_t const> const compilation;
  types::scheme_t::map const typing;
  defn_map_t const defn_map;
  ctor_id_map_t const ctor_id_map;
  data_ctors_map_t const data_ctors_map;

  std::ostream &dump(std::ostream &os) {
    for (auto pair : defn_map.decl_map) {
      auto scheme = get(typing, pair.first, types::scheme_t::ref{});
      assert(scheme != nullptr);
      os << pair.first << " = " << pair.second->str() << " :: " << scheme->str()
         << std::endl;
    }
    return os;
  }
};

std::map<std::string, int> get_builtin_arities() {
  const types::scheme_t::map &map = get_builtins();
  std::map<std::string, int> builtin_arities;
  for (auto pair : map) {
    types::type_t::refs terms;
    unfold_binops_rassoc(ARROW_TYPE_OPERATOR,
                         pair.second->instantiate(INTERNAL_LOC()), terms);
    builtin_arities[pair.first] = terms.size() - 1;
  }
  debug_above(
      7,
      log("builtin_arities are %s",
          join_with(builtin_arities, ", ", [](std::pair<std::string, int> a) {
            return string_format("%s: %d", a.first.c_str(), a.second);
          }).c_str()));
  return builtin_arities;
}

phase_2_t compile(std::string user_program_name_) {
  auto builtin_arities = get_builtin_arities();
  auto compilation = compiler::parse_program(user_program_name_,
                                             builtin_arities);
  if (compilation == nullptr) {
    exit(EXIT_FAILURE);
  }

  program_t *program = compilation->program;

  env_t env{{} /*map*/,
            nullptr /*return_type*/,
            {} /*instance_requirements*/,
            std::make_shared<tracked_types_t>(),
            compilation->ctor_id_map,
            compilation->data_ctors_map};

  initialize_default_env(env);

  auto type_class_map = check_type_classes(program->type_classes, env);

  check_decls(compilation->program_name + ".main", program->decls, env);
  std::map<defn_id_t, decl_t *> overrides_map;

  auto instance_decls = check_instances(program->instances, type_class_map,
                                        overrides_map, env);

  std::map<std::string, decl_t *> decl_map;

  for (auto decl : program->decls) {
    assert(!in(decl->var.name, decl_map));
    decl_map[decl->var.name] = decl;
  }

  /* the instance decls were already checked, but let's add them to the list of
   * decls for the lowering step */
  for (auto decl : instance_decls) {
    assert(!in(decl->var.name, decl_map));
    decl_map[decl->var.name] = decl;
  }

#if 0
    if (debug_compiled_env) {
        INDENT(0, "--debug_compiled_env--");
        for (auto pair : env.map) {
            log("%s" c_good(" :: ") c_type("%s"), pair.first.c_str(),
                pair.second->normalize()->str().c_str());
        }
    }
#endif

  for (auto pair : decl_map) {
    assert(pair.first == pair.second->var.name);

    auto type = env.lookup_env(
        identifier_t(pair.first, pair.second->var.location));
#if 0
        if (debug_compiled_env) {
            INDENT(0, "--debug_compiled_env--");
            log("%s " c_good("::") " %s", pair.second->str().c_str(),
                env.map[pair.first]->str().c_str());
        }
#endif
  }

  try {
    check_instance_requirements(program->instances, env);
  } catch (user_error &e) {
    print_exception(e);
  }

  if (debug_all_expr_types) {
    INDENT(0, "--debug_all_expr_types--");
    log(c_good("All Expression Types"));
    for (auto pair : *env.tracked_types) {
      log_location(pair.first->get_location(), "%s :: %s",
                   pair.first->str().c_str(),
                   pair.second->generalize({})->str().c_str());
    }
  }

  defn_map_t defn_map;
  defn_map.populate(decl_map, overrides_map, env);
  return phase_2_t{compilation, env.map, defn_map, compilation->ctor_id_map,
                   compilation->data_ctors_map};
}

typedef std::map<
    std::string,
    std::map<types::type_t::ref, translation_t::ref, types::compare_type_t>>
    translation_map_t;

void specialize_core(const types::type_env_t &type_env,
                     const defn_map_t &defn_map,
                     const types::scheme_t::map &typing,
                     const ctor_id_map_t &ctor_id_map,
                     const data_ctors_map_t &data_ctors_map,
                     defn_id_t defn_id,
                     /* output */ translation_map_t &translation_map,
                     /* output */ needed_defns_t &needed_defns) {
  if (starts_with(defn_id.id.name, "__builtin_")) {
    return;
  }

  /* expected type schemes for specializations can have unresolved type
   * variables. That indicates that an input to the function is irrelevant to
   * the output. However, since Zion is eagerly evaluated and permits impure
   * side effects, it may not be irrelevant to the overall program's
   * semantics, so terms with ambiguous types cannot be thrown out altogether.
   */

  /* They cannot have unresolved bounded type variables, because that would
   * imply that we don't know which type class instance to choose within the
   * inner specialization. */
  assert(defn_id.scheme->btvs() == 0);

  const auto type = defn_id.scheme->instantiate({});
  auto translation = get(translation_map, defn_id.id.name, type,
                         translation_t::ref{});
  if (translation != nullptr) {
    debug_above(6, log("we have already specialized %s. it is %s",
                       defn_id.str().c_str(), translation->str().c_str()));
    return;
  }

  /* ... like a GRAY mark in the visited set... */
  translation_map[defn_id.id.name][type] = nullptr;
  try {
    debug_above(
        7, log(c_good("Specializing subprogram %s"), defn_id.str().c_str()));

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
    env_t env{typing /*map*/,
              nullptr /*return_type*/,
              {} /*instance_requirements*/,
              {} /*tracked_types*/,
              ctor_id_map,
              data_ctors_map};
    // TODO: clean this up. it is ugly. we're accessing the base class
    // translation_env_t's tracked_types
    auto tracked_types = std::make_shared<tracked_types_t>();
    env.tracked_types = tracked_types;

    decl_t *decl_to_check = defn_map.maybe_lookup(defn_id);
    if (decl_to_check == nullptr) {
      throw user_error(defn_id.id.location,
                       "could not find a definition for %s :: %s",
                       defn_id.id.str().c_str(), defn_id.scheme->str().c_str());
    }

    expr_t *to_check = decl_to_check->value;
    const std::string final_name = defn_id.id.name;

    /* wrap this expr in it's asserted type to ensure that it monomorphizes */
    auto as_defn = new as_t(to_check, defn_id.scheme, false);
    auto defn_to_check = identifier_t{defn_id.repr_public(),
                                      defn_id.id.location};
    check(true /*check_constraint_coverage*/, defn_to_check, as_defn, env);
    assert(env.tracked_types == tracked_types);

    if (debug_specialized_env) {
      for (auto pair : *tracked_types) {
        log_location(pair.first->get_location(), "%s :: %s",
                     pair.first->str().c_str(), pair.second->str().c_str());
      }
    }

    // TODO: this may be the right spot to look through the tracked_types and
    // determine how to rebind any type variables with btvs > 0 so that we can
    // resolve instance defaults... we'll need to check to ensure that we are
    // properly adding to needed_defns
#if 0
    for (auto pair : tracked_types) {
      const bitter::expr_t *expr;
      types::type_t::ref type;
      std::tie(std::ref(expr), std::ref(type)) = pair;
    }
#endif

    translation_env_t tenv{tracked_types, ctor_id_map, data_ctors_map};
    std::unordered_set<std::string> bound_vars;
    INDENT(6, string_format("----------- specialize %s ------------",
                            defn_id.str().c_str()));
    bool returns = true;
    auto translated_decl = translate_expr(
        defn_id, as_defn, bound_vars, type_env, tenv, needed_defns, returns);

    assert(returns);

    if (debug_all_translated_defns) {
      log_location(defn_id.id.location, "%s = %s", defn_id.str().c_str(),
                   translated_decl->str().c_str());
    }

    debug_above(4, log("setting %s :: %s = %s", final_name.c_str(),
                       type->str().c_str(), translated_decl->str().c_str()));
    translation_map[final_name][type] = translated_decl;
  } catch (...) {
    translation_map[defn_id.id.name].erase(type);
    throw;
  }
}

struct phase_3_t {
  phase_2_t phase_2;
  translation_map_t translation_map;

  std::ostream &dump(std::ostream &os) {
    for (auto pair : translation_map) {
      for (auto overloads : pair.second) {
        log_location(overloads.second->expr->get_location(), "%s :: %s = %s",
                     pair.first.c_str(), overloads.first->str().c_str(),
                     overloads.second->expr->str().c_str());
      }
    }
    return os;
  }
};

phase_3_t specialize(const phase_2_t &phase_2) {
  if (user_error::errors_occurred()) {
    throw user_error(INTERNAL_LOC(), "quitting");
  }
  decl_t *program_main = phase_2.defn_map.lookup(
      {make_iid(phase_2.compilation->program_name + ".main"),
       program_main_scheme});

  needed_defns_t needed_defns;
  defn_id_t main_defn{program_main->var, program_main_scheme};
  insert_needed_defn(needed_defns, main_defn, INTERNAL_LOC(), main_defn);

  translation_map_t translation_map;
  while (needed_defns.size() != 0) {
    auto next_defn_id = needed_defns.begin()->first;
    try {
      specialize_core(phase_2.compilation->type_env, phase_2.defn_map,
                      phase_2.typing, phase_2.ctor_id_map,
                      phase_2.data_ctors_map, next_defn_id, translation_map,
                      needed_defns);
    } catch (user_error &e) {
      if (fast_fail) {
        throw;
      } else {
        print_exception(e);
        /* and continue */
      }
    }
    needed_defns.erase(next_defn_id);
  }

  if (debug_compiled_env) {
    INDENT(0, "--debug_compiled_env--");
    for (auto pair : translation_map) {
      for (auto overload : pair.second) {
        if (pair.first == "std.Ref") {
          assert(overload.second != nullptr);
          log_location(overload.second->get_location(), "%s :: %s = %s",
                       pair.first.c_str(), overload.first->str().c_str(),
                       overload.second->str().c_str());
        }
      }
    }
  }
  return phase_3_t{phase_2, translation_map};
}

struct phase_4_t {
  phase_4_t(const phase_4_t &) = delete;
  phase_4_t(phase_3_t phase_3,
            gen::gen_env_t &&gen_env,
            llvm::Module *llvm_module,
            llvm::DIBuilder *dbuilder,
            std::string output_llvm_filename)
      : phase_3(phase_3), gen_env(std::move(gen_env)), llvm_module(llvm_module),
        dbuilder(dbuilder), output_llvm_filename(output_llvm_filename) {
  }
  phase_4_t(phase_4_t &&rhs)
      : phase_3(rhs.phase_3), gen_env(std::move(rhs.gen_env)),
        llvm_module(rhs.llvm_module), dbuilder(rhs.dbuilder),
        output_llvm_filename(rhs.output_llvm_filename) {
    rhs.llvm_module = nullptr;
    rhs.dbuilder = nullptr;
  }
  ~phase_4_t() {
    delete dbuilder;
    delete llvm_module;
    // FUTURE: unlink(output_llvm_filename.c_str());
  }

  phase_3_t phase_3;
  gen::gen_env_t gen_env;
  llvm::Module *llvm_module = nullptr;
  llvm::DIBuilder *dbuilder = nullptr;
  std::string output_llvm_filename;

  std::ostream &dump(std::ostream &os) {
    if (dbuilder != nullptr) {
      dbuilder->finalize();
    }
    return os << llvm_print_module(*llvm_module);
  }
};

struct code_symbol_t {
  std::string name;
  types::type_t::ref type;
  const tracked_types_t &typing;
  const bitter::expr_t *expr;
};

std::unordered_set<std::string> get_globals(const phase_3_t &phase_3) {
  std::unordered_set<std::string> globals;
  for (auto pair : phase_3.translation_map) {
    debug_above(7, log("adding global %s", pair.first.c_str()));
    globals.insert(pair.first);
  }

  /* make sure the builtins are in the list of globals so that they don't get
   * collected when creating closures */
  for (auto pair : get_builtins()) {
    globals.insert(pair.first);
  }
  return globals;
}

void build_main_function(llvm::IRBuilder<> &builder,
                         llvm::Module *llvm_module,
                         const gen::gen_env_t &gen_env,
                         std::string program_name) {
  std::string main_closure = program_name + ".main";
  types::type_t::ref main_type = type_arrows(
      {type_unit(INTERNAL_LOC()), type_unit(INTERNAL_LOC())});

  llvm::Type *llvm_main_args_types[] = {
      builder.getInt32Ty(),
      builder.getInt8Ty()->getPointerTo()->getPointerTo()};
  llvm::Function *llvm_function = llvm::Function::Create(
      llvm::FunctionType::get(
          builder.getInt32Ty(),
          llvm::ArrayRef<llvm::Type *>(llvm_main_args_types),
          false /*isVarArgs*/),
      llvm::Function::ExternalLinkage, "main", llvm_module);

  llvm_function->setDoesNotThrow();

  llvm::BasicBlock *block = llvm::BasicBlock::Create(
      builder.getContext(), "program_entry", llvm_function);
  builder.SetInsertPoint(block);

  llvm::Value *llvm_main_closure = gen::get_env_var(
      builder, gen_env, make_iid(main_closure), main_type);

  llvm::Value *gep_path[] = {builder.getInt32(0), builder.getInt32(0)};
  llvm::Value *main_func = builder.CreateLoad(
      builder.CreateInBoundsGEP(llvm_main_closure, gep_path));
  llvm::Value *main_args[] = {
      llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo()),
      builder.CreateBitCast(llvm_main_closure,
                            builder.getInt8Ty()->getPointerTo())};
  builder.CreateCall(main_func, llvm::ArrayRef<llvm::Value *>(main_args));
  builder.CreateRet(builder.getInt32(0));
}

phase_4_t ssa_gen(llvm::LLVMContext &context, const phase_3_t &phase_3) {
  llvm::Module *llvm_module = new llvm::Module("program", context);
  llvm::DIBuilder *dbuilder = nullptr; // new llvm::DIBuilder(*module);
  /*
  dbuilder->createCompileUnit(llvm::dwarf::DW_LANG_C,
                              dbuilder->createFile("lib/std.zion", "."),
                              "Zion Compiler", 0, "", 0);
                              */
  llvm::IRBuilder<> builder(context);

  gen::gen_env_t gen_env;

  /* resolvers is the list of top-level symbols that need to be resolved. they
   * can be traversed in any order, and will automatically resolve in dependency
   * order based on a topological sorting. this could be a bit intense on the
   * stack, worst case is on the same order as the user's program stack depth.
   */
  std::list<std::shared_ptr<gen::resolver_t>> resolvers;
  std::string output_filename;

  try {
    const std::unordered_set<std::string> globals = get_globals(phase_3);

    debug_above(6, log("globals are %s", join(globals).c_str()));
    debug_above(2, log("type_env is %s",
                       str(phase_3.phase_2.compilation->type_env).c_str()));
    for (auto pair : phase_3.translation_map) {
      for (auto &overload : pair.second) {
        const std::string &name = pair.first;
        const types::type_t::ref &type = overload.first;
        translation_t::ref translation = overload.second;

        debug_above(4, log("fetching %s expression type from translated types",
                           name.c_str()));
        debug_above(
            4,
            log("%s should be the same type as %s",
                get(translation->typing, translation->expr, {})->str().c_str(),
                type->str().c_str()));
        assert(type_equality(get(translation->typing, translation->expr, {}),
                             type));

        /* at this point we should not have a resolver or a declaration or
         * definition or anything for this symbol */
        llvm::Value *value = gen::maybe_get_env_var(
            gen_env, {pair.first, overload.second->expr->get_location()}, type);
        assert(value == nullptr);

        debug_above(4, log("making a placeholder proxy value for %s :: %s = %s",
                           pair.first.c_str(), type->str().c_str(),
                           translation->expr->str().c_str()));

        std::shared_ptr<gen::resolver_t> resolver = gen::lazy_resolver(
            name, type,
            [&builder, &llvm_module, name, translation, &phase_3, &gen_env,
             &globals](llvm::Value **llvm_value) -> gen::resolution_status_t {
              gen::publishable_t publishable(llvm_value);
              return gen::gen(
                  name, builder, llvm_module, nullptr /*break_to_block*/,
                  nullptr /*continue_to_block*/, translation->expr,
                  translation->typing, phase_3.phase_2.compilation->type_env,
                  gen_env, {}, globals, &publishable);
            });

        resolvers.push_back(resolver);
        gen_env[name].emplace(type, resolver);
      }
    }

    // TODO: differentiate between globals and functions...
    // global initialization will happen inside of the __program_init function
    for (auto resolver : resolvers) {
      debug_above(2, log("resolving %s...", resolver->str().c_str()));
      llvm::Value *value = resolver->resolve();
      debug_above(2, log("resolved to %s", llvm_print(value).c_str()));
    }

    output_filename = "./" + phase_3.phase_2.compilation->program_name + ".ll";
    llvm::IRBuilder<> builder(context);
    build_main_function(builder, llvm_module, gen_env,
                        phase_3.phase_2.compilation->program_name);

    llvm_verify_module(*llvm_module);

    std::ofstream ofs;
    ofs.open(output_filename.c_str(), std::ofstream::out);
    ofs << llvm_print_module(*llvm_module) << std::endl;
    ofs.close();

  } catch (user_error &e) {
    print_exception(e);
    /* and continue */
  }

  return phase_4_t(phase_3, std::move(gen_env), llvm_module, dbuilder,
                   output_filename);
}

struct job_t {
  std::string cmd;
  std::vector<std::string> opts;
  std::vector<std::string> args;
};

int run_job(const job_t &job) {
  get_help = in_vector("-help", job.opts) || in_vector("--help", job.opts);
  debug_compiled_env = (getenv("SHOW_ENV") != nullptr) ||
                       in_vector("-show-env", job.opts);
  debug_types = (getenv("SHOW_TYPES") != nullptr) ||
                in_vector("-show-types", job.opts);
  debug_all_expr_types = (getenv("SHOW_EXPR_TYPES") != nullptr) ||
                         in_vector("-show-expr-types", job.opts);
  debug_all_translated_defns = (getenv("SHOW_DEFN_TYPES") != nullptr) ||
                               in_vector("-show-defn-types", job.opts);
  max_tuple_size = (getenv("ZION_MAX_TUPLE") != nullptr)
                       ? atoi(getenv("ZION_MAX_TUPLE"))
                       : 16;

  std::map<std::string, std::function<int(const job_t &, bool)>> cmd_map;
  cmd_map["test"] = [&](const job_t &job, bool explain) {
    if (explain) {
      std::cerr << "test: run tests" << std::endl;
      return EXIT_FAILURE;
    }
    assert(alphabetize(0) == "a");
    assert(alphabetize(1) == "b");
    assert(alphabetize(2) == "c");
    assert(alphabetize(26) == "aa");
    assert(alphabetize(27) == "ab");
    return run_job({"run", {}, {"test_basic"}});
  };
  cmd_map["find"] = [&](const job_t &job, bool explain) {
    if (explain) {
      std::cerr << "find: resolve module filenames" << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}, {}});
    }
    log("%s",
        compiler::resolve_module_filename(INTERNAL_LOC(), job.args[0], ".zion")
            .c_str());
    return EXIT_SUCCESS;
  };
  cmd_map["lex"] = [&](const job_t &job, bool explain) {
    if (explain) {
      std::cerr << "lex: lexes Zion into tokens" << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    }

    std::string filename = compiler::resolve_module_filename(
        INTERNAL_LOC(), job.args[0], ".zion");
    std::ifstream ifs;
    ifs.open(filename.c_str());
    zion_lexer_t lexer({filename}, ifs);
    token_t token;
    bool newline = false;
    while (lexer.get_token(token, newline, nullptr)) {
      log_location(token.location, "%s (%s)", token.text.c_str(),
                   tkstr(token.tk));
    }
    return EXIT_SUCCESS;
  };

  cmd_map["parse"] = [&](const job_t &job, bool explain) {
    if (explain) {
      std::cerr << "parse: parses Zion into an intermediate lambda calculus"
                << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    }

    std::string user_program_name = job.args[0];
    auto compilation = compiler::parse_program(user_program_name,
                                               get_builtin_arities());
    if (compilation != nullptr) {
      for (auto decl : compilation->program->decls) {
        log_location(decl->var.location, "%s = %s", decl->var.str().c_str(),
                     decl->value->str().c_str());
      }
      for (auto type_class : compilation->program->type_classes) {
        log_location(type_class->id.location, "%s", type_class->str().c_str());
      }
      for (auto instance : compilation->program->instances) {
        log_location(instance->type_class_id.location, "%s",
                     instance->str().c_str());
      }
      return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
  };
  cmd_map["compile"] = [&](const job_t &job, bool explain) {
    if (explain) {
      std::cerr << "compile: parses and compiles Zion into an intermediate "
                   "lambda calculus. this performs type checking"
                << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    } else {
      auto phase_2 = compile(job.args[0]);
      if (user_error::errors_occurred()) {
        return EXIT_FAILURE;
      }
      phase_2.dump(std::cout);

      return user_error::errors_occurred() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
  };
  cmd_map["specialize"] = [&](const job_t &job, bool explain) {
    if (explain) {
      std::cerr << "specialize: compiles, then specializes the Zion lambda "
                   "calculus to a monomorphized form"
                << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    } else {
      auto phase_3 = specialize(compile(job.args[0]));
      if (user_error::errors_occurred()) {
        return EXIT_FAILURE;
      }
      phase_3.dump(std::cout);
      return user_error::errors_occurred() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
  };
  cmd_map["ll"] = [&](const job_t &job, bool explain) {
    if (explain) {
      std::cerr << "ll: compiles, specializes, then generates LLVM output"
                << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    } else {
      llvm::LLVMContext context;
      phase_4_t phase_4 = ssa_gen(context, specialize(compile(job.args[0])));

      return user_error::errors_occurred() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
  };
  cmd_map["run"] = [&](const job_t &job, bool explain) {
    if (explain) {
      std::cerr << "run: compiles, specializes, generates LLVM output, then "
                   "runs the generated binary"
                << std::endl;
      return EXIT_FAILURE;
    }
    llvm::LLVMContext context;
    phase_4_t phase_4 = ssa_gen(context, specialize(compile(job.args[0])));

    if (!user_error::errors_occurred()) {
      auto command_line = string_format(
          // We are using clang to lower the code from LLVM, and link it
          // to the runtime.
          "clang ${ZION_OPT_FLAGS} "
          // NB: we don't embed the target triple into the LL, so any
          // targeted triple causes an ugly error from clang, so I just
          // ignore it here.
          "-Wno-override-module "
          // TODO: plumb host targeting through clang here
          "--target=$(llvm-config --host-target) "
          // TODO: plumb zion_rt.c properly into installation location.
          // probably something like /usr/share/zion/rt
          "${ZION_RT}/zion_rt.c "
          "%s "
          "-o %s",
          phase_4.output_llvm_filename.c_str(),
          phase_4.phase_3.phase_2.compilation->program_name.c_str(),
          phase_4.phase_3.phase_2.compilation->program_name.c_str());
      if (std::system(command_line.c_str()) != 0) {
        throw user_error(INTERNAL_LOC(), "failed to compile binary");
      }

      return run_program(phase_4.phase_3.phase_2.compilation->program_name, {});
    } else {
      return EXIT_FAILURE;
    }
  };

  if (!in(job.cmd, cmd_map)) {
    job_t new_job;
    new_job.args.insert(new_job.args.begin(), job.cmd);
    std::copy(job.args.begin(), job.args.end(),
              std::back_inserter(new_job.args));
    new_job.cmd = "run";
    return cmd_map["run"](new_job, false /*explain*/);
  } else {
    return cmd_map[job.cmd](job, get_help);
  }
#if 0
    std::cerr << "bad CLI invocation of " << job.cmd << " "
              << join(job.args, " ") << std::endl;
    for (auto pair : cmd_map) {
      pair.second(true /*explain*/);
    }
    return EXIT_FAILURE;
  } else {
    return cmd_map[job.cmd](get_help);
  }
#endif
}

int main(int argc, char *argv[]) {
  signal(SIGINT, &handle_sigint);
  init_dbg();
  std::shared_ptr<logger> logger(std::make_shared<standard_logger>("", "."));

  job_t job;
  if (1 < argc) {
    int index = 1;
    job.cmd = argv[index++];
    while (index < argc) {
      if (starts_with(argv[index], "-")) {
        job.opts.push_back(argv[index++]);
      } else {
        job.args.push_back(argv[index++]);
      }
    }
  } else {
    job.cmd = "help";
  }

  try {
    return run_job(job);
  } catch (user_error &e) {
    print_exception(e);
    /* and continue */
    return EXIT_FAILURE;
  }
}
