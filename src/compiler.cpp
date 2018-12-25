#include "zion.h"
#include <stdarg.h>
#include "ast.h"
#include "compiler.h"
#include "lexer.h"
#include "parse_state.h"
#include "parser.h"
#include <vector>
#include "disk.h"
#include <fstream>
#include <set>
#include "utils.h"
#include <sys/stat.h>
#include <iostream>
#include "prefix.h"

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

compiler_t::compiler_t(std::string program_name_) :
	program_name(strip_zion_extension(program_name_))
{
}

compiler_t::~compiler_t() {
}

void compiler_t::info(const char *format, ...) {
	va_list args;
	va_start(args, format);
	logv(log_info, format, args);
	va_end(args);
}

std::string compiler_t::get_executable_filename() const {
	return "z.out";
}

std::string resolve_module_filename(
		location_t location,
		std::string name,
		std::string extension)
{
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

	std::string leaf_name = name + extension;
	std::string working_resolution;
	for (auto zion_path : get_zion_paths()) {
		auto test_path = zion_path + "/" + leaf_name;
		if (file_exists(test_path)) {
			std::string test_resolution;
			if (real_path(test_path, test_resolution)) {
				if (working_resolution.size() && working_resolution != test_resolution) {
					throw user_error(location, "multiple " C_FILENAME "%s"
							C_RESET " modules found with the same name in source "
							"path [%s, %s]", name.c_str(),
							working_resolution.c_str(),
							test_resolution.c_str());
				} else {
					working_resolution = test_resolution;
					debug_above(11, log(log_info, "searching for file %s, found it at %s",
								name.c_str(),
								working_resolution.c_str()));
				}
			} else {
				/* if the file exists, it should have a real_path */
				panic(string_format("searching for file %s, unable to resolve its real path (%s)",
							name.c_str(),
							test_path.c_str()));
			}
		} else {
			debug_above(11, log(log_info, "searching for file %s, did not find it at %s",
						name.c_str(),
						test_path.c_str()));
		}
	}

	if (working_resolution.size() != 0) {
		/* cool, we found one and only one module with the requested name in
		 * the source paths */
		return working_resolution;
	} else {
		throw user_error(location, "module not found: " c_error("`%s`") " (Note that module names should not have .zion extensions.) Looked in ZION_PATH=[%s]",
				name.c_str(),
				join(get_zion_paths(), ":").c_str());
		return "";
	}
}

struct global_parser_state_t {

	std::vector<module_t *> modules;
	std::map<std::string, module_t *> modules_map_by_filename;
	std::map<std::string, module_t *> modules_map_by_name;
	std::vector<token_t> comments;
	std::set<token_t> link_ins;

	module_t *parse_module(identifier_t module_id) {
		if (auto module = get(modules_map_by_name, module_id.name, (module_t *)nullptr)) {
			return module;
		}
		std::string module_filename = resolve_module_filename(module_id.location, module_id.name, ".zion");
		if (auto module = get(modules_map_by_filename, module_filename, (module_t *)nullptr)) {
			return module;
		}

		/* we found an unparsed file */
		std::ifstream ifs;
		ifs.open(module_filename.c_str());

		if (ifs.good()) {
			debug_above(11, log(log_info, "parsing module " c_id("%s"), module_filename.c_str()));
			zion_lexer_t lexer({module_filename}, ifs);

			parse_state_t ps(module_filename, "", lexer, &comments, &link_ins);

			identifiers_t dependencies;
			module_t *module = ::parse_module(ps, dependencies);

			modules.push_back(module);
			modules_map_by_name[ps.module_name] = module;
			modules_map_by_filename[ps.filename] = module;

			for (auto dependency : dependencies) {
				parse_module(dependency);
			}

			return module;
		} else {
			auto error = user_error(module_id.location, "could not open \"%s\" when trying to link module", module_filename.c_str());
			error.add_info(module_id.location, "imported here");
			throw error;
		}
	}
};


std::set<std::string> get_top_level_decls(
		const std::vector<decl_t *> &decls,
		const std::vector<type_decl_t> &type_decls,
		const std::vector<type_class_t *> &type_classes)
{
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
	}
	return top_level_decls;
}

bool compiler_t::parse_program() {
	try {
		/* first just parse all the modules that are reachable from the initial module
		 * and bring them into our whole ast */
		auto module_name = program_name;

		assert(program == nullptr);

		global_parser_state_t gps;

		/* always include the builtins library */
		if (getenv("NO_BUILTINS") == nullptr) {
			gps.parse_module({"lib/builtins", location_t{"builtins", 0, 0}});
		}

		/* always include the standard library */
		if (getenv("NO_STD_LIB") == nullptr) {
			gps.parse_module({"lib/std", location_t{std::string(GLOBAL_SCOPE_NAME) + " lib", 0, 0}});
		}

		/* now parse the main program module */
		gps.parse_module({module_name, location_t{"command line build parameters", 0, 0}});

		debug_above(11, log(log_info, "parse_module of %s succeeded", module_name.c_str(),
					false /*global*/));

		std::vector<decl_t *> program_decls;
		std::vector<type_class_t *> program_type_classes;
		/* next, merge the entire set of modules into one program */

		for (module_t *module : gps.modules) {
			/* get a list of all top-level decls */
			std::set<std::string> bindings = get_top_level_decls(module->decls, module->type_decls, module->type_classes);
			module_t *module_rebound = prefix(bindings, module);

			/* now all locally referring vars are fully qualified */
			for (decl_t *decl : module_rebound->decls) {
				program_decls.push_back(decl);
			}
			for (type_class_t *type_class : module_rebound->type_classes) {
				program_type_classes.push_back(type_class);
			}
		}
		assert(program == nullptr);
		program = new program_t(
				program_decls,
				program_type_classes,
			   	new application_t(
					new var_t(make_iid("main")),
				   	new var_t(make_iid("unit"))));

		comments = gps.comments;
		link_ins = gps.link_ins;

		return true;
	} catch (user_error &e) {
		print_exception(e);
		return false;
	}
}


bool compiler_t::build_type_check_and_code_gen() {
	try {
		debug_above(2, log(log_info, "type checking found no errors"));
		// TODO:...
		assert(false);
		return true;

	} catch (user_error &e) {
		print_exception(e);
		return false;
	}
}

std::string compute_module_key(std::vector<std::string> lib_paths, std::string filename) {
	std::string working_key;
	for (auto lib_path : lib_paths) {
		if (starts_with(filename, lib_path)) {
			if (working_key.size() < (filename.size() - lib_path.size())) {
				working_key = filename.substr(lib_path.size() + 1);
				assert(ends_with(working_key, ".zion"));
				working_key = working_key.substr(0, working_key.size() - strlen(".zion"));
			}
		}

	}
	if (working_key.size() == 0) {
		if (filename[0] == '/') {
				assert(ends_with(filename, ".zion"));
				working_key = filename.substr(0, filename.size() - strlen(".zion"));
		} else {
			panic(string_format("could not find module filename in lib paths (%s)",
						filename.c_str()));
		}
	}
	return working_key;
}
