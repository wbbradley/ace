#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>

#include "ast.h"
#include "builtins.h"
#include "class_predicate.h"
#include "compiler.h"
#include "disk.h"
#include "env.h"
#include "gen.h"
#include "host.h"
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

types::Ref program_main_type = type_arrows(
    {type_unit(INTERNAL_LOC()), type_unit(INTERNAL_LOC())});
types::Scheme::Ref program_main_scheme = scheme({}, {}, program_main_type);

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
    int ret = execvp(("./" + executable).c_str(),
                     const_cast<char **>(&args[0]));
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
           Identifier id,
           const Expr *expr,
           Env &env) {
  Constraints constraints;
  types::ClassPredicates instance_requirements;
  log("type checking %s = %s", id.str().c_str(), expr->str().c_str());
  types::Ref ty = infer(expr, env, constraints, instance_requirements);
  types::Map bindings = solver(check_constraint_coverage,
                               make_context(id.location, "solving %s :: %s",
                                            id.name.c_str(), ty->str().c_str()),
                               constraints, env, instance_requirements);

  ty = ty->rebind(bindings);
  instance_requirements = types::rebind(instance_requirements, bindings);
  types::SchemeRef scheme = ty->generalize(instance_requirements)->normalize();

  debug_above(3, log_location(id.location, "adding %s to env as %s",
                              id.str().c_str(), scheme->str().c_str()));
  // log_location(id.location, "let %s = %s", id.str().c_str(),
  // expr->str().c_str());
  env.extend(id, scheme, true /*allow_subscoping*/);
}

std::vector<std::string> alphabet(int count) {
  std::vector<std::string> xs;
  for (int i = 0; i < count; ++i) {
    xs.push_back(alphabetize(i));
  }
  return xs;
}

void initialize_default_env(Env &env) {
  for (auto pair : get_builtins()) {
    env.map[pair.first] = pair.second;
  }
}

std::map<std::string, const TypeClass *> check_type_classes(
    const std::vector<const TypeClass *> &type_classes,
    Env &env) {
  std::map<std::string, const TypeClass *> type_class_map;

  /* introduce all the type class signatures into the env, and build up an
   * index of type_class names */
  for (const TypeClass *type_class : type_classes) {
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

      auto predicates = type_class->class_predicates;
      predicates.insert(std::make_shared<types::ClassPredicate>(
          type_class->id, type_class->type_var_ids));

      std::map<std::string, std::string> remapping;
      for (auto &var_id : type_class->type_var_ids) {
        remapping[var_id.name] = gensym_name();
      }
      predicates = types::remap_vars(predicates, remapping);

      for (auto pair : type_class->overloads) {
        if (in(pair.first, env.map)) {
          auto error = user_error(pair.second->get_location(),
                                  "the name " c_id("%s") " is already in use",
                                  pair.first.c_str());
          error.add_info(env.map[pair.first]->get_location(),
                         "see first declaration here");
          throw error;
        }

        env.extend(Identifier{pair.first, pair.second->get_location()},
                   pair.second->remap_vars(remapping)->generalize(predicates),
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
                 const std::vector<const Decl *> &decls,
                 Env &env) {
  for (const Decl *decl : decls) {
    /* seed each decl with a type variable to let inference resolve */
    env.extend(decl->id, type_variable(INTERNAL_LOC())->generalize({}),
               true /*allow_subscoping*/);
  }

  for (const Decl *decl : decls) {
    try {
      /* make sure that the "main" function has the correct signature */
      check(false /*check_constraint_coverage*/, decl->id,
            (decl->id.name == entry_point_name)
                ? new As(decl->value, program_main_scheme, false /*force_cast*/)
                : decl->value,
            env);
    } catch (user_error &e) {
      print_exception(e);

      /* keep trying other decls, and pretend like this function gives back
       * whatever the user wants... */
      env.extend(decl->id,
                 type_arrow(type_variable(INTERNAL_LOC()),
                            type_variable(INTERNAL_LOC()))
                     ->generalize({}),
                 false /*allow_subscoping*/);
    }
  }
}

Identifier make_instance_decl_id(const Instance *instance, Identifier decl_id) {
  return Identifier{string_format("%s/%s",
                                  instance->class_predicate->repr().c_str(),
                                  decl_id.name.c_str()),
                    decl_id.location};
}

void check_instance_for_type_class_overload(
    std::string name,
    types::Ref type,
    const Instance *instance,
    const types::Map &subst,
    std::set<std::string> &names_checked,
    std::vector<const Decl *> &instance_decls,
    std::map<DefnId, const Decl *> &overrides_map,
    Env &env,
    const types::ClassPredicates &class_predicates) {
  bool found = false;
  for (const Decl *decl : instance->decls) {
    assert(name.find(".") != std::string::npos);
    // assert(decl->id.name.find(".") != std::string::npos);
    if (decl->id.name == name) {
      found = true;
      if (in(name, names_checked)) {
        throw user_error(decl->get_location(),
                         "name %s already duplicated in this instance",
                         decl->id.str().c_str());
      }
      names_checked.insert(decl->id.name);

      Env local_env{env};
      Identifier instance_decl_id = make_instance_decl_id(instance, decl->id);
      types::Scheme::Ref expected_scheme = type->rebind(subst)->generalize(
          class_predicates);

      Expr *instance_decl_expr = new As(decl->value, expected_scheme,
                                        false /*force_cast*/);
      check(false /*check_constraint_coverage*/, instance_decl_id,
            instance_decl_expr, local_env);

      log("checking the instance fn %s gave scheme %s. we expected %s.",
          instance_decl_id.str().c_str(),
          local_env.map[instance_decl_id.name]->normalize()->str().c_str(),
          expected_scheme->normalize()->str().c_str());

      if (!scheme_equality(local_env.map[instance_decl_id.name],
                           expected_scheme->normalize())) {
        auto error = user_error(instance_decl_id.location,
                                "instance component %s appears to be more "
                                "constrained than the type class",
                                decl->id.str().c_str());
        error.add_info(
            instance_decl_id.location,
            "instance component declaration has scheme %s",
            local_env.map[instance_decl_id.name]->normalize()->str().c_str());
        error.add_info(type->get_location(),
                       "type class component declaration has scheme %s",
                       expected_scheme->normalize()->str().c_str());
        throw error;
      }

      env.map[instance_decl_id.name] = expected_scheme;

      instance_decls.push_back(new Decl(instance_decl_id, instance_decl_expr));
      auto defn_id = DefnId{decl->id, expected_scheme};
      overrides_map[defn_id] = instance_decls.back();
    }
  }
  if (!found) {
    throw user_error(type->get_location(),
                     "could not find decl for %s in instance %s", name.c_str(),
                     instance->class_predicate->str().c_str());
  }
}

/* typecheck an instance for whether it properly overloads the type class with
 * which it is associated */
void check_instance_for_type_class_overloads(
    const Instance *instance,
    const TypeClass *type_class,
    std::vector<const Decl *> &instance_decls,
    std::map<DefnId, const Decl *> &overrides_map,
    Env &env) {
  /* make a template for the types that the instance implementation should
   * conform to */
  types::Map subst;

  types::Refs new_type_parameters;

  // find all ftvs
  // freshen them all
  const types::Ftvs &ftvs = instance->class_predicate->get_ftvs();
  for (auto &ftv : ftvs) {
    assert(in(ftv, type_class->type_var_ids));
  }
  int i = 0;
  assert(instance->class_predicate->params.size() ==
         type_class->type_var_ids.size());
  for (auto &type_var_id : type_class->type_var_ids) {
    subst[type_var_id.name] = instance->class_predicate->params[i++];
  }

  /* check whether this instance properly implements the given type class */
  std::set<std::string> names_checked;

  for (auto pair : type_class->overloads) {
    auto name = pair.first;
    auto type = pair.second;
    check_instance_for_type_class_overload(
        name, type, instance, subst, names_checked, instance_decls,
        overrides_map, env,
        type_class->class_predicates /*, type_class->defaults*/);
  }

  /* check for unrelated declarations inside of an instance */
  for (const Decl *decl : instance->decls) {
    if (!in(decl->id.name, names_checked)) {
      throw user_error(decl->id.location,
                       "extraneous declaration %s found in instance %s "
                       "(names_checked = {%s})",
                       decl->id.str().c_str(),
                       instance->class_predicate->str().c_str(),
                       join(names_checked, ", ").c_str());
    }
  }
}

std::vector<const Decl *> check_instances(
    const std::vector<const Instance *> &instances,
    const std::map<std::string, const TypeClass *> &type_class_map,
    std::map<DefnId, const Decl *> &overrides_map,
    Env &env) {
  std::vector<const Decl *> instance_decls;

  for (const Instance *instance : instances) {
    try {
      const TypeClass *type_class = get(
          type_class_map, instance->class_predicate->classname.name,
          static_cast<const TypeClass *>(nullptr));

      if (type_class == nullptr) {
        /* Error Handling */
        auto error = user_error(instance->class_predicate->get_location(),
                                "could not find type class for instance %s",
                                instance->class_predicate->str().c_str());
        auto leaf_name = split(instance->class_predicate->classname.name, ".")
                             .back();
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
      e.add_info(instance->class_predicate->get_location(),
                 "while checking instance %s",
                 instance->class_predicate->str().c_str());
      print_exception(e);
    }
  }
  return instance_decls;
}

#if 0
bool instance_matches_requirement(Instance *instance,
                                  const types::ClassPredicateRef &ir,
                                  Env &env) {
  // log("checking %s %s vs. %s %s", ir.type_class_name.c_str(),
  // ir.type->str().c_str(), instance->type_class_id.name.c_str(),
  // instance->type->str().c_str());
  auto pm = env.get_predicate_map();
  if (instance->class_predicate->classname.name != ir->classname.name) {
    return false;
  }

  return type_equality(type_tuple(ir->params),
                       type_tuple(instance->class_predicate->params));
}

void check_instance_requirements(const std::vector<Instance *> &instances,
                                 Env &env) {
  for (auto ir : env.instance_requirements) {
    log("checking instance requirement %s", ir->str().c_str());
    std::vector<Instance *> matching_instances;
    for (auto instance : instances) {
      if (instance_matches_requirement(instance, ir, env)) {
        matching_instances.push_back(instance);
      }
    }

    if (matching_instances.size() == 0) {
      throw user_error(
          ir->get_location(),
          "could not find an instance that supports the requirement %s",
          ir->str().c_str());
    } else if (matching_instances.size() != 1) {
      auto error = user_error(ir->get_location(),
                              "found multiple instances implementing %s",
                              ir->str().c_str());
      for (auto mi : matching_instances) {
        error.add_info(mi->get_location(), "matching instance found is %s",
                       mi->class_predicate->str().c_str());
      }
      throw error;
    }
  }
}
#endif

class defn_map_t {
  std::map<DefnId, const Decl *> map;
  std::map<std::string, const Decl *> decl_map;

  friend struct phase_2_t;

public:
  const Decl *maybe_lookup(DefnId defn_id) const {
    auto iter = map.find(defn_id);
    const Decl *decl = nullptr;
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

  const Decl *lookup(DefnId defn_id) const {
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

  void populate(const std::map<std::string, const Decl *> &decl_map_,
                const std::map<DefnId, const Decl *> &overrides_map,
                const Env &env) {
    decl_map = decl_map_;

    /* populate the definition map which is the main result of the first phase
     * of compilation */
    for (auto pair__ : decl_map) {
      const std::string &decl_name = pair__.first;
      const Decl *decl = pair__.second;
      assert(decl_name == decl->id.name);

      types::Scheme::Ref scheme = env.lookup_env(decl->id)->normalize();
      DefnId defn_id = DefnId{decl->id, scheme};
      assert(!in(defn_id, map));
      debug_above(8, log("populating defn_map with %s", defn_id.str().c_str()));
      map[defn_id] = decl;
    }

    for (auto pair : overrides_map) {
      const DefnId &defn_id = pair.first;
      debug_above(8, log("populating defn_map with override %s",
                         defn_id.str().c_str()));
      assert(!in(defn_id, map));
      map[defn_id] = pair.second;
    }
  }
};

struct phase_2_t {
  std::shared_ptr<Compilation const> const compilation;
  types::Scheme::Map const typing;
  defn_map_t const defn_map;
  CtorIdMap const ctor_id_map;
  DataCtorsMap const data_ctors_map;

  std::ostream &dump(std::ostream &os) {
    for (auto pair : defn_map.decl_map) {
      auto scheme = get(typing, pair.first, types::Scheme::Ref{});
      assert(scheme != nullptr);
      os << pair.first << " = " << pair.second->str() << " :: " << scheme->str()
         << std::endl;
    }
    return os;
  }
};

std::map<std::string, int> get_builtin_arities() {
  const types::Scheme::Map &map = get_builtins();
  std::map<std::string, int> builtin_arities;
  for (auto pair : map) {
    types::Refs terms;
    unfold_binops_rassoc(ARROW_TYPE_OPERATOR, pair.second->type, terms);
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

  const Program *program = compilation->program;

  Env env{{} /*map*/,
          nullptr /*return_type*/,
          std::make_shared<TrackedTypes>(),
          compilation->ctor_id_map,
          compilation->data_ctors_map};

  initialize_default_env(env);

  auto type_class_map = check_type_classes(program->type_classes, env);

  check_decls(compilation->program_name + ".main", program->decls, env);
  std::map<DefnId, const Decl *> overrides_map;

  std::vector<const Decl *> instance_decls = check_instances(
      program->instances, type_class_map, overrides_map, env);

  std::map<std::string, const Decl *> decl_map;
  for (const Decl *decl : program->decls) {
    assert(!in(decl->id.name, decl_map));
    decl_map[decl->id.name] = decl;
  }

  /* the instance decls were already checked, but let's add them to the list of
   * decls for the lowering step */
  for (auto decl : instance_decls) {
    assert(!in(decl->id.name, decl_map));
    decl_map[decl->id.name] = decl;
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
    assert(pair.first == pair.second->id.name);

    auto type = env.lookup_env(
        Identifier(pair.first, pair.second->id.location));
#if 0
        if (debug_compiled_env) {
            INDENT(0, "--debug_compiled_env--");
            log("%s " c_good("::") " %s", pair.second->str().c_str(),
                env.map[pair.first]->str().c_str());
        }
#endif
  }

#if 0
  try {
    check_instance_requirements(program->instances, env);
  } catch (user_error &e) {
    print_exception(e);
  }
#endif

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

typedef std::map<std::string,
                 std::map<types::Ref, Translation::ref, types::CompareType>>
    translation_map_t;

void specialize_core(const types::TypeEnv &type_env,
                     const defn_map_t &defn_map,
                     const types::Scheme::Map &typing,
                     const CtorIdMap &ctor_id_map,
                     const DataCtorsMap &data_ctors_map,
                     DefnId defn_id,
                     /* output */ translation_map_t &translation_map,
                     /* output */ NeededDefns &needed_defns) {
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
                         Translation::ref{});
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
    types::ClassPredicates class_predicates;
    Env env{typing /*map*/,
            nullptr /*return_type*/,
            {} /*tracked_types*/,
            ctor_id_map,
            data_ctors_map};

    // TODO: clean this up. it is ugly. we're accessing the base class
    // TranslationEnv's tracked_types
    auto tracked_types = std::make_shared<TrackedTypes>();
    env.tracked_types = tracked_types;

    const Decl *decl_to_check = defn_map.maybe_lookup(defn_id);
    if (decl_to_check == nullptr) {
      throw user_error(defn_id.id.location,
                       "could not find a definition for %s :: %s",
                       defn_id.id.str().c_str(), defn_id.scheme->str().c_str());
    }

    const Expr *to_check = decl_to_check->value;
    const std::string final_name = defn_id.id.name;

    /* wrap this expr in it's asserted type to ensure that it monomorphizes */
    auto as_defn = new As(to_check, defn_id.scheme, false);
    auto defn_to_check = Identifier{defn_id.repr_public(), defn_id.id.location};
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
      const bitter::Expr *expr;
      types::Ref type;
      std::tie(std::ref(expr), std::ref(type)) = pair;
    }
#endif

    TranslationEnv tenv{tracked_types, ctor_id_map, data_ctors_map};
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
  const Decl *program_main = phase_2.defn_map.lookup(
      {make_iid(phase_2.compilation->program_name + ".main"),
       program_main_scheme});

  NeededDefns needed_defns;
  DefnId main_defn{program_main->id, program_main_scheme};
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

struct CodeSymbol {
  std::string name;
  types::Ref type;
  const TrackedTypes &typing;
  const bitter::Expr *expr;
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
  types::Ref main_type = type_arrows(
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

  // Initialize the process
  auto llvm_zion_init_func_decl = llvm::cast<llvm::Function>(
      llvm_module->getOrInsertFunction(
          "zion_init",
          llvm::FunctionType::get(builder.getVoidTy(), false /*isVarArg*/)));
  builder.CreateCall(llvm_zion_init_func_decl);

  llvm::Value *llvm_main_closure = gen::get_env_var(
      builder, gen_env, make_iid(main_closure), main_type);

  llvm::Value *gep_path[] = {builder.getInt32(0), builder.getInt32(0)};
  llvm::Value *main_func = builder.CreateLoad(
      builder.CreateInBoundsGEP(llvm_main_closure, gep_path));
  llvm::Value *main_args[] = {
      llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo()),
      builder.CreateBitCast(llvm_main_closure,
                            builder.getInt8Ty()->getPointerTo(),
                            "main_closure")};
  builder.CreateCall(main_func, llvm::ArrayRef<llvm::Value *>(main_args));
  builder.CreateRet(builder.getInt32(0));
}

phase_4_t ssa_gen(llvm::LLVMContext &context, const phase_3_t &phase_3) {
  llvm::Module *llvm_module = new llvm::Module("program", context);
  llvm::DIBuilder *dbuilder = nullptr; // new llvm::DIBuilder(*module);
  llvm::IRBuilder<> builder(context);

  gen::gen_env_t gen_env;

  /* resolvers is the list of top-level symbols that need to be resolved. they
   * can be traversed in any order, and will automatically resolve in dependency
   * order based on a topological sorting. this could be a bit intense on the
   * stack, worst case is on the same order as the user's program stack depth.
   */
  std::list<std::shared_ptr<gen::Resolver>> resolvers;
  std::string output_filename;

  try {
    const std::unordered_set<std::string> globals = get_globals(phase_3);

    debug_above(6, log("globals are %s", join(globals).c_str()));
    debug_above(2, log("type_env is %s",
                       str(phase_3.phase_2.compilation->type_env).c_str()));
    for (auto pair : phase_3.translation_map) {
      for (auto &overload : pair.second) {
        const std::string &name = pair.first;
        const types::Ref &type = overload.first;
        Translation::ref translation = overload.second;

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

        std::shared_ptr<gen::Resolver> resolver = gen::lazy_resolver(
            name, type,
            [&builder, &llvm_module, name, translation, &phase_3, &gen_env,
             &globals](llvm::Value **llvm_value) -> gen::resolution_status_t {
              gen::Publishable publishable(llvm_value);
              /* we are resolving a global object, so we should not be inside of
               * a basic block. */
              builder.ClearInsertionPoint();
              llvm::IRBuilderBase::InsertPointGuard ipg(builder);

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

struct Job {
  std::string cmd;
  std::vector<std::string> opts;
  std::vector<std::string> args;
};

int run_job(const Job &job) {
  get_help = in_vector("-help", job.opts) || in_vector("--help", job.opts);
  debug_compiled_env = (getenv("SHOW_ENV") != nullptr) ||
                       in_vector("-show-env", job.opts);
  debug_types = (getenv("SHOW_TYPES") != nullptr) ||
                in_vector("-show-types", job.opts);
  debug_all_expr_types = (getenv("SHOW_EXPR_TYPES") != nullptr) ||
                         in_vector("-show-expr-types", job.opts);
  debug_all_translated_defns = (getenv("SHOW_DEFN_TYPES") != nullptr) ||
                               in_vector("-show-defn-types", job.opts);

  std::map<std::string, std::function<int(const Job &, bool)>> cmd_map;
  cmd_map["help"] = [&](const Job &job, bool explain) {
    std::cerr << "zion:" << std::endl;
    for (auto &cmd_pair : cmd_map) {
      if (cmd_pair.first != "help") {
        /* run the command in explain mode */
        std::cerr << "\t";
        // TODO: just have a different way of doing this... this is dumb.
        cmd_pair.second(job, true);
      }
    }
    std::cerr << "Also try looking at the manpage. man zion." << std::endl;
    return EXIT_FAILURE;
  };
  cmd_map["test"] = [&](const Job &job, bool explain) {
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
  cmd_map["find"] = [&](const Job &job, bool explain) {
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
  cmd_map["lex"] = [&](const Job &job, bool explain) {
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
    Token token;
    bool newline = false;
    while (lexer.get_token(token, newline, nullptr)) {
      log_location(token.location, "%s (%s)", token.text.c_str(),
                   tkstr(token.tk));
    }
    return EXIT_SUCCESS;
  };

  cmd_map["parse"] = [&](const Job &job, bool explain) {
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
        log_location(decl->id.location, "%s = %s", decl->id.str().c_str(),
                     decl->value->str().c_str());
      }
      for (auto type_class : compilation->program->type_classes) {
        log_location(type_class->id.location, "%s", type_class->str().c_str());
      }
      for (auto instance : compilation->program->instances) {
        log_location(instance->class_predicate->get_location(), "%s",
                     instance->str().c_str());
      }
      return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
  };
  cmd_map["compile"] = [&](const Job &job, bool explain) {
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
  cmd_map["specialize"] = [&](const Job &job, bool explain) {
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
  cmd_map["ll"] = [&](const Job &job, bool explain) {
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
  cmd_map["run"] = [&](const Job &job, bool explain) {
    if (explain) {
      std::cerr << "run: compiles, specializes, generates LLVM output, then "
                   "runs the generated binary"
                << std::endl;
      return EXIT_FAILURE;
    }

    if (getenv("ZION_RT") == nullptr) {
      log(log_error,
          "ZION_RT is not set. It should be set to the dirname of zion_rt.c. "
          "That is typically /usr/local/share/zion/runtime.");
      return EXIT_FAILURE;
    }

    llvm::LLVMContext context;
    phase_4_t phase_4 = ssa_gen(context, specialize(compile(job.args[0])));

    if (!user_error::errors_occurred()) {
      std::stringstream ss_c_flags;
      std::stringstream ss_lib_flags;
      for (auto link_in : phase_4.phase_3.phase_2.compilation->link_ins) {
        std::string pkg_name = unescape_json_quotes(link_in.name.text);
        switch (link_in.lit) {
        case lit_pkgconfig:
          ss_c_flags << get_pkg_config("--cflags-only-I", pkg_name) << " ";
          ss_lib_flags << get_pkg_config("--libs --static", pkg_name) << " ";
          break;
        }
      }

      auto command_line = string_format(
          // We are using clang to lower the code from LLVM, and link it
          // to the runtime.
          "clang "
          // Include any necessary include dirs for C dependencies.
          "%s "
          // Allow for the user to specify optimizations
          "${ZION_OPT_FLAGS} "
          // NB: we don't embed the target triple into the LL, so any
          // targeted triple causes an ugly error from clang, so I just
          // ignore it here.
          "-Wno-override-module "
          // TODO: plumb host targeting through clang here
          "--target=$(llvm-config --host-target) "
          // TODO: plumb zion_rt.c properly into installation location.
          // probably something like /usr/share/zion/rt
          "${ZION_RT}/zion_rt.c "
          // Add linker flags
          "%s "
          // Don't forget the built .ll file from our frontend here.
          "%s "
          // Give the binary a name.
          "-o %s",
          ss_c_flags.str().c_str(), phase_4.output_llvm_filename.c_str(),
          ss_lib_flags.str().c_str(),
          phase_4.phase_3.phase_2.compilation->program_name.c_str(),
          phase_4.phase_3.phase_2.compilation->program_name.c_str());
      debug_above(1, log("running %s", command_line.c_str()));
      if (std::system(command_line.c_str()) != 0) {
        throw user_error(INTERNAL_LOC(), "failed to compile binary");
      }

      return run_program(phase_4.phase_3.phase_2.compilation->program_name, {});
    } else {
      return EXIT_FAILURE;
    }
  };

  if (!in(job.cmd, cmd_map)) {
    Job new_job;
    new_job.args.insert(new_job.args.begin(), job.cmd);
    std::copy(job.args.begin(), job.args.end(),
              std::back_inserter(new_job.args));
    new_job.cmd = "run";
    return cmd_map["run"](new_job, false /*explain*/);
  } else {
    return cmd_map[job.cmd](job, get_help);
  }
}

int main(int argc, char *argv[]) {
  // signal(SIGINT, &handle_sigint);
  init_dbg();
  init_host();
  std::shared_ptr<logger> logger(std::make_shared<standard_logger>("", "."));

  Job job;
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
