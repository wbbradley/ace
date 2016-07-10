#include "zion.h"
#include "ast.h"
#include "compiler.h"
#include "lexer.h"
#include "parse_state.h"
#include <vector>
#include "disk.h"
#include <fstream>
#include "json.h"
#include <unordered_set>
#include "phase_scope_setup.h"
#include "utils.h"
#include "llvm_utils.h"

const std::string module_prefix = "module:";
const std::string file_prefix = "file:";

compiler::compiler(std::string program_name_, const libs &zion_paths) :
	program_name(program_name_),
	zion_paths(make_ptr<std::vector<std::string>>(zion_paths)),
   	builder(llvm_context)
{
	if (ends_with(program_name, ".zion")) {
		/* as a courtesy, strip the extension from the filename here */
		program_name = program_name.substr(0, program_name.size() - strlen(".zion"));
	}

	auto program_symbol = string_format("program-%s", program_name.c_str());
	program_scope = program_scope_t::create(program_symbol);
}

void compiler::info(const char *format, ...) {
	va_list args;
	va_start(args, format);
	logv(log_info, format, args);
	va_end(args);
}

program_scope_t::ref compiler::get_program_scope() const {
	return program_scope;
}

std::vector<zion_token_t> compiler::get_comments() const {
	return comments;
}

std::string compiler::get_program_name() const {
	return program_name;
}

void compiler::resolve_module_filename(status_t &status, location location, std::string name, std::string &resolved) {
	std::string leaf_name = name + ".zion";

	for (auto zion_path : *zion_paths) {
		auto test_path = zion_path + "/" + leaf_name;
		if (file_exists(test_path)) {
			resolved = test_path;
			debug_above(4, log(log_info, "searching for file %s, found it at %s",
						name.c_str(),
						test_path.c_str()));
			return;
		} else {
			debug_above(4, log(log_info, "searching for file %s, did not find it at %s",
						name.c_str(),
						test_path.c_str()));
		}
	}

	user_error(status, location, "module not found: " c_error("`%s`") " (Note that module names should not have .zion extensions.) Looked in ZION_PATH=[%s]",
			name.c_str(),
			join(*zion_paths, ":").c_str());
}

void compiler::build_parse_linked(status_t &status, ptr<const ast::module> module) {
	/* now, recursively make sure that all of the linked modules are parsed */
	for (auto &link : module->linked_modules) {
		auto linked_module_name = link->extern_module->get_canonical_name();
		build_parse(status, link->extern_module->token.location, linked_module_name);

		if (!status) {
			break;
		}
	}
}

void compiler::build_parse(status_t &status, location location, std::string module_name) {
	// TODO: include some notion of versions
	/* check whether this module has been parsed */
	auto module_build_state_check = get_module(module_prefix + module_name);
	if (!module_build_state_check) {
		/* this module has not been parsed, let's parse it */
		std::string module_filename;
		resolve_module_filename(status, location, module_name, module_filename);

		if (!!status) {
			/* we found a file */
			std::ifstream ifs;
			ifs.open(module_filename.c_str());

			if (ifs.good()) {
				debug_above(4, log(log_info, "parsing module \"%s\"", module_filename.c_str()));
				zion_lexer_t lexer({module_filename}, ifs);
				parse_state_t ps(status, module_filename, lexer, &comments);
				auto module = ast::module::parse(ps);

				/* parse may have succeeded, either way add this module to
				 * our list of modules */
				set_module(module_name, module->filename.str(), module);
				assert(!!get_module(file_prefix + module_filename));
				assert(!!get_module(module_prefix + module_name));
				build_parse_linked(status, module);
			} else {
				user_error(status, location, "could not open \"%s\" when trying to link module",
					   	module_filename.c_str());
			}
		} else {
			/* no file, i guess */
		}
	} else {
		info("no need to build %s as it's already been linked in",
				module_name.c_str());
	}
}

void rt_bind_var_from_llir(
		status_t &status,
		llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope,
		ast::item::ref &program,
		std::string name,
		llvm::Module &llvm_module,
		std::string name_in_llir,
		bound_type_t::refs args,
		bound_type_t::ref return_type)
{
	assert(return_type != nullptr);
	for (auto arg : args) {
		assert(arg);
	}

	/* bind this LLVM ir function to a particular variable name in this
	 * resolve_map */
	auto llvm_function = llvm_module.getFunction(name_in_llir);
	if (!llvm_function) {
		user_error(status, location{llvm_module.getName().str(), 0, 0},
				"unable to find function " c_var("%s"), name_in_llir.c_str());
	} else {
		types::term::ref term = get_function_term(args, return_type);

		/* this is putting a pointer to this function, and later we'll use the
		 * term to deduce how to call it */
		auto type = term->get_type();

		/* see if this bound type already exists */
		auto bound_type = program_scope->get_bound_type(
				type->get_signature());

		if (bound_type == nullptr) {
			/* we haven't seen this bound type before, let's
			 * create it, and register it */
			bound_type = bound_type_t::create(
					type,
					location{llvm_module.getName().str(), 0, 0},
					llvm_function->getType());
			program_scope->put_bound_type(bound_type);
		}

		program_scope->put_bound_variable(
				name,
				bound_var_t::create(
					INTERNAL_LOC(),
					name,
					bound_type,
					llvm_function,
					make_iid(name)));
	}
}

void add_global_types(
		llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope,
		llvm::Module *llvm_module_gc)
{
	llvm::Function *llvm_mark_fn_default = llvm_module_gc->getFunction("mark_fn_default");

	/* let's add the builtin types to the program scope */
	std::vector<std::pair<atom, bound_type_t::ref>> globals = {
		{{"void"}, bound_type_t::create(type_id(make_iid("void")), INTERNAL_LOC(), builder.getVoidTy())},
		{{"module"}, bound_type_t::create(type_id(make_iid("module")), INTERNAL_LOC(), builder.getVoidTy())},
		{{"int"}, bound_type_t::create(type_id(make_iid("int")), INTERNAL_LOC(), builder.getInt64Ty())},
		{{"float"}, bound_type_t::create(type_id(make_iid("float")), INTERNAL_LOC(), builder.getFloatTy())},
		{{"bool"}, bound_type_t::create(type_id(make_iid("bool")), INTERNAL_LOC(), builder.getInt1Ty())},
		{{"str"}, bound_type_t::create(type_id(make_iid("str")), INTERNAL_LOC(), builder.getInt8Ty()->getPointerTo())},

		/* pull in the garbage collection and memory reference types */
		{{"__tag_var"}, bound_type_t::create(type_id(make_iid("__tag_var")), INTERNAL_LOC(), llvm_module_gc->getTypeByName("struct.tag_t"))},
		{{"__type_id"}, bound_type_t::create(type_id(make_iid("__type_id")), INTERNAL_LOC(), builder.getInt32Ty())},
		{{"__byte_count"}, bound_type_t::create(type_id(make_iid("__byte_count")), INTERNAL_LOC(), builder.getInt64Ty())},
		{{"__var"}, bound_type_t::create(type_id(make_iid("__var")), INTERNAL_LOC(), llvm_module_gc->getTypeByName("struct.var_t"))},
		{{"__var_ref"}, bound_type_t::create(type_id(make_iid("__var_ref")), INTERNAL_LOC(), llvm_module_gc->getTypeByName("struct.var_t")->getPointerTo())},
		{{"__mark_fn"}, bound_type_t::create(type_id(make_iid("__mark_fn")), INTERNAL_LOC(), llvm_mark_fn_default->getFunctionType()->getPointerTo())},
		{{"__bytes"}, bound_type_t::create(type_id(make_iid("__bytes")), INTERNAL_LOC(), builder.getInt8Ty()->getPointerTo())},
	};

	for (auto type_pair : globals) {
		program_scope->put_bound_type(type_pair.second);
	}
	debug_above(9, log(log_info, "%s", program_scope->str().c_str()));
}

void add_globals(
		status_t &status,
		compiler &compiler,
	   	llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope, 
		ast::item::ref program)
{
	auto llvm_module_int = compiler.llvm_load_ir(status, "build/rt_int.llir");
	auto llvm_module_float = compiler.llvm_load_ir(status, "build/rt_float.llir");
	auto llvm_module_str = compiler.llvm_load_ir(status, "build/rt_str.llir");
	auto llvm_module_gc = compiler.llvm_load_ir(status, "build/rt_gc.llir");

	/* set up the global scalar types, as well as memory reference and garbage
	 * collection types */
	add_global_types(builder, program_scope, llvm_module_gc);

	/* lookup the types of bool and void pointer for use below */
	bound_type_t::ref void_ptr_type = program_scope->get_bound_type({"__bytes"});
	bound_type_t::ref bool_type = program_scope->get_bound_type({"bool"});

	/* get the null pointer value */
	llvm::Value *llvm_null_value = llvm::ConstantPointerNull::get(llvm::dyn_cast<llvm::PointerType>(void_ptr_type->llvm_type));
	assert(llvm_null_value != nullptr);

	program_scope->put_bound_variable("true", bound_var_t::create(INTERNAL_LOC(), "true", bool_type, builder.getTrue(), make_iid("true")));
	program_scope->put_bound_variable("false", bound_var_t::create(INTERNAL_LOC(), "false", bool_type, builder.getFalse(), make_iid("false")));
	program_scope->put_bound_variable(
			"null", bound_var_t::create(INTERNAL_LOC(), "null", void_ptr_type,
				llvm_null_value, make_iid("null")));

	if (!!status) {
		
		struct binding_t {
			std::string name;
			llvm::Module *llvm_module;
			std::string name_in_llir;
			std::vector<std::string> args;
			std::string return_type;
		};

		auto bindings = std::vector<binding_t>{
			{"int", llvm_module_int, "__int_int", {"int"}, "int"},
			{"int", llvm_module_int, "__int_float", {"float"}, "int"},
			{"int", llvm_module_int, "__int_str", {"str"}, "int"},

			{"float", llvm_module_float, "__float_int", {"int"}, "float"},
			{"float", llvm_module_float, "__float_float", {"float"}, "float"},
			{"float", llvm_module_float, "__float_str", {"str"}, "float"},

			{"str", llvm_module_str, "__str_int", {"int"}, "str"},
			{"str", llvm_module_str, "__str_float", {"float"}, "str"},
			{"str", llvm_module_str, "__str_str", {"str"}, "str"},

			{"+", llvm_module_int, "__int_plus_int", {"int", "int"}, "int"},
			{"-", llvm_module_int, "__int_minus_int", {"int", "int"}, "int"},
			{"*", llvm_module_int, "__int_times_int", {"int", "int"}, "int"},
			{"/", llvm_module_int, "__int_divide_int", {"int", "int"}, "int"},
			{"%", llvm_module_int, "__int_modulus_int", {"int", "int"}, "int"},

			/* bitmasking */
			{"mask", llvm_module_int, "__int_mask_int", {"int", "int"}, "int"},

			{"-", llvm_module_int, "__int_neg", {"int"}, "int"},
			{"+", llvm_module_int, "__int_pos", {"int"}, "int"},

			{"-", llvm_module_float, "__float_neg", {"float"}, "float"},
			{"+", llvm_module_float, "__float_pos", {"float"}, "float"},

			{"+", llvm_module_float, "__int_plus_float", {"int", "float"}, "float"},
			{"-", llvm_module_float, "__int_minus_float", {"int", "float"}, "float"},
			{"*", llvm_module_float, "__int_times_float", {"int", "float"}, "float"},
			{"/", llvm_module_float, "__int_divide_float", {"int", "float"}, "float"},

			{"+", llvm_module_float, "__float_plus_int", {"float", "int"}, "float"},
			{"-", llvm_module_float, "__float_minus_int", {"float", "int"}, "float"},
			{"*", llvm_module_float, "__float_times_int", {"float", "int"}, "float"},
			{"/", llvm_module_float, "__float_divide_int", {"float", "int"}, "float"},

			{"+", llvm_module_float, "__float_plus_float", {"float", "float"}, "float"},
			{"-", llvm_module_float, "__float_minus_float", {"float", "float"}, "float"},
			{"*", llvm_module_float, "__float_times_float", {"float", "float"}, "float"},
			{"/", llvm_module_float, "__float_divide_float", {"float", "float"}, "float"},

			{">", llvm_module_int, "__int_gt_int", {"int", "int"}, "int"},
			{"<", llvm_module_int, "__int_lt_int", {"int", "int"}, "int"},
			{">=", llvm_module_int, "__int_gte_int", {"int", "int"}, "int"},
			{"<=", llvm_module_int, "__int_lte_int", {"int", "int"}, "int"},
			{"!=", llvm_module_int, "__int_ineq_int", {"int", "int"}, "int"},
			{"==", llvm_module_int, "__int_eq_int", {"int", "int"}, "int"},

			{"__push_stack_var", llvm_module_gc, "push_stack_var", {"__var_ref"}, "void"},
			{"__pop_stack_var", llvm_module_gc, "pop_stack_var", {"__var_ref"}, "void"},
			{"__create_var", llvm_module_gc, "create_var", {"str", "__mark_fn", "__type_id", "__byte_count"}, "__var_ref"},
		};

		for (auto &binding : bindings) {
			/* lookup the types for the function term */
			bound_type_t::refs args;
			bound_type_t::ref return_type;
			for (auto arg : binding.args) {
				args.push_back(program_scope->get_bound_type({arg}));
			}
			return_type = program_scope->get_bound_type({binding.return_type});

			/* go ahead and bind this function to global scope overrides */
			rt_bind_var_from_llir(status, builder, program_scope, program, binding.name,
				*binding.llvm_module, binding.name_in_llir, args, return_type);
		}
#if 0
		auto type_num_binary_op =	
			type_overloads::create({
					get_function_term(get_args_term(Integer, Integer), Bool),
					get_function_term(get_args_term(Float, Float), Bool),
					});

		auto type_plus_ops =	
			type_overloads::create({
					get_function_term(get_args_term(Str, Str), Str),
					get_function_term(get_args_term(Integer, Integer), Integer),
					get_function_term(get_args_term(Integer), Integer),
					get_function_term(get_args_term(Float, Float), Float),
					get_function_term(get_args_term(Float, Integer), Float),
					get_function_term(get_args_term(Integer, Float), Float),
					get_function_term(get_args_term(Float), Float),
					});

		auto type_minus_ops =	
			type_overloads::create({
					get_function_term(get_args_term(Integer, Integer), Integer),
					get_function_term(get_args_term(Integer), Integer),
					get_function_term(get_args_term(Float, Float), Float),
					get_function_term(get_args_term(Float, Integer), Float),
					get_function_term(get_args_term(Integer, Float), Float),
					get_function_term(get_args_term(Float), Float),
					});

		vars["+"] = {"+", type_plus_ops};
		vars["-"] = {"-", type_minus_ops};
		vars[">"] = {">", type_num_binary_op};
		vars[">="] = {"<", type_num_binary_op};
		vars["<="] = {"<=", type_num_binary_op};
		vars["<"] = {">=", type_num_binary_op};

		auto type_equality_ops =	
			type_overloads::create({
					get_function_term(get_args_term(Integer, Integer), Bool),
					get_function_term(get_args_term(Float, Integer), Bool),
					get_function_term(get_args_term(Integer, Float), Bool),
					get_function_term(get_args_term(Bool, Bool), Bool),
					get_function_term(get_args_term(Str, Str), Bool),
					get_function_term(get_args_term(Str, Integer), Bool),
					get_function_term(get_args_term(Integer, Str), Bool),
					});

		vars["=="] = {"==", type_equality_ops};
		vars["!="] = {"!=", type_equality_ops};

		auto type_multiplicative_ops =	
			type_overloads::create({
					get_function_term(get_args_term(Integer, Integer), Integer),
					get_function_term(get_args_term(Float, Integer), Float),
					get_function_term(get_args_term(Integer, Float), Float),
					get_function_term(get_args_term(Float, Float), Float),
					});

		vars["/"] = {"/", type_multiplicative_ops};
		vars["*"] = {"*", type_multiplicative_ops};
		vars["%"] = {"%", get_function_term(get_args_term(Integer, Integer), Integer)};

		vars["__print__"] = {"__print__", get_function_term(get_args_term(Str), Void)};
#endif
	}
}

void compiler::build(status_t &status) {
	/* first just parse all the modules that are reachable from the initial module
	 * and bring them into our whole ast */
	auto module_name = program_name;

	build_parse(status, location{"command line build parameters", 0, 0}, module_name);

	if (!!status) {
		debug_above(4, log(log_info, "build_parse of %s succeeded", module_name.c_str()));

		/* create the program ast to contain all of the modules */
		auto program = ast::create<ast::program>({});

		/* always include the standard library */
		build_parse(status, location{"default include", 0, 0}, "std");

		/* next, merge the entire set of modules into one program */
		for (const auto &module_data_pair : modules) {
			/* note the use of the set here to ensure that each module is only
			 * included once */
			auto module = module_data_pair.second;
			program->modules.insert(module);
		}

		/* set up the names that point back into the AST resolved to the right
		 * module scopes */
		status = scope_setup_program(*program, *this);

		if (!!status) {
			/* set up global types and variables */
			add_globals(status, *this, builder, program_scope, program);

			if (!!status) {
				status |= type_check_program(builder, *program, *this);

				if (!!status) {
					debug_above(2, log(log_info, "type checking found no errors"));
					return;
				} else {
					debug_above(2, log(log_info, "type checking found errors"));
				}
			}
		}
	}
}

std::unordered_set<std::string> compiler::compile_modules(status_t &status) {
	if (!!status) {
		std::unordered_set<std::string> filenames;
		for (auto &llvm_module_pair : llvm_modules) {
			std::ofstream ofs;
			std::string filename = llvm_module_pair.first.str() + ".ir";

			/* make sure we're not overwriting ourselves... probably need to fix this
			 * later */
			assert(filenames.find(filename) == filenames.end());
			filenames.insert(filename);

			log(log_info, "opening %s...", filename.c_str());
			ofs.open(filename.c_str());
			if (ofs.good()) {
				llvm::raw_os_ostream os(ofs);
				llvm_module_pair.second->setTargetTriple(LLVMGetDefaultTargetTriple());

				// TODO: set the data layout string to whatever llvm-link wants
				// llvm_module_pair.second->setDataLayout(...);

				status_t status;
				llvm_verify_module(status, *llvm_module_pair.second);
				if (!!status) {
					llvm_module_pair.second->print(os, nullptr /*AssemblyAnnotationWriter*/);
					os.flush();
				}
			}
		}
		return filenames;
	}

	assert(!status);
	return {};
}

int compiler::emit_built_program(status_t &status, std::string executable_filename) {
	std::unordered_set<std::string> filenames;
	if (!!status) {
		filenames = compile_modules(status);
	}

	if (!!status) {
		std::string bitcode_filename = executable_filename + ".bc";

		std::stringstream ss;
		ss << "llvm-link-3.7 -suppress-warnings";
		for (auto filename : filenames) {
			ss << " " << filename;
		}
		ss << " -o " << bitcode_filename;
		log(log_info, "running %s...", ss.str().c_str());

		/* link the .llir files together into a bitcode file */
		errno = 0;
		int ret = system(ss.str().c_str());
		if (ret == 0) {
			ss.str("");
			ss << "clang-3.7 -Wno-override-module -std=c11 -Wall -O0 -mcx16 -pthread ";
			ss << bitcode_filename << " -o " << executable_filename;

			/* compile the bitcode into a local machine executable */
			errno = 0;
			int ret = system(ss.str().c_str());
			if (ret != 0) {
				user_error(status, location{}, "failure (%d) when running: %s", ss.str().c_str());
			}

			return ret;
		} else {
			user_error(status, location{}, "failure (%d) when running: %s", ss.str().c_str());
			return ret;
		}
	}

	assert(!status);
	return -1;
}

int compiler::run_program(std::string bitcode_filename) {
	std::stringstream ss;
	ss << "lli-3.7 " << bitcode_filename;
	log(log_info, "running %s...", ss.str().c_str());
	return system(ss.str().c_str());
}

std::unique_ptr<llvm::Module> &compiler::get_llvm_module(atom name) {
	std::stringstream ss;
	ss << "did not find module " << name << " in [";
	const char *sep = "";

	// TODO: don't use O(N)
	for (auto &pair : llvm_modules) {
		if (name == pair.first) {
			return pair.second;
		}
		ss << sep << pair.first << ": " << pair.second->getName().str();
		sep = ", ";
	}
	ss << "]";
	log(log_warning, "%s", ss.str().c_str());

	static std::unique_ptr<llvm::Module> hack;
	return hack;
}

void compiler::set_module(
		std::string module_name,
		std::string filename,
		ptr<ast::module> module)
{
	atom module_key = module_prefix + module_name;
	atom filename_key = file_prefix + filename;

	module->module_key = module_key;
	module->filename_key = filename_key;

	debug_above(4, log(log_info, "setting syntax and scope for module (`%s`, `%s`) valid=%s",
				module_key.str().c_str(), filename_key.str().c_str(),
				boolstr(!!module)));

	if (!get_module(module_key) && !get_module(filename_key))  {
		/* add the module to the compiler's modules map */
		modules[module_key] = module;
		modules[filename_key] = module;
	} else {
		panic(string_format("module (`%s`,`%s`) already exists!", module_name.c_str(),
					filename.c_str()));
	}
}

ptr<const ast::module> compiler::get_module(atom key_alias) {
	auto module = modules[key_alias];

	if (!module) {
		debug_above(4, log(log_warning, "could not find valid module for " c_module("%s"), key_alias.c_str()));
		static const std::vector<std::string> valid_module_lookup_prefixes = {
			module_prefix,
			file_prefix,
		};

		for (auto &prefix : valid_module_lookup_prefixes) {
			if (starts_with(key_alias, prefix)) {
				return module;
			}
		}

		panic(string_format("get_module called with `%s`, must use one of these prefixes %s",
					key_alias.c_str(),
					join(valid_module_lookup_prefixes, ", ").c_str()));
		return {};
	}

	debug_above(4, log(log_info, "found valid module for %s", key_alias.c_str()));
	return module;
}

module_scope_t::ref compiler::get_module_scope(atom module_key) {
    auto iter = module_scopes.find(module_key);
    if (iter != module_scopes.end()) {
        return iter->second;
    } else {
        return nullptr;
    }
}

void compiler::set_module_scope(atom module_key, module_scope_t::ref module_scope) {
    assert(get_module_scope(module_key) == nullptr);
    module_scopes[module_key] = module_scope;
}

std::string compiler::dump_llvm_modules() {
	return program_scope->dump_llvm_modules();
}

std::string compiler::dump_program_text(atom module_name) {
	atom module_key = module_prefix + module_name;
    auto iter = modules.find(module_key);
	if (iter != modules.end()) {
		if (iter->second) {
			return iter->second->str();
		} else {
			log(log_warning, "no module " c_module("%s") " found", module_name.c_str());
			return "";
		}
	} else {
		assert(!"this module does not exist");
		return "";
	}
}


llvm::Module *compiler::llvm_load_ir(status_t &status, std::string filename) {
	llvm::LLVMContext &llvm_context = builder.getContext();
	llvm::SMDiagnostic err;
	llvm_modules.push_back({filename, parseIRFile(filename, err, llvm_context)});

	auto *llvm_module = llvm_modules.back().second.operator ->();
	if (llvm_module == nullptr) {
		llvm_modules.pop_back();

		/* print the diagnostic error messages */
		std::stringstream ss;
		llvm::raw_os_ostream os(ss);
		err.print("zion", os);
		os.flush();

		/* report the error */
		user_error(status, location{filename, 0, 0}, "%s", ss.str().c_str());
		return nullptr;
	} else {
		debug_above(4, log(log_info, "parsed module %s\n%s", filename.c_str(),
					llvm_print_module(*llvm_module).c_str()));
		return llvm_module;
	}
}

llvm::Module *compiler::llvm_create_module(atom module_name) {
	llvm::LLVMContext &llvm_context = builder.getContext();
	llvm_modules.push_front({
			module_name,
		   	std::unique_ptr<llvm::Module>(new llvm::Module(module_name.str(), llvm_context))
	});

	return llvm_modules.front().second.operator ->();
}
