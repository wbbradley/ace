cmake_minimum_required(VERSION 3.4.3)
project(Zion)

include_directories(BEFORE .)
add_definitions(-DZION_DEBUG)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Set your project compile flags.
# E.g. if using the C++ header files
# you will need to enable C++11 support
# for your compiler.

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
add_compile_options(-std=gnu++11 -g -O0)

# add build steps for each of the runtime modules
foreach(_rt_module
		rt_float
		rt_int
		rt_ref
		rt_str
		rt_typeid)

	add_custom_command(
		OUTPUT ${_rt_module}.llir
		COMMAND clang -S -Wall -emit-llvm ${_rt_module}.c -o ${_rt_module}.llir
		DEPENDS ${_rt_module}.c)

endforeach(_rt_module)

add_executable(zionc
	rt_float.llir
	rt_int.llir
	rt_ref.llir
	rt_str.llir
	rt_typeid.llir
	ast.cpp
	atom.cpp
	bound_type.cpp
	bound_var.cpp
	callable.cpp
	compiler.cpp
	dbg.cpp
	disk.cpp
	identifier.cpp
	json.cpp
	json_lexer.cpp
	json_parser.cpp
	lexer.cpp
	life.cpp
	llvm_test.cpp
	llvm_types.cpp
	llvm_utils.cpp
	location.cpp
	logger.cpp
	main.cpp
	main.cpp
	mmap_file.cpp
	parse_state.cpp
	parser.cpp
	patterns.cpp
	phase_scope_setup.cpp
	render.cpp
	scopes.cpp
	signature.cpp
	status.cpp
	tests.cpp
	token.cpp
	token_queue.cpp
	type_checker.cpp
	type_instantiation.cpp
	types.cpp
	unchecked_type.cpp
	unchecked_var.cpp
	unification.cpp
	utils.cpp
	var.cpp
	variant.cpp
	)

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs support core irreader mcjit)

# Link against LLVM libraries
target_link_libraries(zionc ${llvm_libs})

enable_testing()

add_test(
	NAME unit_tests
	COMMAND ./zionc test)

foreach(_test_name
		test_hello_world
		test_recursion
		test_mike)
	add_test(
		NAME ${_test_name}
		COMMAND ./zionc run ${_test_name})
endforeach(_test_name)
