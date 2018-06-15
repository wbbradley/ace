cmake_minimum_required(VERSION 3.4.3)
project(Zion)

include_directories(BEFORE src)
add_definitions(-DZION_DEBUG)

find_package(LLVM REQUIRED CONFIG
HINTS ${HOME}/opt/llvm/release_40/MinSizeRel/lib)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

option(MEMORY_DEBUGGING "Track all allocations and spew to stdout about it" OFF)
if(MEMORY_DEBUGGING)
	set(option_flags -DMEMORY_DEBUGGING)
endif()

option(DEBUG_LLVM_TYPES "Print out LLVM type information a lot" OFF)
if(DEBUG_LLVM_TYPES)
	set(option_flags -DDEBUG_LLVM_TYPES)
endif()


include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_compile_options(-std=c++14 -g -O3 ${option_flags})

add_executable(zion
	src/ast.cpp
	src/atom.cpp
	src/bound_type.cpp
	src/bound_var.cpp
	src/builtins.cpp
	src/callable.cpp
	src/coercions.cpp
	src/compiler.cpp
	src/dbg.cpp
	src/disk.cpp
	src/dump.cpp
	src/fitting.cpp
	src/identifier.cpp
	src/lexer.cpp
	src/life.cpp
	src/llvm_reader.cpp
	src/llvm_test.cpp
	src/llvm_types.cpp
	src/llvm_utils.cpp
	src/location.cpp
	src/logger.cpp
	src/main.cpp
	src/match.cpp
	src/mmap_file.cpp
	src/null_check.cpp
	src/parse_state.cpp
	src/parser.cpp
	src/patterns.cpp
	src/phase_scope_setup.cpp
	src/render.cpp
	src/scopes.cpp
	src/tests.cpp
	src/token.cpp
	src/token_queue.cpp
	src/type_checker.cpp
	src/type_eval.cpp
	src/type_instantiation.cpp
	src/type_parser.cpp
	src/types.cpp
	src/unchecked_type.cpp
	src/unchecked_var.cpp
	src/unification.cpp
	src/user_error.cpp
	src/utils.cpp
	src/var.cpp
	src/zion_gc_lowering.cpp
	)

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs all)
# engine interpreter support core irreader target jit)

# Link against LLVM libraries
target_link_libraries(zion ${llvm_libs})

enable_testing()

add_test(
	NAME unit_tests
	COMMAND ./zion test)

foreach(_test_name
		test_hello_world
		test_map
		test_mike
		test_recursion
		test_if_finalizer)
	add_test(NAME ${_test_name} COMMAND python expect.py -p ${_test_name})
endforeach(_test_name)
