cmake_minimum_required(VERSION 3.4.3)
project(Ace)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#-----------------------------------------------------------------------------
# Enable ccache if not already enabled by symlink masquerading and if no other
# CMake compiler launchers are already defined
#-----------------------------------------------------------------------------
find_program(CCACHE_EXECUTABLE ccache)
mark_as_advanced(CCACHE_EXECUTABLE)
if(CCACHE_EXECUTABLE)
  foreach(LANG C CXX)
    if(NOT DEFINED CMAKE_${LANG}_COMPILER_LAUNCHER AND NOT CMAKE_${LANG}_COMPILER MATCHES ".*/ccache$")
      message(STATUS "Enabling ccache for ${LANG}")
      set(CMAKE_${LANG}_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE} CACHE STRING "")
    endif()
  endforeach()
endif()

include_directories(BEFORE src)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM installation prefix is ${LLVM_INSTALL_PREFIX}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

if (APPLE)
  add_compile_options(-Wno-unknown-warning-option)
  add_compile_options(-Wno-error=init-list-lifetime)
endif()

option(DEBUG "Build a Debug build" OFF)
if(DEBUG)
	message("Compiling in Debug mode...")
	add_definitions(-DACE_DEBUG)
	add_compile_options(-std=c++17 -g -O0 -Wall -Werror ${option_flags})
else()
	message("Compiling in Release mode...")
	add_compile_options(-std=c++17 -g -O3 -Wall -Werror ${option_flags})
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lpthread -std=c++17 -Wl,-rpath,${LLVM_INSTALL_PREFIX}/lib")

add_executable(ace
	src/ast.cpp
	src/builtins.cpp
	src/class_predicate.cpp
	src/checked.cpp
	src/compiler.cpp
	src/context.cpp
	src/constraint.cpp
	src/data_ctors_map.cpp
	src/dbg.cpp
	src/defn_id.cpp
	src/disk.cpp
	src/gen.cpp
  src/graph.cpp
	src/host.cpp
	src/identifier.cpp
	src/import_rules.cpp
	src/infer.cpp
	src/lexer.cpp
	src/link_ins.cpp
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
	src/scheme.cpp
	src/scheme_resolver.cpp
	src/scope.cpp
	src/solver.cpp
  src/tarjan.cpp
  src/testing.cpp
  src/tld.cpp
	src/token.cpp
	src/token_queue.cpp
	src/tracked_types.cpp
	src/translate.cpp
	src/typed_id.cpp
	src/types.cpp
	src/unification.cpp
	src/user_error.cpp
	src/utils.cpp
	)


message(STATUS "Using dynamic link options -L${LLVM_LIBRARY_DIR} -lLLVM")
target_link_libraries(ace -L${LLVM_LIBRARY_DIR} -lLLVM)


