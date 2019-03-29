#include "compiler.h"

#include <fstream>
#include <iostream>
#include <set>
#include <stdarg.h>
#include <sys/stat.h>
#include <vector>

#include "ast.h"
#include "disk.h"
#include "lexer.h"
#include "parse_state.h"
#include "parser.h"
#include "prefix.h"
#include "utils.h"
#include "zion.h"

using namespace bitter;

std::string strip_zion_extension(std::string module_name) {
  if (ends_with(module_name, ".zion")) {
    /* as a courtesy, strip the extension from the filename here */
    return module_name.substr(0, module_name.size() - strlen(".zion"));
  } else {
    return module_name;
  }
}

const std::vector<std::string> &get_zion_paths() {
  static bool checked = false;
  static std::vector<std::string> zion_paths;

  if (!checked) {
    checked = true;
    if (getenv("ZION_PATH") != nullptr) {
      zion_paths = split(getenv("ZION_PATH"), ":");
    }
    zion_paths.insert(zion_paths.begin(), ".");
    for (auto &zion_path : zion_paths) {
      /* fix any paths to be absolute */
      real_path(zion_path, zion_path);
    }
  }
  return zion_paths;
}

namespace compiler {

std::string get_executable_filename() {
  return "z.out";
}

std::string resolve_module_filename(location_t location,
                                    std::string name,
                                    std::string extension) {
  std::string filename_test_resolution;
  if (real_path(name, filename_test_resolution)) {
    if (name == filename_test_resolution) {
      if (!ends_with(filename_test_resolution, extension)) {
        filename_test_resolution = filename_test_resolution + extension;
      }

      if (file_exists(filename_test_resolution)) {
        /* short circuit if we're given a real path */
        return filename_test_resolution;
      } else {
        panic(string_format("filename %s does not exist", name.c_str()));
      }
    } else if (extension == "" && file_exists(filename_test_resolution)) {
      return filename_test_resolution;
    }
  }

  std::string leaf_name;
  if (name.find(extension) == name.size() - extension.size()) {
    leaf_name = name;
  } else {
    leaf_name = name + extension;
  }
  std::string working_resolution;
  for (auto zion_path : get_zion_paths()) {
    auto test_path = zion_path + "/" + leaf_name;
    if (file_exists(test_path)) {
      std::string test_resolution;
      if (real_path(test_path, test_resolution)) {
        if (working_resolution.size() && working_resolution != test_resolution) {
          throw user_error(location,
                           "multiple " C_FILENAME "%s" C_RESET
                           " modules found with the same name in source "
                           "path [%s, %s]",
                           name.c_str(), working_resolution.c_str(), test_resolution.c_str());
        } else {
          working_resolution = test_resolution;
          debug_above(11, log(log_info, "searching for file %s, found it at %s", name.c_str(),
                              working_resolution.c_str()));
        }
      } else {
        /* if the file exists, it should have a real_path */
        panic(string_format("searching for file %s, unable to resolve its real path (%s)",
                            name.c_str(), test_path.c_str()));
      }
    } else {
      debug_above(11, log(log_info, "searching for file %s, did not find it at %s",
                          name.c_str(), test_path.c_str()));
    }
  }

  if (working_resolution.size() != 0) {
    /* cool, we found one and only one module with the requested name in
     * the source paths */
    return working_resolution;
  } else {
    throw user_error(
        location,
        "module not found: " c_error("`%s`") " (Note that module names should not have "
                                             ".zion extensions.) Looked in ZION_PATH=[%s]",
        name.c_str(), join(get_zion_paths(), ":").c_str());
    return "";
  }
}

struct global_parser_state_t {
  global_parser_state_t(const std::map<std::string, int> &builtin_arities)
      : builtin_arities(builtin_arities) {
  }
  std::vector<module_t *> modules;
  std::map<std::string, module_t *> modules_map_by_filename;
  std::map<std::string, module_t *> modules_map_by_name;
  std::vector<token_t> comments;
  std::set<token_t> link_ins;
  const std::map<std::string, int> &builtin_arities;

  module_t *parse_module_statefully(identifier_t module_id) {
    if (auto module = get(modules_map_by_name, module_id.name, (module_t *)nullptr)) {
      return module;
    }
    std::string module_filename =
        compiler::resolve_module_filename(module_id.location, module_id.name, ".zion");
    if (auto module = get(modules_map_by_filename, module_filename, (module_t *)nullptr)) {
      return module;
    }

    /* we found an unparsed file */
    std::ifstream ifs;
    ifs.open(module_filename.c_str());

    if (ifs.good()) {
      debug_above(11, log(log_info, "parsing module " c_id("%s"), module_filename.c_str()));
      zion_lexer_t lexer({module_filename}, ifs);

      parse_state_t ps(module_filename, "", lexer, comments, link_ins, builtin_arities);

      std::set<identifier_t> dependencies;
      module_t *module = ::parse_module(ps, {modules_map_by_name["std"]}, dependencies);

      modules.push_back(module);
      modules_map_by_name[ps.module_name] = module;
      modules_map_by_filename[ps.filename] = module;

      debug_above(8, log("while parsing %s got dependencies {%s}", module_id.str().c_str(),
                         join(dependencies, ", ").c_str()));
      for (auto dependency : dependencies) {
        parse_module_statefully(dependency);
      }

      return module;
    } else {
      auto error =
          user_error(module_id.location, "could not open \"%s\" when trying to link module",
                     module_filename.c_str());
      error.add_info(module_id.location, "imported here");
      throw error;
    }
  }
};

std::set<std::string> get_top_level_decls(const std::vector<decl_t *> &decls,
                                          const std::vector<type_decl_t> &type_decls,
                                          const std::vector<type_class_t *> &type_classes) {
  std::map<std::string, location_t> module_decls;
  for (decl_t *decl : decls) {
    if (module_decls.find(decl->var.name) != module_decls.end()) {
      auto error = user_error(decl->var.location, "duplicate symbol");
      error.add_info(module_decls[decl->var.name], "see prior definition here");
      throw error;
    }
    module_decls[decl->var.name] = decl->var.location;
  }
  std::set<std::string> top_level_decls;
  for (auto pair : module_decls) {
    top_level_decls.insert(pair.first);
  }
  for (auto type_decl : type_decls) {
    top_level_decls.insert(type_decl.id.name);
  }
  for (auto type_class : type_classes) {
    top_level_decls.insert(type_class->id.name);
    for (auto overload_pair : type_class->overloads) {
      top_level_decls.insert(overload_pair.first);
    }
  }
  debug_above(8, log("tlds are %s", ::join(top_level_decls, ", ").c_str()));
  return top_level_decls;
}

compilation_t::ref parse_program(std::string user_program_name,
                                 const std::map<std::string, int> &builtin_arities) {
  std::string program_name = strip_zion_extension(leaf_from_file_path(user_program_name));
  try {
    /* first just parse all the modules that are reachable from the initial
     * module and bring them into our whole ast */
    auto module_name = program_name;

    global_parser_state_t gps(builtin_arities);

    /* always include the builtins library */
    if (getenv("NO_PRELUDE") == nullptr) {
      gps.parse_module_statefully({"lib/std", location_t{"std", 0, 0}});
    }

    /* now parse the main program module */
    gps.parse_module_statefully(
        {module_name, location_t{"command line build parameters", 0, 0}});

    debug_above(11, log(log_info, "parse_module of %s succeeded", module_name.c_str(),
                        false /*global*/));

    std::vector<decl_t *> program_decls;
    std::vector<type_class_t *> program_type_classes;
    std::vector<instance_t *> program_instances;
    ctor_id_map_t ctor_id_map;
    data_ctors_map_t data_ctors_map;

    /* next, merge the entire set of modules into one program */
    for (module_t *module : gps.modules) {
      /* get a list of all top-level decls */
      std::set<std::string> bindings =
          get_top_level_decls(module->decls, module->type_decls, module->type_classes);

      module_t *module_rebound = prefix(bindings, module);

      /* now all locally referring vars are fully qualified */
      for (decl_t *decl : module_rebound->decls) {
        program_decls.push_back(decl);
      }

      for (type_class_t *type_class : module_rebound->type_classes) {
        program_type_classes.push_back(type_class);
      }

      for (instance_t *instance : module_rebound->instances) {
        program_instances.push_back(instance);
      }

      for (auto pair : module_rebound->ctor_id_map) {
        assert(!in(pair.first, ctor_id_map));
        ctor_id_map[pair.first] = pair.second;
      }

      for (auto pair : module_rebound->data_ctors_map) {
        assert(!in(pair.first, data_ctors_map));
        data_ctors_map[pair.first] = pair.second;
      }
    }

    return std::make_shared<compilation_t>(
        program_name,
        new program_t(
            program_decls, program_type_classes, program_instances,
            new application_t(new var_t(make_iid("main")), new tuple_t(INTERNAL_LOC(), {}))),
        gps.comments, gps.link_ins, ctor_id_map, data_ctors_map);
  } catch (user_error &e) {
    print_exception(e);
    return nullptr;
  }
}
} // namespace compiler
