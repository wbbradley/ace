#pragma once
#include <list>
#include <vector>

#include "ast_decls.h"
#include "data_ctors_map.h"
#include "location.h"
#include "parse_state.h"
#include "ace.h"

namespace ace {
struct Compilation {
  using ref = std::shared_ptr<Compilation>;
  Compilation(std::string program_filename,
              std::string program_name,
              const ast::Program *program,
              std::vector<Token> comments,
              const std::set<LinkIn> &link_ins,
              const DataCtorsMap &data_ctors_map,
              const types::TypeEnv &type_env)
      : program_filename(program_filename), program_name(program_name),
        program(program), comments(comments), link_ins(link_ins),
        data_ctors_map(data_ctors_map), type_env(type_env) {
  }

  std::string const program_filename;
  std::string const program_name;
  const ast::Program *program;
  std::vector<Token> const comments;
  std::set<LinkIn> const link_ins;
  DataCtorsMap const data_ctors_map;
  types::TypeEnv const type_env;
};

namespace compiler {
typedef std::vector<std::string> libs;

void info(const char *format, ...);

/* first step is to parse all modules */
Compilation::ref parse_program(
    std::string program_name,
    const std::map<std::string, int> &builtin_arities);

/* parse a single module */
std::string resolve_module_filename(Location location,
                                    std::string name,
                                    std::string extension,
                                    const maybe<std::string> &reference_path);
std::set<std::string> get_top_level_decls(
    const std::vector<const ast::Decl *> &decls,
    const std::vector<const ast::TypeDecl *> &type_decls,
    const std::vector<const ast::TypeClass *> &type_classes,
    const std::vector<Identifier> &imports);
}; // namespace compiler

std::string strip_ace_extension(std::string module_name);
const std::vector<std::string> &get_ace_paths();
} // namespace ace
