#include "bound_var.h"
#include <ostream>
#include "scopes.h"

void dump_bindings(std::ostream &os, const bound_var_t::map &bound_vars, const bound_type_t::map &bound_types, bool tags_fmt=false);
void dump_unchecked_vars(std::ostream &os, const unchecked_var_t::map &unchecked_vars, bool tags_fmt=false);
void dump_unchecked_types(std::ostream &os, const unchecked_type_t::map &unchecked_types);
void dump_unchecked_type_tags(std::ostream &os, const unchecked_type_t::map &unchecked_types);
void dump_unchecked_var_tags(std::ostream &os, const unchecked_var_t::map &unchecked_vars);
void dump_linked_modules(std::ostream &os, const module_scope_t::map &modules);
void dump_type_map(std::ostream &os, types::type_t::map env, std::string desc);
void dump_env_map(std::ostream &os, const env_map_t &env_map, std::string desc);
