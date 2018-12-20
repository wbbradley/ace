#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "location.h"
#include <vector>
#include <list>
#include "parse_state.h"
#include "infer.h"

struct compiler_t {
	typedef std::vector<std::string> libs;

	compiler_t() = delete;
	compiler_t(const compiler_t &) = delete;
	compiler_t(std::string program_name);
	~compiler_t();

	void info(const char *format, ...);

	/* testing */
	std::string dump_program_text(std::string module_name);

	/* first step is to parse all modules */
	bool parse_program();

	bool build_type_check_and_code_gen();

	/* parse a single module */
	bitter::module_t *parse_module(location_t location, std::string module_name);

	std::string get_executable_filename() const;

	bitter::program_t *program = nullptr;

private:
	std::string program_name;
	std::vector<token_t> comments;
	std::set<token_t> link_ins;

	friend bool _check_compiler_error(compiler_t &compiler, int &skipped);
};

std::string strip_zion_extension(std::string module_name);
const std::vector<std::string> &get_zion_paths();
std::string resolve_module_filename(
		location_t location,
		std::string name,
		std::string extension);
