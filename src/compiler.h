#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "location.h"
#include <vector>
#include <list>
#include "parse_state.h"
#include "infer.h"

struct compilation_t {
	using ref = std::shared_ptr<compilation_t>;
	compilation_t(
			std::string program_name,
			bitter::program_t *program,
			std::vector<token_t> comments,
			std::set<token_t> link_ins) : 
			// std::map<std::string, std::set<std::string>> data_ctors) :
		program_name(program_name),
		program(program),
		comments(comments),
		link_ins(link_ins)
		// data_ctors(data_ctors)
	{
	}

	std::string const program_name;
	bitter::program_t * const program;
	std::vector<token_t> const comments;
	std::set<token_t> const link_ins;
	// std::map<std::string, std::set<std::string>> const data_ctors;
};

namespace compiler {
	typedef std::vector<std::string> libs;

	void info(const char *format, ...);

	/* testing */
	std::string dump_program_text(std::string module_name);

	/* first step is to parse all modules */
	compilation_t::ref parse_program(std::string program_name);

	/* parse a single module */
	bitter::module_t *parse_module(location_t location, std::string module_name);
	std::string resolve_module_filename(
			location_t location,
			std::string name,
			std::string extension);
	std::set<std::string> get_top_level_decls(
			const std::vector<bitter::decl_t *> &decls,
			const std::vector<bitter::type_decl_t> &type_decls,
			const std::vector<bitter::type_class_t *> &type_classes);
};

std::string strip_zion_extension(std::string module_name);
const std::vector<std::string> &get_zion_paths();
