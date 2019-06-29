#pragma once
#include <list>
#include <vector>

#include "ast_decls.h"
#include "infer.h"
#include "location.h"
#include "parse_state.h"
#include "zion.h"

struct Compilation {
  using ref = std::shared_ptr<Compilation>;
  Compilation(std::string program_name,
              bitter::Program *program,
              std::vector<Token> comments,
              const std::set<LinkIn> &link_ins,
              const ctor_id_map_t &ctor_id_map,
              const data_ctors_map_t &data_ctors_map,
              const types::type_env_t &type_env)
      : program_name(program_name), program(program), comments(comments),
        link_ins(link_ins), ctor_id_map(ctor_id_map),
        data_ctors_map(data_ctors_map), type_env(type_env) {
  }

  std::string const program_name;
  bitter::Program *const program;
  std::vector<Token> const comments;
  std::set<LinkIn> const link_ins;
  ctor_id_map_t const ctor_id_map;
  data_ctors_map_t const data_ctors_map;
  types::type_env_t const type_env;
};

namespace compiler {
typedef std::vector<std::string> libs;

void info(const char *format, ...);

/* first step is to parse all modules */
Compilation::ref parse_program(
    std::string program_name,
    const std::map<std::string, int> &builtin_arities);

/* parse a single module */
bitter::Module *parse_module(Location location, std::string module_name);
std::string resolve_module_filename(Location location,
                                    std::string name,
                                    std::string extension);
std::set<std::string> get_top_level_decls(
    const std::vector<bitter::Decl *> &decls,
    const std::vector<bitter::TypeDecl> &type_decls,
    const std::vector<bitter::TypeClass *> &type_classes);
}; // namespace compiler

std::string strip_zion_extension(std::string module_name);
const std::vector<std::string> &get_zion_paths();
