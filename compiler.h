#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "location.h"
#include <vector>
#include <list>
#include "type_checker.h"
#include "scopes.h"

extern const char *INT_TYPE;
extern const char *BOOL_TYPE;
extern const char *FLOAT_TYPE;
extern const char *STR_TYPE;
extern const char *INT_TYPE;
extern const char *BOOL_TYPE;
extern const char *FLOAT_TYPE;
extern const char *STR_TYPE;
extern const char *TRUE_TYPE;
extern const char *FALSE_TYPE;
extern const char *TYPEID_TYPE;

struct compiler {
	typedef std::vector<std::string> libs;
	typedef std::pair<atom, std::unique_ptr<llvm::Module>> llvm_module_t;
	typedef std::list<llvm_module_t> llvm_modules_t;

	compiler() = delete;
	compiler(const compiler &) = delete;
	compiler(std::string program_name, const libs &zion_paths);

	void resolve_module_filename(status_t &status, location location, std::string name, std::string &resolved);
	void info(const char *format, ...);

	module_scope_t::ref get_module_scope(atom module_key);
	void set_module_scope(atom module_key, module_scope_t::ref module_scope);

	std::vector<zion_token_t> get_comments() const;
	ptr<const ast::module> get_module(status_t &status, atom key_alias);
	void set_module(status_t &status, std::string filename, ptr<ast::module> module);
	llvm::Module *llvm_load_ir(status_t &status, std::string filename);
	llvm::Module *llvm_create_module(atom module_name);
	llvm::Module *llvm_get_program_module();

	/* testing */
	std::string dump_llvm_modules();
	std::string dump_program_text(atom module_name);

	void write_obj_file(status_t &status, std::unique_ptr<llvm::Module> &llvm_module);

	void setup_disk_environment(status_t &status);

	void build(status_t &status);
	void build_parse(status_t &status, location location, std::string module_name, bool global);
	void build_parse_linked(status_t &status, ptr<const ast::module> module);
	std::unordered_set<std::string> compile_modules(status_t &status);
	int emit_built_program(status_t &status, std::string bitcode_filename);
	int run_program(std::string bitcode_filename);

	program_scope_t::ref get_program_scope() const;
	std::string get_program_name() const;

	/* member variables */
private:
	std::unique_ptr<llvm::Module> &get_llvm_module(atom name);

	std::string program_name;
	ptr<std::vector<std::string>> zion_paths;
	std::vector<zion_token_t> comments;
	program_scope_t::ref program_scope;
	std::map<atom, ptr<const ast::module>> modules;
	llvm::LLVMContext llvm_context;
	llvm::IRBuilder<> builder;
	llvm_module_t llvm_program_module;
	llvm_modules_t llvm_modules;
	std::map<atom, ptr<module_scope_t>> module_scopes;

	friend bool _check_compiler_error(compiler &compiler, int &skipped);
};

std::string strip_zion_extension(std::string module_name);
