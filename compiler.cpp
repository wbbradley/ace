#include "zion.h"
#include <stdarg.h>
#include "ast.h"
#include "compiler.h"
#include "lexer.h"
#include "parse_state.h"
#include <vector>
#include "disk.h"
#include <fstream>
#include <set>
#include "phase_scope_setup.h"
#include "utils.h"
#include "llvm_utils.h"
#include <sys/stat.h>
#include <iostream>

namespace llvm {
	FunctionPass *createZionGCLoweringPass(StructType *StackEntryTy, StructType *FrameMapTy);
}

std::string strip_zion_extension(std::string module_name) {
	if (ends_with(module_name, ".zion")) {
		/* as a courtesy, strip the extension from the filename here */
		return module_name.substr(0, module_name.size() - strlen(".zion"));
	} else {
		return module_name;
	}
}

compiler_t::compiler_t(std::string program_name_, const libs &zion_paths) :
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

	program_scope = program_scope_t::create(GLOBAL_SCOPE_NAME, *this, llvm_create_module(program_name_ + ".global"));
}

compiler_t::~compiler_t() {
	debug_above(12, std::cout << dump_llvm_modules());
}

void compiler_t::info(const char *format, ...) {
	va_list args;
	va_start(args, format);
	logv(log_info, format, args);
	va_end(args);
}

program_scope_t::ref compiler_t::get_program_scope() const {
	return program_scope;
}

std::vector<token_t> compiler_t::get_comments() const {
	return comments;
}

std::string compiler_t::get_program_name() const {
	return program_name;
}

std::string compiler_t::get_executable_filename() const {
	return program_name + ".zx";
}

void compiler_t::resolve_module_filename(
		status_t &status,
		location_t location,
		std::string name,
		std::string &resolved)
{
	std::string filename_test_resolution;
	if (real_path(name, filename_test_resolution)) {
		if (name == filename_test_resolution) {
			if (!ends_with(filename_test_resolution, ".zion")) {
				filename_test_resolution = filename_test_resolution + ".zion";
			}

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

void compiler_t::build_parse_linked(
		status_t &status,
		ptr<const ast::module_t> module,
		type_macros_t &global_type_macros)
{
	/* now, recursively make sure that all of the linked modules are parsed */
	for (auto &link : module->linked_modules) {
		auto linked_module_name = link->extern_module->get_canonical_name();
		build_parse(status, link->extern_module->token.location, linked_module_name,
			   	false /*global*/, global_type_macros);

		if (!status) {
			break;
		}
	}
}

ast::module_t::ref compiler_t::build_parse(
		status_t &status,
		location_t location,
		std::string module_name,
		bool global,
		type_macros_t &global_type_macros)
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
				assert(ends_with(module_filename, ".zion"));
				ifs.open(module_filename.c_str());

				if (ifs.good()) {
					debug_above(4, log(log_info, "parsing module " c_id("%s"), module_filename.c_str()));
					zion_lexer_t lexer({module_filename}, ifs);

					assert_implies(global, global_type_macros.size() == base_type_macros.size());

					if (global) {
						/* add std types to type_macros to ensure they are not
						 * rewritten by modules */
						std::string std_types[] = {
							"bool",
							"bytes",
							"true",
							"false",
							"int",
							"str",
							"float",
							"TypeID",
							"list",
						};

						for (auto std_type : std_types) {
							assert(global_type_macros.find(std_type) == global_type_macros.end());
							std::string new_name = std_type;
							global_type_macros.insert({std_type,
									type_id(make_iid_impl(new_name, INTERNAL_LOC()))});
						}
					}

					parse_state_t ps(status, module_filename, lexer, global_type_macros, &comments, &link_ins);
					auto module = ast::module_t::parse(ps, global);

                    if (!!status) {
                        set_module(status, module->filename, module);
						build_parse_linked(status, module, global_type_macros);

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
		ast::item_t::ref &program,
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
		user_error(status, location_t{llvm_module.getName().str(), 0, 0},
				"unable to find function " c_var("%s"), name_in_llir.c_str());
	} else {
		types::type_function_t::ref type = get_function_type(args, return_type);

		if (!!status) {
			/* see if this bound type already exists */
			auto bound_type = program_scope->get_bound_type(
					type->get_signature());

			if (bound_type == nullptr) {
				/* we haven't seen this bound type before, let's
				 * create it, and register it */
				bound_type = bound_type_t::create(
						type,
						location_t{llvm_module.getName().str(), 0, 0},
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
						make_iid(name)));
		}
	}
}

void add_global_types(
		status_t &status,
		compiler_t &compiler,
		llvm::IRBuilder<> &builder,
	   	program_scope_t::ref program_scope)
{
	/* let's add the builtin types to the program scope */
	std::vector<std::pair<std::string, bound_type_t::ref>> globals = {
		{{"nil"},
			bound_type_t::create(
					type_nil(),
					INTERNAL_LOC(),
					builder.getInt8Ty()->getPointerTo())},
		{{"void"},
			bound_type_t::create(
					type_id(make_iid("void")),
					INTERNAL_LOC(),
				   	builder.getVoidTy())},
		{{"*void"},
			bound_type_t::create(
					type_ptr(type_id(make_iid("void"))),
					INTERNAL_LOC(),
				   	builder.getInt8Ty()->getPointerTo())},
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
		{{CHAR_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(CHAR_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt8Ty())},
		{{UTF8_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(UTF8_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt8Ty())},
		{{FLOAT_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(FLOAT_TYPE)),
					INTERNAL_LOC(),
					builder.getDoubleTy())},
		{{BOOL_TYPE},
		   	bound_type_t::create(
					type_id(make_iid(BOOL_TYPE)),
				   	INTERNAL_LOC(),
				   	builder.getInt64Ty())},
		{{STR_TYPE},
		   	bound_type_t::create(
					type_ptr(type_id(make_iid(CHAR_TYPE))),
				   	INTERNAL_LOC(),
				   	builder.getInt8Ty()->getPointerTo())},
		{{PTR_TO_STR_TYPE},
		   	bound_type_t::create(
					type_ptr(type_ptr(type_id(make_iid(CHAR_TYPE)))),
				   	INTERNAL_LOC(),
				   	builder.getInt8Ty()->getPointerTo()->getPointerTo())},
		{{TYPEID_TYPE},
			bound_type_t::create(
					type_id(make_iid(TYPEID_TYPE)),
					INTERNAL_LOC(),
					builder.getInt32Ty())},
	};

	for (auto type_pair : globals) {
		program_scope->put_bound_type(status, type_pair.second);
		if (!status) {
			break;
		}
		compiler.base_type_macros[type_pair.first] = type_id(make_iid(type_pair.first));
	}
	compiler.base_type_macros[std::string{"vector"}] = type_id(make_iid("vector.vector"));

	debug_above(10, log(log_info, "%s", program_scope->str().c_str()));
}

void add_globals(
		status_t &status,
		compiler_t &compiler,
	   	llvm::IRBuilder<> &builder,
		program_scope_t::ref program_scope, 
		ast::item_t::ref program)
{
	auto llvm_module_int = compiler.llvm_load_ir(status, "rt_int.llir");
	auto llvm_module_float = compiler.llvm_load_ir(status, "rt_float.llir");
	auto llvm_module_str = compiler.llvm_load_ir(status, "rt_str.llir");
	auto llvm_module_typeid = compiler.llvm_load_ir(status, "rt_typeid.llir");

	/* set up the global scalar types, as well as memory reference and garbage
	 * collection types */
	add_global_types(status, compiler, builder, program_scope);
	assert(!!status);

	/* lookup the types of bool and void pointer for use below */
	bound_type_t::ref nil_type = program_scope->get_bound_type({"nil"});
	assert(nil_type != nullptr);

	bound_type_t::ref bool_type = program_scope->get_bound_type({BOOL_TYPE});
	assert(bool_type != nullptr);

	program_scope->put_bound_variable(
			status,
			"__true__",
			bound_var_t::create(INTERNAL_LOC(),
				"__true__",
				bool_type,
				builder.getInt64(1/*true*/), make_iid("__true__")));
	assert(!!status);

	program_scope->put_bound_variable(status, "__false__", bound_var_t::create(INTERNAL_LOC(), "__false__", bool_type, builder.getInt64(0/*false*/), make_iid("__false__")));
	assert(!!status);

	if (!!status) {
		struct binding_t {
			std::string name;
			llvm::Module *llvm_module;
			std::string name_in_llir;
			std::vector<std::string> args;
			std::string return_type;
		};

		// TODO: move all of these to std.zion as link defs

		/* add builtin functions to the program namespace. these functions are
		 * all defined in the rt_*.c files */
		auto bindings = std::vector<binding_t>{
			{INT_TYPE, llvm_module_int, "__int_int", {INT_TYPE}, INT_TYPE},
			{INT_TYPE, llvm_module_int, "__int_int32", {INT32_TYPE}, INT_TYPE},
			{INT32_TYPE, llvm_module_int, "__int32_int", {INT_TYPE}, INT32_TYPE},
			{INT_TYPE, llvm_module_int, "__int_int16", {INT16_TYPE}, INT_TYPE},
			{INT16_TYPE, llvm_module_int, "__int16_int", {INT_TYPE}, INT16_TYPE},
			{INT_TYPE, llvm_module_int, "__int_float", {FLOAT_TYPE}, INT_TYPE},
			{INT_TYPE, llvm_module_int, "__int_str", {STR_TYPE}, INT_TYPE},

			{FLOAT_TYPE, llvm_module_float, "__float_int", {INT_TYPE}, FLOAT_TYPE},
			{FLOAT_TYPE, llvm_module_float, "__float_float", {FLOAT_TYPE}, FLOAT_TYPE},
			{FLOAT_TYPE, llvm_module_float, "__float_str", {STR_TYPE}, FLOAT_TYPE},

			{"mem_dump", llvm_module_str, "mem_dump", {"*void", INT_TYPE}, "void"},
			{"__str__", llvm_module_str, "__str_int", {INT_TYPE}, STR_TYPE},
			{"__str__", llvm_module_str, "__str_int_radix", {INT_TYPE, INT_TYPE}, STR_TYPE},
			{"__str__", llvm_module_str, "__str_float", {FLOAT_TYPE}, STR_TYPE},
			{"__str__", llvm_module_str, "__str_type_id", {TYPEID_TYPE}, STR_TYPE},
			{"__str__", llvm_module_str, "__str_str", {STR_TYPE}, STR_TYPE},
			{"__getitem__", llvm_module_str, "__ptr_to_str_get_item", {PTR_TO_STR_TYPE, INT_TYPE}, STR_TYPE},

			{"__ineq__", llvm_module_typeid, "__type_id_ineq_type_id", {TYPEID_TYPE, TYPEID_TYPE}, BOOL_TYPE},

			{"concat",   llvm_module_str, "concat", {STR_TYPE, STR_TYPE}, STR_TYPE},
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
		};

		for (auto &binding : bindings) {
			/* lookup the types for the function type */
			bound_type_t::refs args;
			bound_type_t::ref return_type;
			for (auto arg : binding.args) {
				auto bound_type = program_scope->get_bound_type({arg});
				if (bound_type == nullptr) {
					debug_above(2, log("can't find bound type for %s",
								arg.c_str()));
					assert(false);
				}
				args.push_back(bound_type);
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

void compiler_t::build_parse_modules(status_t &status) {
	/* first just parse all the modules that are reachable from the initial module
	 * and bring them into our whole ast */
	auto module_name = program_name;

	assert(program == nullptr);

	/* create the program ast to contain all of the modules */
	program = ast::create<ast::program_t>({});

	/* set up global types and variables */
	add_globals(status, *this, builder, program_scope, program);

	if (!!status) {
		type_macros_t global_type_macros = base_type_macros;

		/* always include the standard library */
        if (getenv("NO_STD_LIB") == nullptr) {
            build_parse(status, location_t{std::string(GLOBAL_SCOPE_NAME) + " lib", 0, 0},
				   	"lib/std",
				   	true /*global*/,
				   	global_type_macros);
        }

		if (!!status) {
			/* now parse the main program module */
			main_module = build_parse(status, location_t{"command line build parameters", 0, 0},
					module_name, false /*global*/, global_type_macros);

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


void compiler_t::build_type_check_and_code_gen(status_t &status) {
	if (!!status) {
		/* set up the names that point back into the AST resolved to the right
		 * module scopes */
		status = scope_setup_program(*program, *this);

		if (!!status) {
			/* final and most complex pass to resolve all needed symbols in order to guarantee type constraints, and
			 * generate LLVM IR */
			type_check_program(status, builder, *program, *this);

			if (!!status) {
				debug_above(2, log(log_info, "type checking found no errors"));
				return;
			} else {
				debug_above(2, log(log_info, "type checking found errors"));
			}
		}
	}

	assert(!status);
}

std::string collect_filename_from_module_pair(
		status_t &status,
	   	const compiler_t::llvm_module_t &llvm_module_pair)
{
	std::ofstream ofs;
	std::string filename = llvm_module_pair.first + ".ir";

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

std::set<std::string> compiler_t::compile_modules(status_t &status) {
	if (!!status) {
		std::set<std::string> filenames;
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

void compiler_t::emit_built_program(status_t &status, std::string executable_filename) {
	std::vector<std::string> obj_files;
	emit_object_files(status, obj_files);
	if (!!status) {
		std::string clang_bin = getenv("LLVM_CLANG_BIN") ? getenv("LLVM_CLANG_BIN") : "/usr/bin/clang";
		if (clang_bin.size() == 0) {
			user_error(status, INTERNAL_LOC(), "cannot find clang! please specify it in an ENV var called LLVM_CLANG_BIN");
			return;
		}

		std::stringstream ss;
		ss << clang_bin << " -lc -lm";
		if (getenv("ZION_LINK") != nullptr) {
			ss << " " << getenv("ZION_LINK");
		}
	   	if (getenv("ARC4RANDOM_LIB") != nullptr) {
			ss << " -l" << getenv("ARC4RANDOM_LIB");
		}

		ss << " -Wno-override-module -Wall -g -O0 -mcx16";
		for (auto obj_file : obj_files) {
			ss << " " << obj_file;
		}
		for (auto link_in : link_ins) {
			ss << " " << unescape_json_quotes(link_in.text);
		}
		ss << " -o " << executable_filename;

		/* compile the bitcode into a local machine executable */
		errno = 0;
		int ret = system(ss.str().c_str());
		if (ret != 0) {
			user_error(status, location_t{}, "failure (%d) when running: %s",
					ret, ss.str().c_str());
		}

		struct stat s;
		assert(stat(executable_filename.c_str(), &s) == 0);

		return;
	}

	assert(!status);
	return;
}

void run_gc_lowering(
		llvm::Module *llvm_module,
		llvm::StructType *llvm_stack_frame_map_type,
		llvm::StructType *llvm_stack_entry_type)
{
	assert(llvm_module != nullptr);
	assert(llvm_stack_frame_map_type != nullptr);
	assert(llvm_stack_entry_type != nullptr);

	// Create a function pass manager.
	auto FPM = llvm::make_unique<llvm::legacy::FunctionPassManager>(llvm_module);

	// Add some optimizations.
	FPM->add(llvm::createZionGCLoweringPass(
				llvm_stack_entry_type,
				llvm_stack_frame_map_type));

	bool optimize = false;
	if (optimize) {
		// FPM->add(llvm::createInstructionCombiningPass());
		FPM->add(llvm::createFunctionInliningPass());
		// FPM->add(llvm::createReassociatePass());
		// FPM->add(llvm::createGVNPass());
		// FPM->add(llvm::createCFGSimplificationPass());
	}

	FPM->doInitialization();

	// Run the optimizations over all functions in the module being added to
	// the JIT.
	for (auto &F : *llvm_module) {
		FPM->run(F);
		F.setGC("shadow-stack");
	}
	debug_above(5, log("writing to jit.llir..."));
	FILE *fp = fopen("jit.llir", "wt");
	fprintf(fp, "%s\n", llvm_print_module(*llvm_module).c_str());
	fclose(fp);
}

void compiler_t::lower_program_module() {
	// Create the JIT.  This takes ownership of the module.
	llvm::Module *llvm_program_module = llvm_get_program_module();

	auto program_scope = get_program_scope();

	status_t status;
	llvm::LLVMContext &llvm_context = llvm_program_module->getContext();
	llvm::IRBuilder<> builder(llvm_context);
	auto bound_stack_frame_map_type = program_scope->get_runtime_type(
			status, builder, "stack_frame_map_t");
	assert(!!status);

	auto bound_stack_entry_type = program_scope->get_runtime_type(
			status, builder, "stack_entry_t");
	assert(!!status);

	run_gc_lowering(
			llvm_program_module,
			llvm::dyn_cast<llvm::StructType>(bound_stack_frame_map_type->get_llvm_specific_type()),
			llvm::dyn_cast<llvm::StructType>(bound_stack_entry_type->get_llvm_specific_type()));
}

int compiler_t::run_program(int argc, char *argv_input[]) {
	using namespace llvm;

	lower_program_module();

	llvm::Module *llvm_program_module = this->llvm_get_program_module();

	std::string error_str;
	auto llvm_engine = EngineBuilder(std::unique_ptr<llvm::Module>(llvm_program_module))
		.setErrorStr(&error_str)
		.setVerifyModules(true)
		.create();

	if (llvm_engine == nullptr) {
		fprintf(stderr, "Could not create ExecutionEngine: %s\n", error_str.c_str());
		exit(1);
	}

	while (llvm_modules.size() != 0) {
		std::unique_ptr<llvm::Module> llvm_module;

		/* grab the module from the list of included modules */
		std::swap(llvm_module, llvm_modules.back().second);

		/* make sure that the engine can find functions from this module */
		llvm_engine->addModule(std::move(llvm_module));
		// REVIEW: alternatively... 
		// llvm::Linker::linkModules(*llvm_program_module, std::move(llvm_module));

		/* remove this module from the list, now that we've transitioned it over to the engine */
		llvm_modules.pop_back();
	}

	/* prepare for dumping info about any crashes in the user's program */
	sys::PrintStackTraceOnErrorSignal(argv_input[0]);
	PrettyStackTraceProgram X(argc, argv_input);

	/* Call llvm_shutdown() on exit. */
	atexit(llvm_shutdown);

	/* find the standard library's main entry point. */
	auto llvm_fn_main = llvm_engine->FindFunctionNamed("__main__");
	assert(llvm_fn_main != nullptr);
	char **envp = (char**)malloc(sizeof(char**) * 1);
	envp[0] = nullptr;

	std::vector<std::string> argv;
	for (int i = 0; i < argc; ++i) {
		argv.push_back(argv_input[i]);
	}

	debug_above(5, log("writing to jit.llir..."));
	FILE *fp = fopen("jit.llir", "wt");
	fprintf(fp, "%s\n", llvm_print_module(*llvm_program_module).c_str());
	fclose(fp);

	/* finally, run the user's program */
	return llvm_engine->runFunctionAsMain(llvm_fn_main, argv, envp);
}

std::unique_ptr<llvm::MemoryBuffer> codegen(llvm::Module &module) {
	return nullptr;
}

void emit_object_file_from_module(status_t &status, llvm::Module *llvm_module, std::string Filename) {
	debug_above(2, log("Creating %s...", Filename.c_str()));
	using namespace llvm;
	auto TargetTriple = llvm::sys::getProcessTriple();
	llvm_module->setTargetTriple(TargetTriple);

	// Create the llvm_target
	std::string Error;
	auto llvm_target = TargetRegistry::lookupTarget(TargetTriple, Error);

	// Print an error and exit if we couldn't find the requested target.
	// This generally occurs if we've forgotten to initialise the
	// TargetRegistry or we have a bogus target triple.
	if (!llvm_target) {
		user_error(status, INTERNAL_LOC(), "%s", Error.c_str());
		return;
	}

	auto CPU = "generic";
	auto Features = "";
	TargetOptions opt;
	auto RM = Optional<Reloc::Model>();
	auto llvm_target_machine = llvm_target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

	llvm_module->setDataLayout(llvm_target_machine->createDataLayout());

	std::error_code EC;
	raw_fd_ostream dest(Filename, EC, sys::fs::F_None);

	if (EC) {
		user_error(status, INTERNAL_LOC(), "Could not open file: %s", EC.message().c_str());
		return;
	}


	legacy::PassManager pass;
	auto FileType = TargetMachine::CGFT_ObjectFile;

	if (llvm_target_machine->addPassesToEmitFile(pass, dest, FileType)) {
		user_error(status, INTERNAL_LOC(), "TargetMachine can't emit a file of this type");
		return;
	}

	pass.run(*llvm_module);
	dest.flush();
}

void compiler_t::emit_object_files(status_t &status, std::vector<std::string> &obj_files) {
	lower_program_module();

	while (llvm_modules.size() != 0) {
		std::unique_ptr<llvm::Module> llvm_module;

		/* grab the module from the list of included modules */
		std::swap(llvm_module, llvm_modules.back().second);

		std::string obj_file = (llvm_module->getName() + ".o").str();
		emit_object_file_from_module(status, llvm_module.operator->(), obj_file);
		if (!status) {
			return;
		}
		obj_files.push_back(obj_file);
		llvm_modules.pop_back();
	}
	auto program_obj_file = get_program_name() + ".o";
	emit_object_file_from_module(status, llvm_get_program_module(), program_obj_file);
	obj_files.push_back(program_obj_file);
	return;
}

std::unique_ptr<llvm::Module> &compiler_t::get_llvm_module(std::string name) {
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

void compiler_t::set_module(
		status_t &status,
		std::string filename,
		ptr<ast::module_t> module)
{
	assert(module != nullptr);
	assert(filename[0] = '/');
	module->module_key = compute_module_key(*zion_paths, filename);
	assert(module->filename == filename);

	debug_above(4, log(log_info, "setting syntax and scope for module (`%s`, `%s`) valid=%s",
				module->module_key.c_str(),
				module->filename.c_str(),
				boolstr(!!module)));

	if (!get_module(status, filename))  {
		/* add the module to the compiler's modules map */
		modules[filename] = module;
	} else {
		panic(string_format("module " C_FILENAME "%s" C_RESET " already exists!",
					filename.c_str()));
	}
}

ptr<const ast::module_t> compiler_t::get_module(status_t &status, std::string key_alias) {
	auto module_iter = modules.find(key_alias);
	if (module_iter != modules.end()) {
		auto module = module_iter->second;
		assert(module != nullptr);
		return module;
	} else {
		debug_above(4, log(log_info, "could not find valid module for " c_module("%s"),
				   	key_alias.c_str()));

		std::string module_filename;
		resolve_module_filename(status, INTERNAL_LOC(), key_alias, module_filename);

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

module_scope_t::ref compiler_t::get_module_scope(std::string module_key) {
    auto iter = module_scopes.find(module_key);
    if (iter != module_scopes.end()) {
        return iter->second;
    } else {
        return nullptr;
    }
}

void compiler_t::set_module_scope(std::string module_key, module_scope_t::ref module_scope) {
    assert(get_module_scope(module_key) == nullptr);
	assert(module_scope != nullptr);
    module_scopes[module_key] = module_scope;
}

std::string compiler_t::dump_llvm_modules() {
	return program_scope->dump_llvm_modules();
}

std::string compiler_t::dump_program_text(std::string module_name) {
	status_t status;
	auto module = get_module(status, module_name);
	if (!!status) {
		if (module != nullptr) {
			return module->str();
		} else {
			panic("this module does not exist");
			return "";
		}
	} else {
		not_impl();
		return "";
	}
}


llvm::Module *compiler_t::llvm_load_ir(status_t &status, std::string filename) {
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
		user_error(status, location_t{filename, 0, 0}, "%s", ss.str().c_str());
		return nullptr;
	} else {
		debug_above(9, log(log_info, "parsed module %s\n%s", filename.c_str(),
					llvm_print_module(*llvm_module).c_str()));
		return llvm_module;
	}
}

llvm::Module *compiler_t::llvm_create_module(std::string module_name) {
	llvm::LLVMContext &llvm_context = builder.getContext();
	if (llvm_program_module.second == nullptr) {
		/* only allow creating one program module */
		llvm_program_module = {
			module_name,
			std::unique_ptr<llvm::Module>(new llvm::Module(module_name, llvm_context))
		};
		return llvm_program_module.second.operator ->();
	} else {
		panic("we are using a single LLIR module per application");
		return nullptr;
	}
}

llvm::Module *compiler_t::llvm_get_program_module() {
	return llvm_program_module.second.operator ->();
}
