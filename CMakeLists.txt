cmake_minimum_required(VERSION 3.4.3)
project(Zion)

include_directories(BEFORE src)
add_definitions(-DZION_DEBUG)

option(MEMORY_DEBUGGING "Track all allocations and spew to stdout about it" OFF)
if(MEMORY_DEBUGGING)
	set(option_flags -DMEMORY_DEBUGGING)
endif()

add_compile_options(-std=c++14 -g -O0 ${option_flags})

add_executable(zion
	src/ast.cpp
	src/atom.cpp
	src/binding.cpp
	src/builtins.cpp
	src/compiler.cpp
	src/dbg.cpp
	src/disk.cpp
	src/env.cpp
	src/identifier.cpp
	src/infer.cpp
	src/lexer.cpp
	src/location.cpp
	src/logger.cpp
	src/main.cpp
	src/match.cpp
	src/mmap_file.cpp
	src/parse_state.cpp
	src/parser.cpp
	src/scope.cpp
	src/tests.cpp
	src/token.cpp
	src/token_queue.cpp
	src/types.cpp
	src/unification.cpp
	src/user_error.cpp
	src/utils.cpp
	)

enable_testing()

add_test(
	NAME unit_tests
	COMMAND zion test)

add_test(
	NAME expect_tests
	COMMAND ./expect-tests.sh .)
