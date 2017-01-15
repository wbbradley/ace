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
#include <sys/stat.h>

std::string strip_zion_extension(std::string module_name) {
	if (ends_with(module_name, ".zion")) {
		/* as a courtesy, strip the extension from the filename here */
		return module_name.substr(0, module_name.size() - strlen(".zion"));
	} else {
		return module_name;
	}
}

compiler::compiler(std::string program_name_, const libs &zion_paths) :
	program_name(strip_zion_extension(program_name_)),
	zion_paths(make_ptr<std::vector<std::string>>()),
   	builder(llvm_context)
{
	for (auto lib_path : zion_paths) {
		std::string real_lib_path;
		if (real_path(lib_path, real_lib_path)) {
			this->zion_paths->push_back(real_lib_path);
		}
	}

	program_scope = program_scope_t::create("program", llvm_create_module(program_name_ + ".global"));
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

void compiler::resolve_module_filename(
		status_t &status,
		location location,
		std::string name,
		std::string &resolved)
{
	std::string filename_test_resolution;
	if (real_path(name, filename_test_resolution)) {
		if (name == filename_test_resolution) {
			if (file_exists(filename_test_resolution)) {
				/* short circuit if we're given a real path */
				resolved = filename_test_resolution;
				return;
			} else {
				panic(string_format("filename %s does not exist", name.c_str()));
			}
		}
	}

	std::string leaf_name = name + ".zion";
	std::string working_resolution;
	for (auto zion_path : *zion_paths) {
		auto test_path = zion_path + "/" + leaf_name;
		if (file_exists(test_path)) {
			std::string test_resolution;
			if (real_path(test_path, test_resolution)) {
				if (working_resolution.size() && working_resolution != test_resolution) {
					user_error(status, location, "multiple " C_FILENAME "%s"
							C_RESET " modules found with the same name in source "
							"path [%s, %s]", name.c_str(),
							working_resolution.c_str(),
							test_resolution.c_str());
				} else {
					working_resolution = test_resolution;
					debug_above(4, log(log_info, "searching for file %s, found it at %s",
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
			debug_above(4, log(log_info, "searching for file %s, did not find it at %s",
						name.c_str(),
						test_path.c_str()));
		}
	}

	if (working_resolution.size() != 0) {
		/* cool, we found one and only one module with the requested name in
		 * the source paths */
		resolved = working_resolution;
		return;
	} else {
		user_error(status, location, "module not found: " c_error("`%s`") " (Note that module names should not have .zion extensions.) Looked in ZION_PATH=[%s]",
				name.c_str(),
				join(*zion_paths, ":").c_str());
	}
}

void compiler::build_parse_linked(status_t &status, ptr<const ast::module> module) {
	/* now, recursively make sure that all of the linked modules are parsed */
	for (auto &link : module->linked_modules) {
		auto linked_module_name = link->extern_module->get_canonical_name();
		build_parse(status, link->extern_module->token.location, linked_module_name,
			   	false /*global*/);

		if (!status) {
			break;
		}
	}
}

ast::module::ref compiler::build_parse(
		status_t &status,
		location location,
		std::string module_name,
		bool global)
{
	std::string module_filename;
	resolve_module_filename(status, location, module_name, module_filename);

	// TODO: include some notion of versions
	if (!!status) {
		assert(module_filename.size() != 0);
		auto existing_module = get_module(status, module_filename);
		if (!!status) {
			if (existing_module == nullptr) {
				/* we found an unparsed file */
				std::ifstream ifs;
				ifs.open(module_filename.c_str());

				if (ifs.good()) {
					debug_above(4, log(log_info, "parsing module \"%s\"", module_filename.c_str()));
					zion_lexer_t lexer({module_filename}, ifs);
					parse_state_t ps(status, module_filename, lexer, &comments);
					auto module = ast::module::parse(ps, global);

					/* parse may have succeeded, either way add this module to
					 * our list of modules */
					set_module(status, module->filename.str(), module);
					if (!!status) {
						build_parse_linked(status, module);

						if (!!status) {
							return module;
						}
					}
				} else {
					user_error(status, location, "could not open \"%s\" when trying to link module",
							module_filename.c_str());
				}
			} else {
				debug_above(3, info("no need to build %s as it's already been linked in",
							module_name.c_str()));
				return existing_module;
			}
		} else {
			/* a failure */
			user_error(status, location, "failed to get module %s", module_name.c_str());
		}
	} else {
		/* no file, i guess */
	}

	assert(!status);
	return nullptr;
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
		types::type::ref type = get_function_type(
				type_variable(location{llvm_module.getName().str(), 0, 0}),
			   	args, return_type);

		if (!!status) {
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
				program_scope->put_bound_type(status, bound_type);
			}

			program_scope->put_bound_variable(
					status,
					name,
					bound_var_t::create(
						INTERNAL_LOC(),
						name,
						bound_type,
						llvm_function,
						make_iid(name),
						false/*is_lhs*/));
		}
	}
}

const char *INT_TYPE = "__int__";
const char *INT64_TYPE = "__int64__";
const char *INT32_TYPE = "__int32__";
const char *INT16_TYPE = "__int16__";
const char *INT8_TYPE = "__int8__";
const char *BOOL_TYPE = "__bool__";
const char *FLOAT_TYPE = "__float__";
const char *STR_TYPE = "__str__";
const char *TRUE_TYPE = "__true__";
const char *FALSE_TYPE = "__false__";
const char *TYPEID_TYPE = "__typeid__";

void add_global_types(
		status_t &status,
		llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope,
		llvm::Module *llvm_module_gc)
{
	llvm::Function *llvm_mark_fn_default = llvm_module_gc->getFunction("mark_fn_default");

	/* let's add the builtin types to the program scope */
	std::vector<std::pair<atom, bound_type_t::ref>> globals = {
		{{"void"},
		   	bound_type_t::create(
					type_id(make_iid("void")),
				   	INTERNAL_LOC(),
				   	builder.getVoidTy())},
		{{"module"},
		   	bound_type_t::create(
					type_id(make_iid("module")),
				   	INTERNAL_LOC(),
				   	builder.getVoidTy())},
		{{INT_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(INT_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt64Ty())},
		{{INT64_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(INT64_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt64Ty())},
		{{INT32_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(INT32_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt32Ty())},
		{{INT16_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(INT16_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt16Ty())},
		{{INT8_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(INT8_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt8Ty())},
		{{FLOAT_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(FLOAT_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getFloatTy())},
		{{BOOL_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(BOOL_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt64Ty())},
		{{STR_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(STR_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt8Ty()->getPointerTo())},

		/* pull in the garbage collection and memory reference types */
		{{"__tag_var"},
		   	bound_type_t::create(type_id(make_iid("__tag_var")),
				   	INTERNAL_LOC(),
				   	llvm_module_gc->getTypeByName("struct.tag_t"))},
		{{TYPEID_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(TYPEID_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt32Ty())},
		{{"__byte_count"},
		   	bound_type_t::create(
					type_id(make_iid("__byte_count")),
				   	INTERNAL_LOC(),
				   	builder.getInt64Ty())},
		{{"__var"},
		   	bound_type_t::create(
					type_id(make_iid("__var")),
				   	INTERNAL_LOC(),
				   	llvm_module_gc->getTypeByName("struct.var_t"))},
		{{"__next_var"},
		   	bound_type_t::create(
					type_id(make_iid("__next_var")),
				   	INTERNAL_LOC(),
				   	llvm_module_gc->getTypeByName("struct.next_var_t"))},
		{{"__var_ref"},
		   	bound_type_t::create(
					type_id(make_iid("__var_ref")),
				   	INTERNAL_LOC(),
				   	llvm_module_gc->getTypeByName("struct.var_t")->getPointerTo())},
		{{"nil"},
		   	bound_type_t::create(
					type_id(make_iid("nil")),
				   	INTERNAL_LOC(),
				   	llvm_module_gc->getTypeByName("struct.var_t")->getPointerTo())},
		{{"__unreachable"},
		   	bound_type_t::create(
					type_unreachable(),
				   	INTERNAL_LOC(),
				   	llvm_module_gc->getTypeByName("struct.var_t")->getPointerTo())},
		{{"__mark_fn"},
		   	bound_type_t::create(
					type_id(make_iid("__mark_fn")),
				   	INTERNAL_LOC(),
				   	llvm_mark_fn_default->getFunctionType()->getPointerTo())},
		{{"__bytes"},
		   	bound_type_t::create(
					type_id(make_iid("__bytes")),
				   	INTERNAL_LOC(),
				   	builder.getInt8Ty()->getPointerTo())},
	};

	for (auto type_pair : globals) {
		program_scope->put_bound_type(status, type_pair.second);
		if (!status) {
			break;
		}
	}
	debug_above(10, log(log_info, "%s", program_scope->str().c_str()));
}

void add_globals(
		status_t &status,
		compiler &compiler,
	   	llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope, 
		ast::item::ref program)
{
	auto llvm_module_int = compiler.llvm_load_ir(status, "rt_int.llir");
	auto llvm_module_float = compiler.llvm_load_ir(status, "rt_float.llir");
	auto llvm_module_str = compiler.llvm_load_ir(status, "rt_str.llir");
	auto llvm_module_gc = compiler.llvm_load_ir(status, "rt_gc.llir");
	auto llvm_module_typeid = compiler.llvm_load_ir(status, "rt_typeid.llir");

	/* set up the global scalar types, as well as memory reference and garbage
	 * collection types */
	add_global_types(status, builder, program_scope, llvm_module_gc);
	assert(!!status);

	/* lookup the types of bool and void pointer for use below */
	bound_type_t::ref nil_type = program_scope->get_bound_type({"nil"});
	bound_type_t::ref void_ptr_type = program_scope->get_bound_type({"__bytes"});
	bound_type_t::ref bool_type = program_scope->get_bound_type({BOOL_TYPE});
	bound_type_t::ref next_var_type = program_scope->get_bound_type({"__next_var"});


	program_scope->put_bound_variable(status, "__true__", bound_var_t::create(INTERNAL_LOC(), "__true__", bool_type, builder.getInt64(1/*true*/), make_iid("__true__"), false/*is_lhs*/));
	assert(!!status);

	program_scope->put_bound_variable(status, "__false__", bound_var_t::create(INTERNAL_LOC(), "__false__", bool_type, builder.getInt64(0/*false*/), make_iid("__false__"), false/*is_lhs*/));
	assert(!!status);

	/* get the nil pointer value cast as our __var_ref type */
	llvm::Type *llvm_nil_type = program_scope->get_bound_type({"__var_ref"})->get_llvm_type();
	llvm::Constant *llvm_nil_value = llvm::Constant::getNullValue(llvm_nil_type);
	program_scope->put_bound_variable(
			status, "nil", bound_var_t::create(INTERNAL_LOC(), "nil",
				nil_type, llvm_nil_value, make_iid("nil"),
				false /*is_lhs*/));
	assert(!!status);

	if (!!status) {
		struct binding_t {
			std::string name;
			llvm::Module *llvm_module;
			std::string name_in_llir;
			std::vector<std::string> args;
			std::string return_type;
		};

		auto bindings = std::vector<binding_t>{
			{INT_TYPE, llvm_module_int, "__int_int", {INT_TYPE}, INT_TYPE},
			{INT_TYPE, llvm_module_int, "__int_float", {FLOAT_TYPE}, INT_TYPE},
			{INT_TYPE, llvm_module_int, "__int_str", {STR_TYPE}, INT_TYPE},

			{FLOAT_TYPE, llvm_module_float, "__float_int", {INT_TYPE}, FLOAT_TYPE},
			{FLOAT_TYPE, llvm_module_float, "__float_float", {FLOAT_TYPE}, FLOAT_TYPE},
			{FLOAT_TYPE, llvm_module_float, "__float_str", {STR_TYPE}, FLOAT_TYPE},

			{STR_TYPE, llvm_module_str, "__str_int", {INT_TYPE}, STR_TYPE},
			{STR_TYPE, llvm_module_str, "__str_float", {FLOAT_TYPE}, STR_TYPE},
			{STR_TYPE, llvm_module_str, "__str_type_id", {TYPEID_TYPE}, STR_TYPE},
			{STR_TYPE, llvm_module_str, "__str_str", {STR_TYPE}, STR_TYPE},

			{"__ineq__", llvm_module_typeid, "__type_id_ineq_type_id", {TYPEID_TYPE, TYPEID_TYPE}, BOOL_TYPE},

			{"__plus__",   llvm_module_str, "__str_plus_str", {STR_TYPE, STR_TYPE}, STR_TYPE},
			{"__not__",   llvm_module_int, "__int_not", {INT_TYPE}, BOOL_TYPE},

			{"__plus__", llvm_module_int, "__int_plus_int", {INT_TYPE, INT_TYPE}, INT_TYPE},
			{"__minus__", llvm_module_int, "__int_minus_int", {INT_TYPE, INT_TYPE}, INT_TYPE},
			{"__times__", llvm_module_int, "__int_times_int", {INT_TYPE, INT_TYPE}, INT_TYPE},
			{"__divide__", llvm_module_int, "__int_divide_int", {INT_TYPE, INT_TYPE}, INT_TYPE},
			{"__mod__", llvm_module_int, "__int_modulus_int", {INT_TYPE, INT_TYPE}, INT_TYPE},

			/* bitmasking */
			{"__mask__", llvm_module_int, "__int_mask_int", {INT_TYPE, INT_TYPE}, INT_TYPE},

			{"__negative__", llvm_module_int, "__int_neg", {INT_TYPE}, INT_TYPE},
			{"__positive__", llvm_module_int, "__int_pos", {INT_TYPE}, INT_TYPE},

			{"__negative__", llvm_module_float, "__float_neg", {FLOAT_TYPE}, FLOAT_TYPE},
			{"__positive__", llvm_module_float, "__float_pos", {FLOAT_TYPE}, FLOAT_TYPE},

			{"__plus__", llvm_module_float, "__int_plus_float", {INT_TYPE, FLOAT_TYPE}, FLOAT_TYPE},
			{"__minus__", llvm_module_float, "__int_minus_float", {INT_TYPE, FLOAT_TYPE}, FLOAT_TYPE},
			{"__times__", llvm_module_float, "__int_times_float", {INT_TYPE, FLOAT_TYPE}, FLOAT_TYPE},
			{"__divide__", llvm_module_float, "__int_divide_float", {INT_TYPE, FLOAT_TYPE}, FLOAT_TYPE},

			{"__plus__", llvm_module_float, "__float_plus_int", {FLOAT_TYPE, INT_TYPE}, FLOAT_TYPE},
			{"__minus__", llvm_module_float, "__float_minus_int", {FLOAT_TYPE, INT_TYPE}, FLOAT_TYPE},
			{"__times__", llvm_module_float, "__float_times_int", {FLOAT_TYPE, INT_TYPE}, FLOAT_TYPE},
			{"__divide__", llvm_module_float, "__float_divide_int", {FLOAT_TYPE, INT_TYPE}, FLOAT_TYPE},

			{"__plus__", llvm_module_float, "__float_plus_float", {FLOAT_TYPE, FLOAT_TYPE}, FLOAT_TYPE},
			{"__minus__", llvm_module_float, "__float_minus_float", {FLOAT_TYPE, FLOAT_TYPE}, FLOAT_TYPE},
			{"__times__", llvm_module_float, "__float_times_float", {FLOAT_TYPE, FLOAT_TYPE}, FLOAT_TYPE},
			{"__divide__", llvm_module_float, "__float_divide_float", {FLOAT_TYPE, FLOAT_TYPE}, FLOAT_TYPE},
			{"__gt__", llvm_module_float, "__float_gt_float", {FLOAT_TYPE, FLOAT_TYPE}, BOOL_TYPE},
			{"__lt__", llvm_module_float, "__float_lt_float", {FLOAT_TYPE, FLOAT_TYPE}, BOOL_TYPE},
			{"__gte__", llvm_module_float, "__float_gte_float", {FLOAT_TYPE, FLOAT_TYPE}, BOOL_TYPE},
			{"__lte__", llvm_module_float, "__float_lte_float", {FLOAT_TYPE, FLOAT_TYPE}, BOOL_TYPE},

			{"__gt__", llvm_module_int, "__int_gt_int", {INT_TYPE, INT_TYPE}, BOOL_TYPE},
			{"__lt__", llvm_module_int, "__int_lt_int", {INT_TYPE, INT_TYPE}, BOOL_TYPE},
			{"__gte__", llvm_module_int, "__int_gte_int", {INT_TYPE, INT_TYPE}, BOOL_TYPE},
			{"__lte__", llvm_module_int, "__int_lte_int", {INT_TYPE, INT_TYPE}, BOOL_TYPE},
			{"__ineq__", llvm_module_int, "__int_ineq_int", {INT_TYPE, INT_TYPE}, BOOL_TYPE},
			{"__ineq__", llvm_module_float, "__float_ineq_float", {FLOAT_TYPE, FLOAT_TYPE}, BOOL_TYPE},
			{"__eq__", llvm_module_int, "__int_eq_int", {INT_TYPE, INT_TYPE}, BOOL_TYPE},
			{"__eq__", llvm_module_float, "__float_eq_float", {FLOAT_TYPE, FLOAT_TYPE}, BOOL_TYPE},
			{"__eq__", llvm_module_typeid, "__type_id_eq_type_id", {TYPEID_TYPE, TYPEID_TYPE}, BOOL_TYPE},
			{"__eq__", llvm_module_str, "__str_eq_str", {STR_TYPE, STR_TYPE}, BOOL_TYPE},

			{"__type_id_eq_type_id", llvm_module_typeid, "__type_id_eq_type_id", {TYPEID_TYPE, TYPEID_TYPE}, BOOL_TYPE},
			{"__int__", llvm_module_typeid, "__type_id_int", {TYPEID_TYPE}, INT_TYPE},

			{"__push_stack_var", llvm_module_gc, "push_stack_var", {"__var_ref"}, "void"},
			{"__pop_stack_var", llvm_module_gc, "pop_stack_var", {"__var_ref"}, "void"},
			{"__create_var", llvm_module_gc, "create_var", {STR_TYPE, "__mark_fn", TYPEID_TYPE, "__byte_count"}, "__var_ref"},
			{"__get_var_type_id", llvm_module_gc, "get_var_type_id", {"__var_ref"}, TYPEID_TYPE},
			{"__isnil", llvm_module_gc, "__isnil", {"__var_ref"}, BOOL_TYPE},
		};

		for (auto &binding : bindings) {
			/* lookup the types for the function type */
			bound_type_t::refs args;
			bound_type_t::ref return_type;
			for (auto arg : binding.args) {
				args.push_back(program_scope->get_bound_type({arg}));
			}
			return_type = program_scope->get_bound_type({binding.return_type});

			/* go ahead and bind this function to global scope overrides */
			rt_bind_var_from_llir(status, builder, program_scope, program, binding.name,
				*binding.llvm_module, binding.name_in_llir, args, return_type);
			if (!status) {
				break;
			}
		}
	}
}

void compiler::build_parse_modules(status_t &status) {
	/* first just parse all the modules that are reachable from the initial module
	 * and bring them into our whole ast */
	auto module_name = program_name;

	assert(program == nullptr);

	/* create the program ast to contain all of the modules */
	program = ast::create<ast::program>({});

	/* set up global types and variables */
	add_globals(status, *this, builder, program_scope, program);

	if (!!status) {
		/* always include the standard library */
        if (getenv("NO_STD_LIB") == nullptr) {
            build_parse(status, location{"std lib", 0, 0}, "lib/std", true /*global*/);
        }

		if (!!status) {
			/* now parse the main program module */
			main_module = build_parse(status, location{"command line build parameters", 0, 0},
					module_name, false /*global*/);

			if (!!status) {
				debug_above(4, log(log_info, "build_parse of %s succeeded", module_name.c_str(),
							false /*global*/));

				/* next, merge the entire set of modules into one program */
				for (const auto &module_data_pair : modules) {
					/* note the use of the set here to ensure that each module is only
					 * included once */
					auto module = module_data_pair.second;
					assert(module != nullptr);
					program->modules.insert(module);
				}
			}
		}
	}
}


void compiler::build_type_check_and_code_gen(status_t &status) {
	if (!!status) {
		/* set up the names that point back into the AST resolved to the right
		 * module scopes */
		status = scope_setup_program(*program, *this);

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

std::string collect_filename_from_module_pair(
		status_t &status,
	   	const compiler::llvm_module_t &llvm_module_pair)
{
	std::ofstream ofs;
	std::string filename = llvm_module_pair.first.str() + ".ir";

	debug_above(1, log(log_info, "opening %s...", filename.c_str()));
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
	} else {
		user_error(status, INTERNAL_LOC(), "failed to open file named %s to write LLIR data",
				filename.c_str());
	}
	return filename;
}

std::unordered_set<std::string> compiler::compile_modules(status_t &status) {
	if (!!status) {
		std::unordered_set<std::string> filenames;
		filenames.insert(collect_filename_from_module_pair(status, llvm_program_module));

		for (auto &llvm_module_pair : llvm_modules) {
			std::string filename = collect_filename_from_module_pair(status, llvm_module_pair);

			/* make sure we're not overwriting ourselves... probably need to fix this
			 * later */
			assert(filenames.find(filename) == filenames.end());
			filenames.insert(filename);
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
		debug_above(1, log(log_info, "running %s...", ss.str().c_str()));

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
				user_error(status, location{}, "failure (%d) when running: %s",
						ret, ss.str().c_str());
			}

			struct stat s;
			assert(stat(executable_filename.c_str(), &s) == 0);

			return ret;
		} else {
			user_error(status, location{}, "failure (%d) when running: %s",
					ret, ss.str().c_str());
			return ret;
		}
	}

	assert(!status);
	return -1;
}

int compiler::run_program(std::string bitcode_filename) {
	std::stringstream ss;
	ss << "lli-3.7 " << bitcode_filename;
	debug_above(1, log(log_info, "running %s...", ss.str().c_str()));
	return system(ss.str().c_str());
}

std::unique_ptr<llvm::Module> &compiler::get_llvm_module(atom name) {
	std::stringstream ss;
	ss << "did not find module " << name << " in [";
	const char *sep = "";
	if (name == llvm_program_module.first) {
		return llvm_program_module.second;
	}
	// TODO: don't use O(N)
	for (auto &pair : llvm_modules) {
		if (name == pair.first) {
			return pair.second;
		}
		ss << sep << pair.first << ": " << pair.second->getName().str();
		sep = ", ";
	}
	ss << "]";
	debug_above(1, log(log_warning, "%s", ss.str().c_str()));

	static std::unique_ptr<llvm::Module> hack;
	return hack;
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
		panic(string_format("could not find module filename in lib paths (%s)",
					filename.c_str()));
	}
	return working_key;
}

void compiler::set_module(
		status_t &status,
		std::string filename,
		ptr<ast::module> module)
{
	assert(module != nullptr);
	assert(filename[0] = '/');
	module->module_key = compute_module_key(*zion_paths, filename);
	assert(module->filename == filename);

	debug_above(4, log(log_info, "setting syntax and scope for module (`%s`, `%s`) valid=%s",
				module->module_key.c_str(),
				module->filename.str().c_str(),
				boolstr(!!module)));

	if (!get_module(status, filename))  {
		/* add the module to the compiler's modules map */
		modules[filename] = module;
	} else {
		panic(string_format("module " C_FILENAME "%s" C_RESET " already exists!",
					filename.c_str()));
	}
}

ptr<const ast::module> compiler::get_module(status_t &status, atom key_alias) {
	auto module_iter = modules.find(key_alias);
	if (module_iter != modules.end()) {
		auto module = module_iter->second;
		assert(module != nullptr);
		return module;
	} else {
		debug_above(4, log(log_info, "could not find valid module for " c_module("%s"),
				   	key_alias.c_str()));

		std::string module_filename;
		resolve_module_filename(status, INTERNAL_LOC(), key_alias.str(), module_filename);

		if (!!status) {
			auto module_iter = modules.find(module_filename);
			if (module_iter != modules.end()) {
				auto module = module_iter->second;
				assert(module != nullptr);
				return module;
			} else {
				return nullptr;
			}
		}
	}

	assert(!status);
	return nullptr;
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
	assert(module_scope != nullptr);
    module_scopes[module_key] = module_scope;
}

std::string compiler::dump_llvm_modules() {
	return program_scope->dump_llvm_modules();
}

std::string compiler::dump_program_text(atom module_name) {
	status_t status;
	auto module = get_module(status, module_name);
	if (!!status) {
		if (module != nullptr) {
			return module->str();
		} else {
			assert(!"this module does not exist");
			return "";
		}
	} else {
		not_impl();
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
		debug_above(9, log(log_info, "parsed module %s\n%s", filename.c_str(),
					llvm_print_module(*llvm_module).c_str()));
		return llvm_module;
	}
}

llvm::Module *compiler::llvm_create_module(atom module_name) {
	llvm::LLVMContext &llvm_context = builder.getContext();
	if (llvm_program_module.second == nullptr) {
		/* only allow creating one program module */
		llvm_program_module = {
			module_name,
			std::unique_ptr<llvm::Module>(new llvm::Module(module_name.str(), llvm_context))
		};
		return llvm_program_module.second.operator ->();
	} else {
		panic("we are using a single LLIR module per application");
		return nullptr;
	}
}

llvm::Module *compiler::llvm_get_program_module() {
	return llvm_program_module.second.operator ->();
}
