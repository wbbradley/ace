#pragma once
#include <list>
#include <vector>

#include "ast_decls.h"
#include "infer.h"
#include "location.h"
#include "parse_state.h"
#include "zion.h"

struct compilation_t {
  using ref = std::shared_ptr<compilation_t>;
  compilation_t(std::string program_name,
                bitter::program_t *program,
                std::vector<Token> comments,
                std::set<Token> link_ins,
                const ctor_id_map_t &ctor_id_map,
                const data_ctors_map_t &data_ctors_map,
                const types::type_env_t &type_env)
      : program_name(program_name), program(program), comments(comments),
        link_ins(link_ins), ctor_id_map(ctor_id_map),
        data_ctors_map(data_ctors_map), type_env(type_env) {
  }

  std::string const program_name;
  bitter::program_t *const program;
  std::vector<Token> const comments;
  std::set<Token> const link_ins;
  ctor_id_map_t const ctor_id_map;
  data_ctors_map_t const data_ctors_map;
  types::type_env_t const type_env;
};

namespace compiler {
typedef std::vector<std::string> libs;

void info(const char *format, ...);

/* first step is to parse all modules */
compilation_t::ref parse_program(
    std::string program_name,
    const std::map<std::string, int> &builtin_arities);

/* parse a single module */
bitter::module_t *parse_module(location_t location, std::string module_name);
std::string resolve_module_filename(location_t location,
                                    std::string name,
                                    std::string extension);
std::set<std::string> get_top_level_decls(
    const std::vector<bitter::decl_t *> &decls,
    const std::vector<bitter::type_decl_t> &type_decls,
    const std::vector<bitter::type_class_t *> &type_classes);
}; // namespace compiler

std::string strip_zion_extension(std::string module_name);
const std::vector<std::string> &get_zion_paths();
