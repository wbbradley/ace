cmake_minimum_required(VERSION 3.4.3)
project(Zion)

include_directories(BEFORE src)
# add_definitions(-DZION_DEBUG)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM installation prefix is ${LLVM_INSTALL_PREFIX}")

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

add_compile_options(-std=c++14 -O3 ${option_flags})
# add_compile_options(-std=c++14 -g -O0 ${option_flags})
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${LLVM_INSTALL_PREFIX}/lib")

add_executable(zion
	src/ast.cpp
	src/builtins.cpp
	src/compiler.cpp
	src/context.cpp
	src/dbg.cpp
	src/defn_id.cpp
	src/disk.cpp
	src/env.cpp
	src/gen.cpp
	src/identifier.cpp
	src/infer.cpp
	src/lexer.cpp
	src/llvm_utils.cpp
	src/location.cpp
	src/logger.cpp
	src/main.cpp
	src/match.cpp
	src/parse_state.cpp
	src/parser.cpp
	src/patterns.cpp
	src/prefix.cpp
	src/resolver.cpp
	src/resolver_impl.cpp
	src/scope.cpp
	src/tests.cpp
	src/token.cpp
	src/token_queue.cpp
	src/translate.cpp
	src/typed_id.cpp
	src/types.cpp
	src/unification.cpp
	src/user_error.cpp
	src/utils.cpp
	)

message(STATUS "Using dynamic link options -L${LLVM_LIBRARY_DIR} -lLLVM")
target_link_libraries(zion -L${LLVM_LIBRARY_DIR} -lLLVM)

enable_testing()

# Test non-prelude based tests first
foreach(test
		test_let
		test_lambda
		test_let_lambda
		test_instance_deduction)
	add_test(
		NAME ${test}
		COMMAND ${CMAKE_SOURCE_DIR}/tests/expect-pass-no-prelude.sh ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${test})
endforeach(test)

# Explicitly test that assertions work as expected and cause a failure code in the built process
add_test(
	NAME test_failed_assert
	COMMAND ${CMAKE_SOURCE_DIR}/tests/test-assert-fail.sh ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR})

# Test that assertions work OK when passing
add_test(
	NAME test_assert
	COMMAND zion test_assert)

# Run built-in unit tests
add_test(
	NAME unit_tests
	COMMAND zion test)

foreach(test
	   	test_panic)
	add_test(
		NAME ${test}
		COMMAND ${CMAKE_SOURCE_DIR}/tests/expect-fail.sh ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${test})
endforeach(test)

foreach(test
	   	test_basic
	   	test_for_loop
	   	test_inferred_return_type
	   	test_match_int
	   	test_newtype
	   	test_newtype_tuple
	   	test_pass
	   	test_strings)
	add_test(
		NAME ${test}
		COMMAND ${CMAKE_SOURCE_DIR}/tests/expect-pass.sh ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${test})
endforeach(test)

option(TIDY "Compile with clang-tidy" OFF)
if(TIDY)
	find_program(
	  CLANG_TIDY_EXE
	  NAMES "clang-tidy"
	  DOC "Path to clang-tidy executable"
	  )

	if(NOT CLANG_TIDY_EXE)
	  message(STATUS "clang-tidy not found.")
	else()
	  message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
	  set(DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-checks=*,-clang-analyzer-alpha.*")
	endif()

	set_target_properties(
	  zion PROPERTIES
	  CXX_STANDARD 14
	  CXX_STANDARD_REQUIRED ON
	  COMPILE_FLAGS "${WARNING_FLAGS}"
	)

	if(CLANG_TIDY_EXE)
	  set_target_properties(
		zion PROPERTIES
		CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
	  )
	endif()
endif()
