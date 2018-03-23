#include "zion.h"
#include "ast.h"
#include "bound_var.h"
#include "unchecked_var.h"

void dump_bindings(
		std::ostream &os,
		const bound_var_t::map &bound_vars,
		const bound_type_t::map &bound_types,
		bool tags_fmt=false)
{
	if (bound_vars.size() != 0) {
		os << "bound vars:\n";
		for (auto &var_pair : bound_vars) {
			os << C_VAR << var_pair.first << C_RESET << ": ";
			const auto &overloads = var_pair.second;
			os << ::str(overloads);
		}
	}

	if (bound_types.size() != 0) {
		os << "bound types:\n";
		for (auto &type_pair : bound_types) {
			os << C_TYPE << type_pair.first << C_RESET << ": ";
			os << *type_pair.second << std::endl;
		}
	}
}

void dump_unchecked_vars(
		std::ostream &os,
		const unchecked_var_t::map &unchecked_vars,
		bool tags_fmt=false)
{
	if (unchecked_vars.size() != 0) {
		os << "unchecked vars:\n";
		for (auto &var_pair : unchecked_vars) {
			os << C_UNCHECKED << var_pair.first << C_RESET << ": [";
			const unchecked_var_t::overload_vector &overloads = var_pair.second;
			const char *sep = "";
			for (auto &var_overload : overloads) {
				os << sep << var_overload->node->token.str();
				sep = ", ";
			}
			os << "]" << std::endl;
		}
	}
}

void dump_unchecked_types(std::ostream &os, const unchecked_type_t::map &unchecked_types) {
	if (unchecked_types.size() != 0) {
		os << "unchecked types:\n";
		for (auto &type_pair : unchecked_types) {
			os << C_TYPE << type_pair.first << C_RESET << ": ";
			os << type_pair.second->node->token.str() << std::endl;
		}
	}
}

void dump_unchecked_type_tags(std::ostream &os, const unchecked_type_t::map &unchecked_types) {
	for (auto &type_pair : unchecked_types) {
		auto loc = type_pair.second->node->get_location();
		os << type_pair.first << "\t" << loc.filename_repr() << "\t" << loc.line << ";/^type " << type_pair.first << "/;\"\tkind:t" << std::endl;
	}
}

void dump_unchecked_var_tags(std::ostream &os, const unchecked_var_t::map &unchecked_vars) {
	for (auto &var_pair : unchecked_vars) {
		for (auto unchecked_var : var_pair.second) {
			auto loc = unchecked_var->node->get_location();
			os << var_pair.first << "\t" << loc.filename_repr() << "\t" << loc.line << ";/^\\(var\\|let\\|def\\) " << var_pair.first << "/;\"\tkind:f" << std::endl;
		}
	}
}

void dump_linked_modules(std::ostream &os, const module_scope_t::map &modules) {
	os << "modules: " << str(modules) << std::endl;
}

void dump_type_map(std::ostream &os, types::type_t::map env, std::string desc) {
	if (env.size() != 0) {
		os << std::endl << desc << std::endl;
		os << join_with(env, "\n", [] (types::type_t::map::value_type value) -> std::string {
			return string_format("%s: %s", value.first.c_str(), value.second->str().c_str());
		});
		os << std::endl;
	}
}

void dump_env_map(std::ostream &os, const env_map_t &env_map, std::string desc) {
	if (env_map.size() != 0) {
		os << std::endl << desc << std::endl;
		os << join_with(env_map, "\n", [] (const env_map_t::value_type &value) -> std::string {
			return string_format("[%s] %s: %s", value.second.first ? "S" : "N", value.first.c_str(), value.second.second->str().c_str());
		});
		os << std::endl;
	}
}


