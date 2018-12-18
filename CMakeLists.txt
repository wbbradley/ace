cmake_minimum_required(VERSION 3.4.3)
project(Zion)

include_directories(BEFORE src)
add_definitions(-DZION_DEBUG)

option(MEMORY_DEBUGGING "Track all allocations and spew to stdout about it" OFF)
if(MEMORY_DEBUGGING)
	set(option_flags -DMEMORY_DEBUGGING)
endif()

foreach(_rt_module
		rt_float
		rt_net
		rt_int
		rt_posix
		rt_str
		rt_typeid
		)
	add_custom_target(
		${_rt_module}.o
		ALL
		COMMAND clang -c ${CMAKE_SOURCE_DIR}/src/${_rt_module}.c -o ${PROJECT_BINARY_DIR}/${_rt_module}.o
		BYPRODUCTS ${PROJECT_BINARY_DIR}/${_rt_module}.o
		)
endforeach(_rt_module)

add_compile_options(-std=c++14 -g -O0 ${option_flags})

add_executable(zion
	src/ast.cpp
	src/atom.cpp
	src/binding.cpp
	src/builtins.cpp
	src/compiler.cpp
	src/dbg.cpp
	src/disk.cpp
	src/identifier.cpp
	src/lexer.cpp
	src/location.cpp
	src/logger.cpp
	src/main.cpp
	src/match.cpp
	src/mmap_file.cpp
	src/parse_state.cpp
	src/parser.cpp
	src/patterns.cpp
	src/scope.cpp
	src/tests.cpp
	src/token.cpp
	src/token_queue.cpp
	src/type_eval.cpp
	src/type_instantiation.cpp
	src/type_parser.cpp
	src/types.cpp
	src/unification.cpp
	src/user_error.cpp
	src/utils.cpp
	src/var.cpp
	)

enable_testing()

add_test(
	NAME unit_tests
	COMMAND zion test)

add_test(
	NAME expect_tests
	COMMAND ./expect-tests.sh .)
