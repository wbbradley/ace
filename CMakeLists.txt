cmake_minimum_required(VERSION 3.4.3)
project(Zion)

include_directories(BEFORE src)
add_definitions(-DZION_DEBUG)

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

add_compile_options(-std=c++14 -g -O0 ${option_flags})
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${LLVM_INSTALL_PREFIX}/lib")

add_executable(zion
	src/ast.cpp
	src/builtins.cpp
	src/compiler.cpp
	src/dbg.cpp
	src/defn_id.cpp
	src/disk.cpp
	src/env.cpp
	src/lower.cpp
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

add_test(
	NAME unit_tests
	COMMAND zion test)

add_test(
	NAME expect_tests
	COMMAND ./expect-tests.sh .)
