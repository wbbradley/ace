#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "location.h"
#include <vector>
#include <list>
#include "parse_state.h"

struct compiler_t {
	typedef std::vector<std::string> libs;

	compiler_t() = delete;
	compiler_t(const compiler_t &) = delete;
	compiler_t(std::string program_name, const libs &zion_paths);
	~compiler_t();

	std::string resolve_module_filename(location_t location, std::string name, std::string extension);
	void info(const char *format, ...);

	std::vector<token_t> get_comments() const;

	/* testing */
	std::string dump_program_text(std::string module_name);

	void setup_disk_environment();

	/* first step is to parse all modules */
	bool build_parse_modules();

	/* type checking and code generation happen during the same pass */
	bool build_type_check_and_code_gen();

	/* parse a single module */
	bitter::module_t *build_parse(location_t location, std::string module_name, type_macros_t &global_type_macros);

	std::set<std::string> compile_modules();
	void emit_built_program(std::string bitcode_filename);
	int run_program(int argc, char *argv[]);
	void emit_object_files(std::vector<std::string> &obj_files);

	void dump_ctags();

	std::string get_program_name() const;
	std::string get_executable_filename() const;

	bitter::module_t *main_module = nullptr;
	type_macros_t base_type_macros;

private:
	void lower_program_module();

	std::string program_name;
	std::shared_ptr<std::vector<std::string>> zion_paths;
	std::set<token_t> link_ins;
	std::vector<token_t> comments;
	std::map<std::string, bitter::module_t *> modules_map;
	bitter::program_t *program = nullptr;

	friend bool _check_compiler_error(compiler_t &compiler, int &skipped);
};

std::string strip_zion_extension(std::string module_name);
