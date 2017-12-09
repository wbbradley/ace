#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "location.h"
#include <vector>
#include <list>
#include "type_checker.h"
#include "scopes.h"
#include "parse_state.h"

struct compiler_t {
	typedef std::vector<std::string> libs;
	typedef std::pair<std::string, std::unique_ptr<llvm::Module>> llvm_module_t;
	typedef std::list<llvm_module_t> llvm_modules_t;

	compiler_t() = delete;
	compiler_t(const compiler_t &) = delete;
	compiler_t(std::string program_name, const libs &zion_paths);
	~compiler_t();

	void resolve_module_filename(status_t &status, location_t location, std::string name, std::string &resolved);
	void info(const char *format, ...);

	module_scope_t::ref get_module_scope(std::string module_key);
	void set_module_scope(std::string module_key, module_scope_t::ref module_scope);

	std::vector<token_t> get_comments() const;
	ptr<const ast::module_t> get_module(status_t &status, std::string key_alias);
	void set_module(status_t &status, std::string filename, ptr<ast::module_t> module);
	llvm::Module *llvm_load_ir(status_t &status, std::string filename);
	llvm::Module *llvm_create_module(std::string module_name);
	llvm::Module *llvm_get_program_module();

	/* testing */
	std::string dump_llvm_modules();
	std::string dump_program_text(std::string module_name);

	void write_obj_file(status_t &status, std::unique_ptr<llvm::Module> &llvm_module);

	void setup_disk_environment(status_t &status);

	/* first step is to parse all modules */
	void build_parse_modules(status_t &status);

	/* type checking and code generation happen during the same pass */
	void build_type_check_and_code_gen(status_t &status);

	/* parse a single module */
	ptr<const ast::module_t> build_parse(status_t &status, location_t location, std::string module_name, bool global, type_macros_t &global_type_macros);

	void build_parse_linked(status_t &status, ptr<const ast::module_t> module, type_macros_t &global_type_macros);
	std::set<std::string> compile_modules(status_t &status);
	void emit_built_program(status_t &status, std::string bitcode_filename);
	int run_program(int argc, char *argv[]);
	void emit_object_files(status_t &status, std::vector<std::string> &obj_files);

	program_scope_t::ref get_program_scope() const;
	std::string get_program_name() const;
	std::string get_executable_filename() const;

	ptr<const ast::module_t> main_module;
	type_macros_t base_type_macros;

	/* member variables */
private:
	void lower_program_module();

	std::unique_ptr<llvm::Module> &get_llvm_module(std::string name);

	std::string program_name;
	ptr<std::vector<std::string>> zion_paths;
	std::vector<token_t> comments;
	program_scope_t::ref program_scope;
	std::map<std::string, ptr<const ast::module_t>> modules;
	llvm::LLVMContext llvm_context;
	llvm::IRBuilder<> builder;
	llvm_module_t llvm_program_module;
	llvm_modules_t llvm_modules;
	std::map<std::string, ptr<module_scope_t>> module_scopes;
	ptr<ast::program_t> program;

	friend bool _check_compiler_error(compiler_t &compiler, int &skipped);
	friend struct program_scope_t;
};

std::string strip_zion_extension(std::string module_name);
