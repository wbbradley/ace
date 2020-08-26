#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>

#include "ast.h"
#include "builtins.h"
#include "checked.h"
#include "class_predicate.h"
#include "compiler.h"
#include "context.h"
#include "disk.h"
#include "gen.h"
#include "graph.h"
#include "host.h"
#include "lexer.h"
#include "logger.h"
#include "logger_decls.h"
#include "solver.h"
#include "tarjan.h"
#include "tests.h"
#include "tld.h"
#include "translate.h"
#include "unification.h"

#define IMPL_SUFFIX "-impl"

const char *LOGO = C_UNCHECKED R"(

░░░░░░░ ░░  ░░░░░░  ░░░    ░░ 
   ░░░  ░░ ░░    ░░ ░░░░   ░░ 
  ░░░   ░░ ░░    ░░ ░░ ░░  ░░ 
 ░░░    ░░ ░░    ░░ ░░  ░░ ░░ 
░░░░░░░ ░░  ░░░░░░  ░░   ░░░░ 

)" C_RESET;

namespace zion {

using namespace ast;

namespace {

bool get_help = false;
bool fast_fail = true && (getenv("ZION_SHOW_ALL_ERRORS") == nullptr);
bool debug_compiled_env = getenv("SHOW_ENV") != nullptr;
bool debug_compile_step = getenv("SHOW_CC") != nullptr;
bool debug_specialized_env = getenv("SHOW_ENV2") != nullptr;
bool debug_types = getenv("SHOW_TYPES") != nullptr;
bool debug_all_expr_types = getenv("SHOW_EXPR_TYPES") != nullptr;
bool debug_all_translated_defns = getenv("SHOW_DEFN_TYPES") != nullptr;

int run_program(std::string executable, std::vector<std::string> args) {
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
    std::string executable_path = "./" + executable;
    std::vector<const char *> raw_args;
    raw_args.reserve(args.size() + 1);
    raw_args.push_back(executable_path.c_str());
    for (auto &arg : args) {
      raw_args.push_back(arg.c_str());
    }
    raw_args.push_back(nullptr);
    int ret = execvp(executable_path.c_str(),
                     const_cast<char **>(&raw_args[0]));
    if (ret == -1) {
      printf("failed to launch %s %s. quitting...\n", executable.c_str(),
             join(args, " ").c_str());
      return EXIT_FAILURE;
    }
  }

  return 0;
}

types::Map resolve_free_type_after_specialization_inference(
    const ast::Expr *expr,
    types::Ref type,
    const types::ClassPredicates &instance_requirements,
    const types::ClassPredicates &instance_predicates) {
  if (type->ftv_count() != 0) {
#ifdef ZION_DEBUG
    INDENT(2, "--resolve_free_type_after_specialization_inference--");
    debug_above(7, log_location(expr->get_location(), "rftasi ::: %s :: %s",
                                expr->str().c_str(), type->str().c_str()));
#endif
    const types::Ftvs ftvs = type->get_ftvs();
    const types::ClassPredicates referenced_predicates =
        types::get_overlapping_predicates(instance_requirements, ftvs,
                                          nullptr /*overlapping_ftvs*/);
    /* resolve overloads when they are ambiguous by looking at available
     * instances */
    debug_above(
        2, log_location(expr->get_location(),
                        "%s :: %s has free variables that are bound to "
                        "predicates {%s}. let's try to match this scheme to "
                        "a known type class instance...",
                        expr->str().c_str(), type->str().c_str(),
                        join_str(referenced_predicates, ", ").c_str()));

    /* just out of curiosity, see when there are more referenced
     * predicates in a single expression type */
    for (auto &referenced_predicate : referenced_predicates) {
      debug_above(2, log("we need to solve %s against {%s}",
                         referenced_predicate->str().c_str(),
                         join_str(instance_predicates, ", ").c_str()));

      types::Map bindings;
      types::ClassPredicates found_instances;
      for (auto &instance_predicate_ : instance_predicates) {
        /* let's freshen the instance_predicate */
        std::map<std::string, std::string> new_ftvs;
        for (auto &ftv : instance_predicate_->get_ftvs()) {
          new_ftvs[ftv] = gensym_name();
        }
        auto instance_predicate = instance_predicate_->remap_vars(new_ftvs);

        if (instance_predicate->classname.name ==
            referenced_predicate->classname.name) {
          /* we are referring to the same type class. now, let's unify the
           * instance parameters in an attempt to resolve any functional
           * dependencies between the associated types. */
          debug_above(3, log("Attempting to unify %s with %s",
                             instance_predicate->str().c_str(),
                             referenced_predicate->str().c_str()));
          types::Unification unification = types::unify_many(
              instance_predicate->params, referenced_predicate->params);
          if (unification.result) {
            debug_above(3, log("%s unified with %s with bindings %s",
                               instance_predicate->str().c_str(),
                               referenced_predicate->str().c_str(),
                               str(unification.bindings).c_str()));
            /* unification was successful. now check whether we've already
             * found a matching instance or not */
            /* this is the only instance which unifies so far. that's good */
            found_instances.insert(instance_predicate);
            std::swap(bindings, unification.bindings);
          }
        }
      }

      if (found_instances.size() == 1) {
        assert(bindings.size() != 0);

        /* this is good, it means that we found a single type class
         * instance for this type class that satisfies our requirements. but, it
         * may not help. the class instance may be more general than necessary
         * to help us resolve our free variable. let's check. */

        for (auto ftv : ftvs) {
          if (in(ftv, bindings)) {
            /* Hooray! This set of bindings actually helped remap us from one of
             * our free variables to something probably more concrete. */
            return bindings;
          } else {
            /* meh, this is more general, and not solving the problem */
          }
        }
      }
    }
  }

  /* nothing to do */
  return {};
}

void tracked_types_have_ftvs(const TrackedTypes &tracked_types,
                             types::Ftvs &ftvs) {
  for (auto pair : tracked_types) {
    for (auto ftv : pair.second->get_ftvs()) {
      ftvs.insert(ftv);
    }
  }
}

CheckedDefinitionRef check_decl(
    const bool check_constraint_coverage,
    const DataCtorsMap &data_ctors_map,
    const types::ClassPredicates &instance_predicates,
    const Identifier id,
    const Decl *decl,
    const types::Ref expected_type,
    const types::SchemeResolver &scheme_resolver) {
  const Expr *expr = decl->value;
  TrackedTypes tracked_types;
  types::Constraints constraints;
  types::ClassPredicates instance_requirements;

  debug_above(
      4, log("type checking %s = %s", id.str().c_str(), expr->str().c_str()));
  types::Ref ty = infer(expr, data_ctors_map, nullptr /*return_type*/,
                        scheme_resolver, tracked_types, constraints,
                        instance_requirements);
  append_to_constraints(constraints, ty, expected_type,
                        make_context(expected_type->get_location(),
                                     "declaration %s has its expected type",
                                     id.str().c_str()));
  types::Map bindings = zion::solver(
      check_constraint_coverage,
      make_context(id.location, "solving %s :: %s", id.name.c_str(),
                   ty->str().c_str()),
      constraints, tracked_types, scheme_resolver, instance_requirements);

  ty = ty->rebind(bindings);
  instance_requirements = types::rebind(instance_requirements, bindings);
  types::SchemeRef scheme = ty->generalize(instance_requirements)->normalize();

  if (instance_predicates.size() != 0) {
    types::Ftvs last_seen_ftvs;
    while (true) {
      types::Ftvs ftvs;
      tracked_types_have_ftvs(tracked_types, ftvs);
      if (ftvs == last_seen_ftvs) {
        /* we're not making progress anymore */
        break;
      } else {
        last_seen_ftvs = ftvs;
      }

      for (auto pair : tracked_types) {
        const ast::Expr *expr = pair.first;
        const types::Ref &type = pair.second;

        types::Map bindings = resolve_free_type_after_specialization_inference(
            expr, type, instance_requirements, instance_predicates);

        if (bindings.size() != 0) {
          rebind_tracked_types(tracked_types, bindings);
          instance_requirements = types::rebind(instance_requirements,
                                                bindings);
        }
      }
    }

    /* do one final check */
    for (auto pair : tracked_types) {
      if (pair.second->ftv_count() != 0) {
        // TODO: normalize the types and predicates in here for cleanliness in
        // errors
        auto expr = pair.first;
        auto type = pair.second;
        const types::ClassPredicates referenced_predicates =
            types::get_overlapping_predicates(instance_requirements,
                                              type->get_ftvs(),
                                              nullptr /*overlapping_ftvs*/);
        if (referenced_predicates.size() != 0) {
          auto error = user_error(
              expr->get_location(),
              "type inference was unable to resolve a fully bound type "
              "for %s :: %s",
              expr->str().c_str(), type->str().c_str());
          error.add_info(type->get_location(),
                         "this type must conform to the following predicate%s",
                         referenced_predicates.size() == 1 ? "" : "s");
          for (auto &referenced_predicate : referenced_predicates) {
            error.add_info(referenced_predicate->get_location(), "%s",
                           referenced_predicate->str().c_str());
          }
          error.add_info(INTERNAL_LOC(), "amongst all these instances:");
          for (auto &predicate : instance_predicates) {
            error.add_info(predicate->get_location(), "%s",
                           predicate->str().c_str());
          }
          throw error;
        }
      }
    }
  }

  return std::make_shared<CheckedDefinition>(scheme, decl, tracked_types);
}

void initialize_builtin_schemes(types::SchemeResolver &scheme_resolver) {
  for (auto pair : get_builtins()) {
    scheme_resolver.insert_scheme(pair.first, pair.second);
  }
}

std::map<std::string, const TypeClass *> check_type_classes(
    const std::vector<const TypeClass *> &type_classes,
    types::SchemeResolver &scheme_resolver) {
  std::map<std::string, const TypeClass *> type_class_map;

  /* introduce all the type class signatures into the scheme_resolver, and build
   * up an index of type_class names */
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
        if (scheme_resolver.scheme_exists(pair.first)) {
          auto error = user_error(pair.second->get_location(),
                                  "the name " c_id("%s") " is already in use",
                                  pair.first.c_str());
          error.add_info(INTERNAL_LOC(), "TODO: get better first decl loc");
          throw error;
        }

        types::SchemeRef scheme = pair.second->remap_vars(remapping)
                                      ->generalize(predicates);

        scheme_resolver.insert_scheme(pair.first, scheme);
      }
    } catch (user_error &e) {
      print_exception(e);
      /* and continue */
    }
  }
  return type_class_map;
}

tarjan::Graph build_program_graph(const std::vector<const Decl *> &decls) {
  tarjan::Graph graph;
  for (auto decl : decls) {
    const auto &name = decl->id.name;
    const auto free_vars = get_free_vars(decl->value, {});
    for (auto free_var : free_vars) {
      if (!zion::tld::is_fqn(free_var)) {
        throw user_error(decl->id.location,
                         "found free_var \"%s\" that is not fully qualified "
                         "within %s (TODO: extract free variable locations, "
                         "not just the containing function's location)",
                         free_var.c_str(),
                         zion::tld::strip_prefix(name).c_str());
      }
    }
    graph.insert({name, free_vars});
  }
  return graph;
}

CheckedDefinitionsByName check_decls(std::string user_program_name,
                                     std::string entry_point_name,
                                     const std::vector<const Decl *> &decls,
                                     const DataCtorsMap &data_ctors_map,
                                     types::SchemeResolver &scheme_resolver,
                                     bool emit_graph_dot) {
  std::unordered_map<std::string, const Decl *> decl_map;
  for (auto decl : decls) {
    debug_above(5,
                log("adding decl named %s to decl_map", decl->id.name.c_str()));
    decl_map.insert({decl->id.name, decl});
  }

  tarjan::Graph graph = build_program_graph(decls);
  tarjan::SCCs sccs = tarjan::compute_strongly_connected_components(graph);
  debug_above(5, log("found program ordering %s", str(sccs).c_str()));
  if (emit_graph_dot) {
    auto dot_file = user_program_name + ".dot";
    zion::graph::emit_graphviz_dot(graph, sccs, entry_point_name, dot_file);
    auto png_file = dot_file + ".png";
    system(("dot " + dot_file + " -Tpng -Gdpi=300 -o " + png_file).c_str());
    ui::open_file(png_file);
  }

  CheckedDefinitionsByName checked_defns;
  for (auto &scc : sccs) {
    /* we are looking at a strongly coupled (aka mutually recursive) set of
     * functions or expressions. let's run inference on them all at once. */
    types::SchemeResolver local_scheme_resolver(&scheme_resolver);

    types::Map map;
    /* seed the SCC with a local type scheme */
    for (auto name : scc) {
      if (decl_map.count(name) != 0) {
        map[name] = type_variable(INTERNAL_LOC());
        local_scheme_resolver.insert_scheme(name, scheme({}, {}, map[name]));
      } else {
#ifdef ZION_DEBUG
        if (debug_level() > 2) {
          log("found a reference to " c_id("%s") " in SCCs that has no decl",
              name.c_str());
          std::set<Identifier> candidates;
          auto scheme = scheme_resolver.lookup_scheme(
              Identifier{name, INTERNAL_LOC()}, candidates);
          if (scheme != nullptr) {
            log("looked up scheme %s :: %s", name.c_str(),
                scheme->str().c_str());
          } else {
            log("no scheme existed for %s", name.c_str());
          }
        }
#endif
      }
    }

    TrackedTypes tracked_types;
    types::Constraints constraints;
    types::ClassPredicates instance_requirements;

    for (auto name : scc) {
      if (decl_map.count(name) != 0) {
        auto ty = infer(decl_map.at(name)->value, data_ctors_map,
                        nullptr /*return_type*/, local_scheme_resolver,
                        tracked_types, constraints, instance_requirements);
        if (name == entry_point_name) {
          append_to_constraints(
              constraints, ty,
              type_arrow(INTERNAL_LOC(),
                         type_params({type_unit(INTERNAL_LOC())}),
                         type_unit(INTERNAL_LOC())),
              make_context(INTERNAL_LOC(),
                           "main function must have signature fn () ()"));
        }

        append_to_constraints(
            constraints, ty, map[name],
            make_context(INTERNAL_LOC(), "scc checks should match inference"));
      }
      debug_above(2, log("inferred types %s", str(map).c_str()));
    }

    if (debug_all_expr_types) {
      INDENT(0, "--debug_all_expr_types--");
      log("All Expression Types in {%s}", join(scc, ", ").c_str());
      for (auto pair : tracked_types) {
        log_location(pair.first->get_location(), "%s :: %s",
                     pair.first->str().c_str(), pair.second->str().c_str());
      }
      log("All Constraints for {%s}", join(scc, ", ").c_str());
      log("%s", str(constraints).c_str());
    }

    types::Map bindings = zion::solver(false /*check_constraint_coverage*/,
                                       make_context(INTERNAL_LOC(), "solving"),
                                       constraints, tracked_types,
                                       scheme_resolver, instance_requirements);

    rebind_tracked_types(tracked_types, bindings);
#ifdef ZION_DEBUG
    if (debug_all_expr_types) {
      log("Rebound Expression Types for {%s}", join(scc, ", ").c_str());
      for (auto pair : tracked_types) {
        log_location(pair.first->get_location(), "%s :: %s",
                     pair.first->str().c_str(),
                     pair.second->generalize({})->str().c_str());
      }
    }
#endif
    for (auto pair : map) {
      auto scheme = pair.second->rebind(bindings)->generalize(
          types::rebind(instance_requirements, bindings));
      // NB: do not normalize the scheme
      debug_above(1, log("resolved %s to scheme %s", pair.first.c_str(),
                         scheme->normalize()->str().c_str()));
      scheme_resolver.insert_scheme(pair.first, scheme);
      // TODO: consider altering CheckedDefinition to have a type, not a scheme
      CheckedDefinitionRef checked_definition =
          std::make_shared<const CheckedDefinition>(
              scheme, decl_map[pair.first], tracked_types);
      checked_defns.insert(
          {pair.first, std::list<CheckedDefinitionRef>{checked_definition}});
    }
  }

  return checked_defns;
}

} // namespace

Identifier make_instance_decl_id(const Instance *instance, Identifier decl_id) {
  return Identifier{string_format("(%s) => %s",
                                  instance->class_predicate->repr().c_str(),
                                  decl_id.name.c_str()),
                    decl_id.location};
}

const Decl *find_overload_for_instance(std::string name,
                                       Location location,
                                       const TypeClass *type_class,
                                       const Instance *instance) {
  /* type class defaults are plumbed down here so that if this instance does not
   * override a particular symbol, we can fall back about trying the "default"
   * impl.
   */
  for (const Decl *decl : instance->decls) {
    assert(tld::is_fqn(name));
    if (decl->id.name == name) {
      return decl;
    }
  }
  for (const Decl *decl : type_class->default_decls) {
    assert(tld::is_fqn(name));
    if (decl->id.name == name) {
      return decl;
    }
  }
  throw user_error(location, "could not find decl for %s in instance %s",
                   zion::tld::strip_prefix(name).c_str(),
                   instance->class_predicate->str().c_str());
}

void check_instance_for_type_class_overload(
    std::string name,
    types::Ref type,
    const TypeClass *type_class,
    const Instance *instance,
    const types::Map &subst,
    const DataCtorsMap &data_ctors_map,
    std::set<std::string> &names_checked,
    types::SchemeResolver &scheme_resolver,
    const types::ClassPredicates &class_predicates,
    CheckedDefinitionsByName &checked_defns) {
  const Decl *source_decl = find_overload_for_instance(
      name, type->get_location(), type_class, instance);

  if (in(name, names_checked)) {
    throw user_error(source_decl->get_location(),
                     "name %s already duplicated in this instance",
                     source_decl->id.str().c_str());
  }
  names_checked.insert(source_decl->id.name);

  types::Ref expected_type = type->rebind(subst);
  types::SchemeRef expected_scheme = expected_type->generalize(
      class_predicates);

  CheckedDefinitionRef checked_defn = check_decl(
      false /*check_constraint_coverage*/, data_ctors_map, {}, source_decl->id,
      source_decl, expected_type, scheme_resolver);
  const auto &resolved_scheme = checked_defn->scheme;
  const auto &decl = checked_defn->decl;

  debug_above(3, log_location(decl->id.location, "adding %s to env as %s",
                              decl->id.str().c_str(),
                              resolved_scheme->str().c_str()));
  checked_defns[decl->id.name].push_back(checked_defn);

  debug_above(3, log_location(decl->id.location,
                              "checked the instance %s :: %s. we expected %s",
                              decl->id.str().c_str(),
                              resolved_scheme->str().c_str(),
                              expected_scheme->str().c_str()));

  /* check whether the resolved scheme matches the expected scheme */
  if (!scheme_equality(resolved_scheme, expected_scheme)) {
    auto error = user_error(decl->id.location,
                            "instance component %s appears to be more "
                            "constrained than the type class",
                            decl->id.str().c_str());
    error.add_info(decl->id.location,
                   "instance component declaration has scheme %s",
                   resolved_scheme->normalize()->str().c_str());
    error.add_info(type->get_location(),
                   "type class component declaration has scheme %s",
                   expected_scheme->str().c_str());
    throw error;
  }
}

/* typecheck an instance for whether it properly overloads the type class with
 * which it is associated */
void check_instance_for_type_class_overloads(
    const Instance *instance,
    const TypeClass *type_class,
    const DataCtorsMap &data_ctors_map,
    std::vector<const Decl *> &instance_decls,
    types::SchemeResolver &scheme_resolver,
    CheckedDefinitionsByName &checked_defns) {
  if (instance->class_predicate->params.size() !=
      type_class->type_var_ids.size()) {
    throw user_error(instance->get_location(),
                     "the number of params on %s does not equal "
                     "the number of type variables on %s",
                     instance->str().c_str(), type_class->str().c_str());
  }

  /* make a template for the types that the instance implementation should
   * conform to */
  types::Map subst;
  int i = 0;

  for (auto &type_var_id : type_class->type_var_ids) {
    subst[type_var_id.name] = instance->class_predicate->params[i++];
  }

  /* check whether this instance properly implements the given type class */
  std::set<std::string> names_checked;

  for (auto pair : type_class->overloads) {
    auto name = pair.first;
    auto type = pair.second;
    check_instance_for_type_class_overload(
        name, type, type_class, instance, subst, data_ctors_map, names_checked,
        scheme_resolver,
        type_class->class_predicates /*, type_class->defaults*/, checked_defns);
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

void check_instances(
    const std::vector<const Instance *> &instances,
    const std::map<std::string, const TypeClass *> &type_class_map,
    const DataCtorsMap &data_ctors_map,
    /* out */ types::SchemeResolver &scheme_resolver,
    /* out */ CheckedDefinitionsByName &checked_defns,
    /* out */ types::ClassPredicates &instance_predicates) {
  std::vector<const Decl *> instance_decls;

  for (const Instance *instance : instances) {
    try {
      debug_above(4, log("Checking whether we have a type class for %s",
                         instance->class_predicate->str().c_str()));

      const TypeClass *type_class = get(
          type_class_map, instance->class_predicate->classname.name,
          static_cast<const TypeClass *>(nullptr));

      if (type_class == nullptr) {
        /* Error Handling */
        auto error = user_error(instance->class_predicate->get_location(),
                                "could not find type class for instance %s",
                                instance->class_predicate->str().c_str());
        auto leaf_name = zion::tld::split_fqn(
                             instance->class_predicate->classname.name)
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
      check_instance_for_type_class_overloads(instance, type_class,
                                              data_ctors_map, instance_decls,
                                              scheme_resolver, checked_defns);

      debug_above(
          3, log("adding predicate %s to the set of all instance predicates",
                 instance->class_predicate->str().c_str()));
      instance_predicates.insert(instance->class_predicate);
    } catch (user_error &e) {
      e.add_info(instance->class_predicate->get_location(),
                 "while checking instance %s",
                 instance->class_predicate->str().c_str());
      print_exception(e);
    }
  }
}

CheckedDefinitionRef specialize_checked_defn(
    const DataCtorsMap &data_ctors_map,
    const types::SchemeResolver &scheme_resolver,
    const CheckedDefinitionsByName &checked_defns,
    const types::ClassPredicates &instance_predicates,
    const Location location,
    const std::string name,
    const types::Ref &type) {
  if (checked_defns.count(name) == 0) {
    throw user_error(
        location,
        "unknown symbol '" c_id("%s") "' requested for specialization ",
        name.c_str())
        .add_info(type->get_location(), "requested type is %s",
                  type->str().c_str())
        .add_info(location, "are you sure you have defined this function?");
  }

  types::Ref decl_type;
  CheckedDefinitionRef checked_defn_to_specialize;
  types::Map bindings;

  for (auto checked_defn : checked_defns.at(name)) {
    /* we have to loop over all possible overloads to ensure that only one
     * unifies */
    types::Unification unification = unify(checked_defn->scheme->type, type);
    if (unification.result) {
      /* multiple overloads exist that match this name and type */
      if (checked_defn_to_specialize != nullptr) {
        throw user_error(checked_defn_to_specialize->decl->get_location(),
                         "found ambiguous instance method")
            .add_info(location, "while looking for " c_id("%s") " :: %s",
                      name.c_str(), type->str().c_str())
            .add_info(checked_defn->get_location(),
                      "also matches (with scheme %s)",
                      checked_defn->scheme->normalize()->str().c_str());
      }
      bindings.swap(unification.bindings);
      checked_defn_to_specialize = checked_defn;
      decl_type = type->rebind(unification.bindings);
      assert(decl_type->ftv_count() == 0);
    }
  }

  if (checked_defn_to_specialize == nullptr) {
    throw user_error(location,
                     "could not find a definition for " c_id("%s") " :: %s",
                     zion::tld::strip_prefix(name).c_str(), type->str().c_str());
  }

  INDENT(1, string_format("specializing checked definition %s :: %s",
                          checked_defn_to_specialize->decl->id.str().c_str(),
                          decl_type->str().c_str(), str(bindings).c_str()));

  return check_decl(false /*check_constraint_coverage*/, data_ctors_map,
                    instance_predicates, checked_defn_to_specialize->decl->id,
                    checked_defn_to_specialize->decl, decl_type,
                    scheme_resolver);
}

struct Phase2 {
  explicit Phase2(const std::shared_ptr<Compilation const> &compilation,
                  const std::shared_ptr<types::SchemeResolver> &scheme_resolver,
                  const CheckedDefinitionsByName &&checked_defns,
                  const types::ClassPredicates &instance_predicates,
                  const DataCtorsMap &data_ctors_map)
      : compilation(compilation), scheme_resolver(scheme_resolver),
        checked_defns(std::move(checked_defns)),
        instance_predicates(instance_predicates),
        data_ctors_map(data_ctors_map) {
  }

  const std::shared_ptr<Compilation const> compilation;
  const std::shared_ptr<types::SchemeResolver> scheme_resolver;
  const CheckedDefinitionsByName checked_defns;
  const types::ClassPredicates instance_predicates;
  const DataCtorsMap data_ctors_map;

  std::ostream &dump(std::ostream &os) {
    for (auto pair : checked_defns) {
      const std::string &name = pair.first;
      const std::list<CheckedDefinitionRef> &checked_defns_list = pair.second;
      for (auto &checked_defn : checked_defns_list) {
        os << name << " :: " << checked_defn->scheme->str() << " = "
           << checked_defn->decl->str() << std::endl;
      }
    }
    return os;
  }
};

static int get_builtin_arity(const types::Refs &terms) {
  return terms.size() - 1;
}

std::map<std::string, int> get_builtin_arities() {
  const types::Scheme::Map &map = get_builtins();
  std::map<std::string, int> builtin_arities;
  for (auto pair : map) {
    types::Refs terms = unfold_arrows(pair.second->type);
    builtin_arities[pair.first] = get_builtin_arity(terms);
  }

  debug_above(
      7,
      log("builtin_arities are %s",
          join_with(builtin_arities, ", ", [](std::pair<std::string, int> a) {
            return string_format("%s: %d", a.first.c_str(), a.second);
          }).c_str()));
  return builtin_arities;
}

Phase2 compile(std::string user_program_name_, bool emit_graph_dot) {
  auto builtin_arities = get_builtin_arities();
  auto compilation = compiler::parse_program(user_program_name_,
                                             builtin_arities);
  if (compilation == nullptr) {
    exit(EXIT_FAILURE);
  }

  const Program *program = compilation->program;

  auto scheme_resolver_ptr = std::make_shared<types::SchemeResolver>();
  auto &scheme_resolver = *scheme_resolver_ptr;

  /* initialize the scheme_resolver with builtins */
  initialize_builtin_schemes(scheme_resolver);

  /* initialize the scheme_resolver with type class decls */
  auto type_class_map = check_type_classes(program->type_classes,
                                           scheme_resolver);
  /* start resolving more schemes */
  CheckedDefinitionsByName checked_defns = check_decls(
      user_program_name_, zion::tld::mktld(compilation->program_name, "main"),
      program->decls, compilation->data_ctors_map, scheme_resolver,
      emit_graph_dot);

  types::ClassPredicates instance_predicates;
  check_instances(program->instances, type_class_map,
                  compilation->data_ctors_map, scheme_resolver, checked_defns,
                  instance_predicates);

  return Phase2{compilation, scheme_resolver_ptr, std::move(checked_defns),
                instance_predicates, compilation->data_ctors_map};
}

typedef std::map<std::string,
                 std::map<types::Ref, Translation::ref, types::CompareType>>
    TranslationMap;

void specialize_core(const types::TypeEnv &type_env,
                     const CheckedDefinitionsByName &checked_defns,
                     const types::ClassPredicates &instance_predicates,
                     const types::SchemeResolver &scheme_resolver,
                     const DataCtorsMap &data_ctors_map,
                     types::DefnId defn_id_to_match,
                     /* output */ TranslationMap &translation_map,
                     /* output */ types::NeededDefns &needed_defns) {
  debug_above(2, log("specialize_core %s", defn_id_to_match.str().c_str()));
  if (starts_with(defn_id_to_match.id.name, "__builtin_")) {
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
  types::Ref type = defn_id_to_match.type;
  if (!type->get_ftvs().empty()) {
    throw user_error(defn_id_to_match.get_location(),
                     "unable to monomorphize %s because it still has free "
                     "type variables %s",
                     defn_id_to_match.str().c_str(),
                     str(type->get_ftvs()).c_str());
  }
  /* see whether we've already specialized this decl */
  auto translation = get(translation_map, defn_id_to_match.id.name, type,
                         Translation::ref{});
  if (translation != nullptr) {
    debug_above(6, log("we have already specialized %s. it is %s",
                       defn_id_to_match.str().c_str(),
                       translation->str().c_str()));
    return;
  }

  debug_above(7, log(c_good("Specializing subprogram %s"),
                     defn_id_to_match.str().c_str()));

  /* start the process of specializing our decl */
  /* get the decl and its tracked types so that we can rebind them and translate
   * a new decl */
  CheckedDefinitionRef checked_defn = specialize_checked_defn(
      data_ctors_map, scheme_resolver, checked_defns, instance_predicates,
      defn_id_to_match.id.location, defn_id_to_match.id.name,
      defn_id_to_match.type);

  debug_above(1, log("found defn for %s in decl %s",
                     defn_id_to_match.str().c_str(),
                     checked_defn->decl->str().c_str()));

  /* now we know the resulting monomorphic type for the value we want to
   * instantiate */
  const auto defn_type = checked_defn->scheme->type;
  const auto &decl = checked_defn->decl;
  const auto &tracked_types = checked_defn->tracked_types;

  const types::DefnId defn_id{defn_id_to_match.id, defn_type};

  assert(type_equality(defn_type, type));

  try {
    /* ... like a GRAY mark in the visited set... */
    translation_map[defn_id.id.name][type] = nullptr;

    const Expr *to_check = decl->value;
    const std::string final_name = defn_id.id.name;

    /* wrap this expr in it's asserted type to ensure that it monomorphizes */
    debug_above(3, log_location(defn_id.id.location, "hey, checking %s",
                                to_check->str().c_str()));
    if (debug_specialized_env) {
      for (auto pair : tracked_types) {
        log_location(pair.first->get_location(), "%s :: %s",
                     pair.first->str().c_str(), pair.second->str().c_str());
      }
    }

    std::unordered_set<std::string> bound_vars;
    INDENT(1, string_format("----------- specialize %s ------------",
                            defn_id.str().c_str()));
#ifdef ZION_DEBUG
    for (auto pair : tracked_types) {
      const ast::Expr *expr;
      types::Ref type;
      std::tie(expr, type) = pair;
      debug_above(
          1, log("spec %s :: %s", expr->str().c_str(), type->str().c_str()));
    }
#endif

    bool returns = true;
    auto translated_decl = translate_expr(defn_id, to_check, data_ctors_map,
                                          bound_vars, tracked_types, type_env,
                                          needed_defns, returns);

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

struct Phase3 {
  Phase2 phase_2;
  TranslationMap translation_map;

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

Phase3 specialize(const Phase2 &phase_2) {
  if (user_error::errors_occurred()) {
    throw user_error(INTERNAL_LOC(), "quitting");
  }
  std::string entry_point_name = zion::tld::mktld(
      phase_2.compilation->program_name, "main");
  if (phase_2.checked_defns.count(entry_point_name) == 0) {
    auto error = user_error(
        Location{phase_2.compilation->program_filename, 1, 1},
        "could not find a definition for %s",
        zion::tld::strip_prefix(entry_point_name).c_str());
    for (auto pair : phase_2.checked_defns) {
      if (pair.first.find(entry_point_name) != std::string::npos) {
        for (auto checked_def : pair.second) {
          error.add_info(checked_def->get_location(),
                         "perhaps you meant %s : %s?", pair.first.c_str(),
                         checked_def->scheme->str().c_str());
        }
      }
    }
    throw error;
  }

  CheckedDefinitionRef checked_defn_main =
      phase_2.checked_defns.at(entry_point_name).back();
  const Decl *program_main = checked_defn_main->decl;
  types::Ref program_type = checked_defn_main->scheme->type;

  types::NeededDefns needed_defns;
  types::DefnId main_defn{program_main->id, program_type};
  insert_needed_defn(needed_defns, main_defn, INTERNAL_LOC(), main_defn);

  CheckedDefinitionsByName checked_defns = phase_2.checked_defns;
  TranslationMap translation_map;
  while (needed_defns.size() != 0) {
    auto next_defn_id = needed_defns.begin()->first;
    try {
      specialize_core(phase_2.compilation->type_env, checked_defns,
                      phase_2.instance_predicates, *phase_2.scheme_resolver,
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
  return Phase3{phase_2, translation_map};
}

struct Phase4 {
  Phase4(const Phase4 &) = delete;
  Phase4(Phase3 phase_3,
         gen::GenEnv &&gen_env,
         llvm::Module *llvm_module,
         std::string output_llvm_filename)
      : phase_3(phase_3), gen_env(std::move(gen_env)), llvm_module(llvm_module),
        output_llvm_filename(output_llvm_filename) {
  }
  Phase4(Phase4 &&rhs)
      : phase_3(rhs.phase_3), gen_env(std::move(rhs.gen_env)),
        llvm_module(rhs.llvm_module),
        output_llvm_filename(rhs.output_llvm_filename) {
    rhs.llvm_module = nullptr;
  }
  ~Phase4() {
    delete llvm_module;
    // FUTURE: unlink(output_llvm_filename.c_str());
  }

  Phase3 phase_3;
  gen::GenEnv gen_env;
  llvm::Module *llvm_module = nullptr;
  std::string output_llvm_filename;

  std::ostream &dump(std::ostream &os) {
    return os << llvm_print_module(*llvm_module);
  }
};

struct CodeSymbol {
  std::string name;
  types::Ref type;
  const TrackedTypes &typing;
  const ast::Expr *expr;
};

std::unordered_set<std::string> get_globals(const Phase3 &phase_3) {
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

llvm::Function *build_main_function(llvm::IRBuilder<> &builder,
                                    llvm::Module *llvm_module,
                                    const gen::GenEnv &gen_env,
                                    std::string program_name) {
  llvm::Function *llvm_function = llvm::Function::Create(
      llvm_main_function_type(builder), llvm::Function::ExternalLinkage, "main",
      llvm_module);

  llvm_function->setDoesNotThrow();

  llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(
      builder.getContext(), PROGRAM_ENTRY_BLOCK, llvm_function);
  llvm::BasicBlock *main_block = llvm::BasicBlock::Create(
      builder.getContext(), MAIN_PROGRAM_BLOCK, llvm_function);

  builder.SetInsertPoint(entry_block);

  // Initialize the process
  auto llvm_zion_init_func_decl = llvm::cast<llvm::Function>(
      llvm_module
          ->getOrInsertFunction("zion_init", llvm_main_function_type(builder))
          .getCallee());
  auto arg_iter = llvm_function->args().begin();
  llvm::Value *llvm_argc = *&arg_iter;
  ++arg_iter;
  llvm::Value *llvm_argv = *&arg_iter;
  std::vector<llvm::Value *> zion_main_args{llvm_argc, llvm_argv};
  builder.CreateCall(llvm_zion_init_func_decl,
                     llvm::ArrayRef<llvm::Value *>(zion_main_args));

  llvm::IRBuilderBase::InsertPointGuard ipg(builder);

  builder.CreateBr(main_block);
  return llvm_function;
}

static types::Ref main_closure_type() {
  static types::Ref main_type = type_arrows(
      {type_unit(INTERNAL_LOC()), type_unit(INTERNAL_LOC())});
  return main_type;
}

void write_main_block(llvm::IRBuilder<> &builder,
                      llvm::Module *llvm_module,
                      const gen::GenEnv &gen_env,
                      std::string main_closure,
                      llvm::Function *llvm_function) {
  builder.SetInsertPoint(
      zion::llvm_find_block_by_name(llvm_function, MAIN_PROGRAM_BLOCK));

  llvm::Value *llvm_main_closure = gen::get_env_var(
      builder, gen_env, make_iid(main_closure), main_closure_type());

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

Phase4 ssa_gen(llvm::LLVMContext &context, const Phase3 &phase_3) {
  llvm::Module *llvm_module = new llvm::Module("program", context);
  llvm::IRBuilder<> builder(context);

  gen::GenEnv gen_env;
  std::string output_filename;

  try {
    const std::unordered_set<std::string> globals = get_globals(phase_3);
    const std::string program_name = phase_3.phase_2.compilation->program_name;
    llvm::IRBuilder<> builder(context);
    llvm::Function *llvm_main_function = build_main_function(
        builder, llvm_module, gen_env, program_name);

    const std::string main_closure = zion::tld::mktld(program_name, "main");

    debug_above(6, log("globals are %s", join(globals).c_str()));
    debug_above(2, log("type_env is %s",
                       str(phase_3.phase_2.compilation->type_env).c_str()));
    for (auto pair : phase_3.translation_map) {
      for (auto &overload : pair.second) {
        const std::string &name = pair.first;
        const types::Ref &type = overload.first;
        Translation::ref translation = overload.second;
        if (translation == nullptr) {
          log("how did we get here in ssa_gen with null translation for %s :: "
              "%s",
              name.c_str(), type->str().c_str());
          assert(false);
        }

        debug_above(4, log("fetching %s expression type from translated types",
                           name.c_str()));
        debug_above(
            4,
            log("%s should be the same type as %s",
                get(translation->typing, translation->expr, {})->str().c_str(),
                type->str().c_str()));
        assert_type_equality(get(translation->typing, translation->expr, {}),
                             type);

        /* at this point we should not have a resolver or a declaration or
         * definition or anything for this symbol */
#ifdef ZION_DEBUG
        llvm::Value *value = gen::maybe_get_env_var(
            builder, gen_env,
            Identifier{pair.first, overload.second->expr->get_location()},
            type);
        assert(value == nullptr);
#endif

        debug_above(4, log("making a placeholder proxy value for %s :: %s = %s",
                           pair.first.c_str(), type->str().c_str(),
                           translation->expr->str().c_str()));

        std::shared_ptr<gen::Resolver> resolver = gen::lazy_resolver(
            name, type,
            [&builder, &llvm_module, name, translation, &phase_3, &gen_env,
             &globals, llvm_main_function](
                llvm::Value **llvm_value) -> gen::ResolutionStatus {
              gen::Publishable publishable(name, llvm_value);
              /* we are resolving a global object, so we should not be inside
               * of a basic block. */
              llvm::IRBuilderBase::InsertPointGuard ipg(builder);
              llvm::Instruction *llvm_entry_terminator =
                  llvm_find_block_by_name(llvm_main_function,
                                          PROGRAM_ENTRY_BLOCK)
                      ->getTerminator();
              assert(llvm_entry_terminator);
              builder.SetInsertPoint(llvm_entry_terminator);

              switch (gen::gen(
                  name, builder, llvm_module, nullptr /*defer_guard*/,
                  nullptr /*break_to_block*/, nullptr /*continue_to_block*/,
                  translation->expr, translation->typing,
                  phase_3.phase_2.compilation->type_env, gen_env, {}, globals,
                  &publishable)) {
              case gen::rs_resolve_again:
                return gen::rs_resolve_again;
              case gen::rs_cache_global_load:
                throw user_error(INTERNAL_LOC(), "wtf");
              case gen::rs_cache_resolution:
                debug_above(4, log("resolving %s as %s", name.c_str(),
                                   llvm_print(*llvm_value).c_str()));
                if (auto llvm_global_value = llvm::dyn_cast<llvm::GlobalValue>(
                        (*llvm_value))) {
                  /* this is already global */
                  return gen::rs_cache_resolution;
                } else if (auto llvm_constant = llvm::dyn_cast<llvm::Constant>(
                               (*llvm_value))) {
                  /* just a constant, no need to load again */
                  return gen::rs_cache_resolution;
                } else {
                  if (auto llvm_inst = llvm::dyn_cast<llvm::Instruction>(
                          (*llvm_value))) {
                    if (llvm_inst->getParent()->getParent() ==
                        llvm_main_function) {
                      debug_above(
                          13,
                          log("current function is %s",
                              llvm_print_function(llvm_main_function).c_str()));
                      llvm::GlobalVariable *llvm_global = llvm_get_global(
                          llvm_module, name,
                          llvm_get_zero_value((*llvm_value)->getType()),
                          false /*is_constant*/);

                      /* initialize the global */
                      builder.CreateStore((*llvm_value), llvm_global);

                      *llvm_value = llvm_global;
                      return gen::rs_cache_global_load;
                    } else {
                      dbg();
                    }
                  } else {
                    dbg();
                  }
                }
                return gen::rs_cache_resolution;
              }
            });

        gen_env[name].emplace(type, resolver);
      }
    }

    /* start by resolving the main function. the resolvers algorithm is a dfs
     * which gives topological ordering of the dependency graph. this can be a
     * bit intense on the compiler stack. worst case is O(user's program stack
     * depth OR the depth of the global object resolution graph). */
    gen_env.at(main_closure)
        .at(main_closure_type())
        ->resolve(builder, INTERNAL_LOC());

    /* finish the main function, filling out the main thunk which calls the main
     * closure. */
    write_main_block(builder, llvm_module, gen_env, main_closure,
                     llvm_main_function);

    auto temp_dir = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : ".");
    output_filename = temp_dir + "/" +
                      phase_3.phase_2.compilation->program_name + ".ll";

    llvm_verify_module(*llvm_module);

    std::ofstream ofs;
    ofs.open(output_filename.c_str(), std::ofstream::out);
    ofs << llvm_print_module(*llvm_module) << std::endl;
    ofs.close();

  } catch (user_error &e) {
    print_exception(e);
    /* and continue */
  }

  return Phase4(phase_3, std::move(gen_env), llvm_module, output_filename);
}

struct Job {
  std::string cmd;
  std::vector<std::string> opts;
  std::vector<std::string> args;
};

bool build_binary(const Job &job, bool explain, std::string &program_name) {
  if (explain) {
    std::cout << "build: compiles, specializes, generates LLVM output, then "
                 "links a binary executable"
              << std::endl;
    return false;
  }

  bool graph_deps = in_vector("-graph", job.opts);

  llvm::LLVMContext context;
  Phase4 phase_4 = ssa_gen(context,
                           specialize(compile(job.args[0], graph_deps)));

  if (user_error::errors_occurred()) {
    return false;
  }
  std::stringstream ss_c_flags;
  std::stringstream ss_compilands;
  std::stringstream ss_lib_flags;
  ss_compilands << "\"$ZION_RUNTIME/zion_rt.c\" ";
  for (auto link_in : phase_4.phase_3.phase_2.compilation->link_ins) {
    std::string link_text = unescape_json_quotes(link_in.name.text);
    switch (link_in.lit) {
    case lit_pkgconfig: {
      ss_c_flags << get_pkg_config("--cflags-only-I", link_text) << " ";
      ss_lib_flags << get_pkg_config("--libs --static", link_text) << " ";
      break;
    }
    case lit_link:
      ss_lib_flags << "-l\"" << link_text << "\" ";
      break;
    case lit_compile:
      ss_compilands << "\"$ZION_RUNTIME/" << link_text << "\" ";
      break;
    }
  }

  auto command_line = string_format(
  // We are using clang to lower the code from LLVM, and link it
  // to the runtime.
#ifdef __APPLE__
      "\"$(brew --prefix)/opt/llvm/bin/clang\" "
#else
      "clang "
#endif
      // Include any necessary include dirs for C dependencies.
      "%s "
#ifdef __APPLE__
      "-I \"$(xcrun --sdk macosx --show-sdk-path)/usr/include\" "
#endif
      // Allow for the user to specify optimizations
      "$ZION_OPT_FLAGS "
      // NB: we don't embed the target triple into the LL, so any
      // targeted triple causes an ugly error from clang, so I just
      // ignore it here.
      "-Wno-override-module "
      // HACKHACK: temporary workaround to allow libsodium to compile
      "-Wno-nullability-completeness "
      // TODO: plumb host targeting through clang here
      // "--target=$(llvm-config --host-target) "
      // Include extra compilands
      "%s "
      // Add linker flags
      "-lm %s "
      // Don't forget the built .ll file from our frontend here.
      "%s "
      // Give the binary a name.
      "-o %s",
      ss_c_flags.str().c_str(), ss_compilands.str().c_str(),
      ss_lib_flags.str().c_str(), phase_4.output_llvm_filename.c_str(),
      phase_4.phase_3.phase_2.compilation->program_name.c_str(),
      phase_4.phase_3.phase_2.compilation->program_name.c_str());
  if (debug_compile_step) {
    log("running %s", command_line.c_str());
  }
  if (std::system(command_line.c_str()) != 0) {
    throw user_error(INTERNAL_LOC(), "failed to compile binary");
  }
  program_name = phase_4.phase_3.phase_2.compilation->program_name;
  return true;
}

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
  if (in_vector("-n", job.opts)) {
    setenv("NO_PRELUDE", "1", true /*overwrite*/);
  }

  std::map<std::string, std::function<int(const Job &, bool)>> cmd_map;
  cmd_map["help"] = [&](const Job &job, bool explain) {
    std::cout << clean_ansi_escapes_if_not_tty(stdout, LOGO);
    std::cout << "zion";
#ifdef ZION_DEBUG
    std::cout << " (debug build)";
#endif
    std::cout << ':' << std::endl;
    for (auto &cmd_pair : cmd_map) {
      if (cmd_pair.first != "help") {
        /* run the command in explain mode */
        std::cout << "\t";
        // TODO: just have a different way of doing this... this is dumb.
        cmd_pair.second(job, true);
      }
    }
    std::cout << "Also try looking at the manpage. man zion." << std::endl;
    return EXIT_FAILURE;
  };
  cmd_map["test"] = [&](const Job &job, bool explain) {
    if (explain) {
      std::cout << "test: run tests" << std::endl;
      return EXIT_FAILURE;
    }

    test_assert(alphabetize(0) == "a");
    test_assert(alphabetize(1) == "b");
    test_assert(alphabetize(2) == "c");
    test_assert(alphabetize(26) == "aa");
    test_assert(alphabetize(27) == "ab");

    tarjan::Graph graph;
    graph.insert({"a", {"b", "f"}});
    graph.insert({"b", {"c"}});
    graph.insert({"g", {"c", "f"}});
    graph.insert({"d", {"c"}});
    graph.insert({"c", {"d"}});
    graph.insert({"h", {"g"}});
    graph.insert({"f", {"h", "c"}});
    tarjan::SCCs sccs = tarjan::compute_strongly_connected_components(graph);
    auto sccs_str = str(sccs);
    std::string tarjan_expect = "{{c, d}, {f, g, h}, {b}, {a}}";
    if (sccs_str != tarjan_expect) {
      log("tarjan says: %s\nit should say: %s", sccs_str.c_str(),
          tarjan_expect.c_str());
      test_assert(false);
    }

    test_assert(zion::tld::split_fqn("::copy::Copy").size() == 2);
    test_assert(zion::tld::is_tld_type("::Copy"));
    test_assert(zion::tld::is_tld_type("::Z"));
    test_assert(!zion::tld::is_tld_type("::copy::copy"));
    test_assert(!zion::tld::is_tld_type("copy::copy"));
    test_assert(!zion::tld::is_tld_type("copy"));
    test_assert(zion::tld::is_tld_type("::copy::Copy"));
    test_assert(!zion::tld::is_tld_type("::copy::copy"));
    test_assert(tld::split_fqn("::inc").size() == 1);

    return EXIT_SUCCESS;
  };
  cmd_map["find"] = [&](const Job &job, bool explain) {
    if (explain) {
      std::cout << "find: resolve module filenames" << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}, {}});
    }
    log("%s", compiler::resolve_module_filename(INTERNAL_LOC(), job.args[0],
                                                ".zion", maybe<std::string>())
                  .c_str());
    return EXIT_SUCCESS;
  };
  cmd_map["lex"] = [&](const Job &job, bool explain) {
    if (explain) {
      std::cout << "lex: lexes Zion into tokens" << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    }

    if (in_vector("-c", job.opts)) {
      std::string text = job.args[0];
      std::istringstream iss(text);
      Lexer lexer({"command-line"}, iss);
      Token token;
      bool newline = false;
      while (lexer.get_token(token, newline, nullptr)) {
        log_location(token.location, "%s (%s)", token.text.c_str(),
                     tkstr(token.tk));
      }
      return EXIT_SUCCESS;
    } else {
      std::string filename = compiler::resolve_module_filename(
          INTERNAL_LOC(), job.args[0], ".zion", maybe<std::string>());
      std::ifstream ifs;
      ifs.open(filename.c_str());
      Lexer lexer({filename}, ifs);
      Token token;
      bool newline = false;
      while (lexer.get_token(token, newline, nullptr)) {
        log_location(token.location, "%s (%s)", token.text.c_str(),
                     tkstr(token.tk));
      }
      return EXIT_SUCCESS;
    }
  };

  cmd_map["parse"] = [&](const Job &job, bool explain) {
    if (explain) {
      std::cout << "parse: parses Zion into an intermediate lambda calculus"
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
      std::cout << "compile: parses and compiles Zion into an intermediate "
                   "lambda calculus. this performs type checking"
                << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    } else {
      bool graph_deps = in_vector("-graph", job.opts);
      auto phase_2 = compile(job.args[0], graph_deps);
      if (user_error::errors_occurred()) {
        return EXIT_FAILURE;
      }
      phase_2.dump(std::cout);

      return user_error::errors_occurred() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
  };
  cmd_map["specialize"] = [&](const Job &job, bool explain) {
    if (explain) {
      std::cout << "specialize: compiles, then specializes the Zion lambda "
                   "calculus to a monomorphized form"
                << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    } else {
      bool graph_deps = in_vector("-graph", job.opts);
      auto phase_3 = specialize(compile(job.args[0], graph_deps));
      if (user_error::errors_occurred()) {
        return EXIT_FAILURE;
      }
      phase_3.dump(std::cout);
      return user_error::errors_occurred() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
  };
  cmd_map["ll"] = [&](const Job &job, bool explain) {
    if (explain) {
      std::cout << "ll: compiles, specializes, then generates LLVM output"
                << std::endl;
      return EXIT_FAILURE;
    }
    if (job.args.size() != 1) {
      return run_job({"help", {}});
    } else {
      bool graph_deps = in_vector("-graph", job.opts);
      llvm::LLVMContext context;
      Phase4 phase_4 = ssa_gen(context,
                               specialize(compile(job.args[0], graph_deps)));

      std::cout << phase_4.output_llvm_filename << std::endl;
      return user_error::errors_occurred() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
  };

  cmd_map["run"] = [&](const Job &job, bool explain) {
    if (explain) {
      std::cout << "run: compiles, specializes, generates LLVM output, then "
                   "runs the generated binary"
                << std::endl;
      return EXIT_FAILURE;
    }

    std::string program_name;
    if (build_binary(job, false /*explain*/, program_name)) {
      return run_program(program_name, vec_slice(job.args, 1, job.args.size()));
    } else {
      return EXIT_FAILURE;
    }
  };

  cmd_map["build"] = [&](const Job &job, bool explain) {
    std::string program_name;
    if (build_binary(job, explain, program_name)) {
      debug_above(1, log("zion build succeeded: %s", program_name.c_str()));
      return EXIT_SUCCESS;
    } else {
      std::cerr << "zion build failed." << std::endl;
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

} // namespace zion

namespace {
void append_env(std::string var_name, std::string value) {
  std::string existing_value = getenv(var_name.c_str())
                                   ? getenv(var_name.c_str())
                                   : "";
  if (existing_value.size() != 0) {
    std::stringstream ss;
    ss << existing_value << ":" << value;
    setenv("ZION_PATH", ss.str().c_str(), true /*overwrite*/);
    std::cout << "Setting ZION_PATH to " << ss.str() << std::endl;
  } else {
    setenv("ZION_PATH", value.c_str(), true /*overwrite*/);
  }
}
void setup_environment_variables() {
  const char *default_zion_root =
#ifdef __APPLE__
      "/usr/local/share/zion";
#else
      ".";
#endif

  setenv("ZION_ROOT", default_zion_root, false /*overwrite*/);

  std::stringstream ss;
  ss << getenv("ZION_ROOT") << "/lib";
  append_env("ZION_PATH", ss.str());
  ss.str("");
  ss << getenv("ZION_ROOT") << "/runtime";
  setenv("ZION_RUNTIME", ss.str().c_str(), false /*overwrite*/);
}
} // namespace

int main(int argc, char *argv[]) {
  setup_environment_variables();
  init_dbg();
  zion::init_host();
  std::shared_ptr<logger> logger(std::make_shared<standard_logger>("", "."));

  zion::Job job;
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
    return zion::run_job(job);
  } catch (zion::user_error &e) {
    zion::print_exception(e);
    /* and continue */
    return EXIT_FAILURE;
  }
}
