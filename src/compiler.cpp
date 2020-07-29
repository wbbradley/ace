#include "compiler.h"

#include <cstdarg>
#include <fstream>
#include <iostream>
#include <set>
#include <sys/stat.h>
#include <vector>

#include "ast.h"
#include "disk.h"
#include "import_rules.h"
#include "lexer.h"
#include "link_ins.h"
#include "parse_state.h"
#include "parser.h"
#include "prefix.h"
#include "tld.h"
#include "utils.h"
#include "zion.h"

namespace zion {

using namespace ast;

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
      for (auto &path : split(getenv("ZION_PATH"), ":")) {
        if (path != "") {
          /* just be careful that user didn't put in an empty ZION_PATH */
          zion_paths.push_back(path);
        }
      }
    } else {
      log(log_error,
          "ZION_PATH is not set. It should be set to the dirname of std.zion. "
          "That is typically /usr/local/share/zion/lib.");
      exit(1);
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

std::string resolve_module_filename(Location location,
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
    } else if (file_exists(filename_test_resolution) &&
               (extension == "" ||
                ends_with(filename_test_resolution, extension))) {
      return filename_test_resolution;
    }
  }

  std::string leaf_name;
  if (ends_with(leaf_name, extension)) {
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
        if (working_resolution.size() &&
            working_resolution != test_resolution) {
          throw user_error(location,
                           "multiple " C_FILENAME "%s" C_RESET
                           " modules found with the same name in source "
                           "path [%s, %s]",
                           name.c_str(), working_resolution.c_str(),
                           test_resolution.c_str());
        } else {
          working_resolution = test_resolution;
          debug_above(11, log(log_info, "searching for file %s, found it at %s",
                              name.c_str(), working_resolution.c_str()));
        }
      } else {
        /* if the file exists, it should have a real_path */
        panic(string_format(
            "searching for file %s, unable to resolve its real path (%s)",
            name.c_str(), test_path.c_str()));
      }
    } else {
      debug_above(11,
                  log(log_info, "searching for file %s, did not find it at %s",
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
        "module not found: " c_error("`%s`") ". Looked in ZION_PATH=[%s]",
        name.c_str(), join(get_zion_paths(), ":").c_str());
    return "";
  }
}

struct GlobalParserState {
  GlobalParserState(const std::map<std::string, int> &builtin_arities)
      : builtin_arities(builtin_arities) {
  }
  std::vector<const Module *> modules;
  std::map<std::string, const Module *> modules_map_by_filename;
  std::map<std::string, const Module *> modules_map_by_name;
  parser::SymbolExports symbol_exports;
  parser::SymbolImports symbol_imports;
  std::vector<Token> comments;
  std::set<LinkIn> link_ins;
  const std::map<std::string, int> &builtin_arities;

  const Module *parse_module_statefully(Identifier module_id) {
    if (auto module = get(modules_map_by_name, module_id.name,
                          static_cast<const Module *>(nullptr))) {
      return module;
    }
    std::string module_filename = compiler::resolve_module_filename(
        module_id.location, module_id.name, ".zion");
    if (auto module = get(modules_map_by_filename, module_filename,
                          static_cast<const Module *>(nullptr))) {
      return module;
    }

    /* we found an unparsed file */
    std::ifstream ifs;
    ifs.open(module_filename.c_str());

    if (ifs.good()) {
      debug_above(11, log(log_info, "parsing module " c_id("%s"),
                          module_filename.c_str()));
      Lexer lexer({module_filename}, ifs);

      parser::ParseState ps(module_filename, "", lexer, comments, link_ins,
                            symbol_exports, symbol_imports, builtin_arities);

      std::set<Identifier> dependencies;
      const Module *module = parse_module(ps, {modules_map_by_name["std"]},
                                          dependencies);

      modules.push_back(module);

      /* break any circular dependencies. inject this module into the graph */
      modules_map_by_name[ps.module_name] = module;
      modules_map_by_filename[ps.filename] = module;

      debug_above(8, log("while parsing %s got dependencies {%s}",
                         ps.module_name.c_str(),
                         join(dependencies, ", ").c_str()));
      for (auto dependency : dependencies) {
        parse_module_statefully(dependency);
      }

      return module;
    } else {
      auto error = user_error(
          module_id.location,
          "could not open \"%s\" when trying to link module",
          module_filename.c_str());
      error.add_info(module_id.location, "imported here");
      throw error;
    }
  }
};

std::set<std::string> get_top_level_decls(
    const std::vector<const Decl *> &decls,
    const std::vector<const TypeDecl *> &type_decls,
    const std::vector<const TypeClass *> &type_classes,
    const std::vector<Identifier> &imports) {
  std::map<std::string, Location> module_decls;
  for (const Decl *decl : decls) {
    if (module_decls.find(decl->id.name) != module_decls.end()) {
      auto error = user_error(decl->id.location, "duplicate symbol");
      error.add_info(module_decls[decl->id.name], "see prior definition here");
      throw error;
    }
    module_decls[decl->id.name] = decl->id.location;
  }
  std::set<std::string> top_level_decls;
  for (auto pair : module_decls) {
    top_level_decls.insert(pair.first);
  }
  for (auto type_decl : type_decls) {
    top_level_decls.insert(type_decl->id.name);
  }
  for (auto type_class : type_classes) {
    top_level_decls.insert(type_class->id.name);
    for (auto overload_pair : type_class->overloads) {
      top_level_decls.insert(overload_pair.first);
    }
  }
  for (auto &import : imports) {
    assert(tld::is_fqn(import.name, true /*default_special*/));
    top_level_decls.insert(import.name);
  }
  debug_above(8, log("tlds are %s", ::join(top_level_decls, ", ").c_str()));
  return top_level_decls;
}

std::shared_ptr<Compilation> merge_compilation(
    std::string program_filename,
    std::string program_name,
    std::vector<const Module *> modules,
    const std::vector<Token> &comments,
    const std::set<LinkIn> &link_ins) {
  std::vector<const Decl *> program_decls;
  std::vector<const TypeClass *> program_type_classes;
  std::vector<const Instance *> program_instances;
  ParsedCtorIdMap ctor_id_map;
  ParsedDataCtorsMap data_ctors_map;
  types::TypeEnv type_env;

  /* next, merge the entire set of modules into one program */
  for (const Module *module : modules) {
    /* get a list of all top-level decls */
    std::set<std::string> maybe_not_tld_bindings = get_top_level_decls(
        module->decls, module->type_decls, module->type_classes,
        module->imports);

    std::set<std::string> bindings;
    for (auto binding: maybe_not_tld_bindings) {
      bindings.insert(binding);
      bindings.insert(tld::tld(binding));
    }
    const Module *module_rebound = prefix(bindings, module);


    /* now all locally referring vars are fully qualified */
    for (const Decl *decl : module_rebound->decls) {
      program_decls.push_back(decl);
    }

    for (const TypeClass *type_class : module_rebound->type_classes) {
      program_type_classes.push_back(type_class);
    }

    for (const Instance *instance : module_rebound->instances) {
      program_instances.push_back(instance);
    }

    for (auto pair : module_rebound->ctor_id_map) {
      if (in(pair.first, ctor_id_map)) {
        throw user_error(INTERNAL_LOC(),
            "ctor_id %s already exists in ctor_id_map but is trying to be added by module %s!",
            pair.first.c_str(),
            module_rebound->name.c_str());
      }
      ctor_id_map[pair.first] = pair.second;
    }

    for (auto pair : module_rebound->data_ctors_map) {
      if (in(pair.first, data_ctors_map)) {
        throw user_error(INTERNAL_LOC(),
                         "data constructor %s already exists in data_ctors_map "
                         "but is trying to be added by module %s as %s!",
                         pair.first.c_str(), module_rebound->name.c_str(),
                         str(data_ctors_map.at(pair.first)).c_str());
      }
      data_ctors_map[pair.first] = pair.second;
    }

    for (auto pair : module_rebound->type_env) {
      assert(!in(pair.first, type_env));
      type_env[pair.first] = pair.second;
    }
  }

  return std::make_shared<Compilation>(
      program_filename, program_name,
      new Program(program_decls, program_type_classes, program_instances,
                  new Application(new Var(make_iid("main")),
                                  {unit_expr(INTERNAL_LOC())})),
      comments, link_ins, DataCtorsMap{data_ctors_map, ctor_id_map}, type_env);
}

Compilation::ref parse_program(
    std::string user_program_name,
    const std::map<std::string, int> &builtin_arities) {
  std::string program_name = strip_zion_extension(
      leaf_from_file_path(user_program_name));
  try {
    /* first just parse all the modules that are reachable from the initial
     * module and bring them into our whole ast */
    auto module_name = program_name;

    GlobalParserState gps(builtin_arities);

    /* include the builtins library */
    if (getenv("NO_PRELUDE") == nullptr || atoi(getenv("NO_PRELUDE")) == 0) {
      gps.parse_module_statefully(
          Identifier{"std" /* lib/std */, Location{"std", 0, 0}});
    } else {
      /* in the case that we are omitting the prelude, still include the GC */
      gps.link_ins.insert(LinkIn{
          lit_pkgconfig, Token{INTERNAL_LOC(), tk_string, "\"bdw-gc\""}});
    }

    /* now parse the main program module */
    gps.parse_module_statefully(Identifier{
        user_program_name, Location{"command line build parameters", 0, 0}});

    debug_above(11, log(log_info, "parse_module of %s succeeded",
                        module_name.c_str(), false /*global*/));

    /* find the import rewriting rules */
    RewriteImportRules rewriting_imports_rules = solve_rewriting_imports(
        gps.symbol_imports, gps.symbol_exports);

    std::string program_filename = compiler::resolve_module_filename(
        INTERNAL_LOC(), user_program_name, ".zion");
    return merge_compilation(
        program_filename, program_name,
        rewrite_modules(rewriting_imports_rules, gps.modules), gps.comments,
        gps.link_ins);

  } catch (user_error &e) {
    print_exception(e);
    return nullptr;
  }
}

} // namespace compiler
} // namespace zion
