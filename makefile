IMAGE=zionlang/zion
VERSION=0.1
INSTALL_DIR=/usr/local/zion

UNAME := $(shell uname)
DEBUG_FLAGS := -DZION_DEBUG -g -O0

ifeq ($(UNAME),Darwin)
	CLANG = ccache clang-3.7
	CLANG_CPP = ccache clang++-3.7
	LLVM_CONFIG = llvm-config-3.7
	LLVM_CFLAGS = -nostdinc++ $(shell $(LLVM_CONFIG) --cxxflags) -g -O0

	CPP = $(CLANG_CPP) -g -O0 -std=c++11 -I /usr/include/c++/v1 -I$(shell $(LLVM_CONFIG) --includedir)/c++/v1
	CC = $(CLANG)
	LINKER = $(CLANG)
	LINKER_OPTS := \
		$(DEBUG_FLAGS) \
		$(shell $(LLVM_CONFIG) --ldflags) \
		-stdlib=libc++ \
		-lstdc++ \
		$(shell $(LLVM_CONFIG) --libs) \
		$(shell $(LLVM_CONFIG) --system-libs) \

	LINKER_DEBUG_OPTS := $(DEBUG_FLAGS)
else

ifeq ($(UNAME),Linux)
	CLANG := ccache clang-3.6
	CLANG_CPP := ccache clang++-3.6
	LLVM_CONFIG := /usr/local/opt/llvm37/bin/llvm-config-3.6
	LLVM_CFLAGS = -nostdinc++ $(shell $(LLVM_CONFIG) --cxxflags) -g -O0

	CPP = $(CLANG_CPP) -g -O0 -std=c++11 -I$(shell $(LLVM_CONFIG) --includedir)/llvm -I/usr/include/x86_64-linux-gnu/c++/4.9 -I/usr/include/c++/4.9
	CC = $(CLANG)
	LINKER = $(CLANG)
	LINKER_OPTS := \
		$(DEBUG_FLAGS) \
		$(shell $(LLVM_CONFIG) --ldflags) \
		-stdlib=libc++ \
		-lstdc++ \
		$(shell $(LLVM_CONFIG) --libs) \
		$(shell $(LLVM_CONFIG) --system-libs) \

	LINKER_DEBUG_OPTS := $(DEBUG_FLAGS)
endif

endif

VPATH = .:$(BUILD_DIR)
BUILD_DIR = build

CFLAGS := \
	-c \
	-Wall \
	-Werror \
	-pthread \
	-DZION_DEBUG \
	-g \
	-O0 \
	-fms-extensions \

ZION_LLVM_SOURCES = \
				main.cpp \
				signature.cpp \
				types.cpp \
				type_checker.cpp \
				type_refs.cpp \
				type_sum.cpp \
				var.cpp \
				ast.cpp \
				compiler.cpp \
				bound_type.cpp \
				bound_var.cpp \
				callable.cpp \
				dbg.cpp \
				disk.cpp \
				identifier.cpp \
				lexer.cpp \
				llvm_utils.cpp \
				llvm_test.cpp \
				llvm_types.cpp \
				location.cpp \
				logger.cpp \
				mmap_file.cpp \
				parse_state.cpp \
				parser.cpp \
				phase_scope_setup.cpp \
				render.cpp \
				scopes.cpp \
				status.cpp \
				tests.cpp \
				token.cpp \
				token_queue.cpp \
				unification.cpp \
				utils.cpp \
				unchecked_var.cpp \
				unchecked_type.cpp \
				variant.cpp \
				atom.cpp \
				json.cpp \
				json_lexer.cpp \
				json_parser.cpp \

ZION_LLVM_OBJECTS = $(addprefix $(BUILD_DIR)/,$(ZION_LLVM_SOURCES:.cpp=.llvm.o))
ZION_TARGET = zion
ZION_RUNTIME = \
				rt_int.c \
				rt_fn.c \
				rt_float.c \
				rt_str.c \
				rt_gc.c \

ZION_RUNTIME_LLIR = $(addprefix $(BUILD_DIR)/,$(ZION_RUNTIME:.c=.llir))

TARGETS = $(ZION_TARGET)

all: $(TARGETS) rt_gc

-include $(ZION_LLVM_OBJECTS:.o=.d)

rt_int.c: zion_rt.h

rt_float.c: zion_rt.h

rt_str.c: zion_rt.h

rt_gc.c: zion_rt.h

$(BUILD_DIR)/.gitignore:
	mkdir -p $(BUILD_DIR)
	echo "*" > $(BUILD_DIR)/.gitignore

value_semantics: build/value_semantics.o
	$(LINKER) $(LINKER_OPTS) $< -o value_semantics

.PHONY: test
test: zion
	ALL_TESTS=1 ./zion test

.PHONY: test-html
test-html: zion
	COLORIZE=1 ALL_TESTS=1 ./zion test \
		| ansifilter -o /var/tmp/zion-test.html --html -la -F 'Menlo' -e=utf-8
	open /var/tmp/zion-test.html

.PHONY: dbg
dbg: zion
	lldb -s .lldb-script -- ./zion test

$(ZION_TARGET): $(BUILD_DIR)/.gitignore $(ZION_LLVM_OBJECTS) $(ZION_RUNTIME_LLIR)
	@echo Linking $@
	$(LINKER) $(LINKER_OPTS) $(ZION_LLVM_OBJECTS) -o $@
	@echo $@ successfully built
	@ccache -s
	@du -hs $@ | cut -f1 | xargs echo Target \`$@\` is

$(BUILD_DIR)/%.e: %.cpp
	@echo Precompiling $<
	@$(CPP) $(CFLAGS) $(LLVM_CFLAGS) -E $< -o $@

$(BUILD_DIR)/%.llvm.o: %.cpp
	@echo Compiling $<
	@$(CPP) $(CFLAGS) $(LLVM_CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	@$(CPP) $(CFLAGS) $(LLVM_CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@echo Compiling $<
	@$(CPP) $(CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	@$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.llir: %.c
	@echo Emitting LLIR from $<
	@$(CLANG) -S -emit-llvm $< -o - | grep -v -e 'llvm\.ident' -e 'Apple LLVM version 6' > $@

rt_gc: rt_gc.c
	$(CLANG) -DRT_GC_TEST -g -std=c11 -Wall -O0 -mcx16 -pthread rt_gc.c -o rt_gc

clean:
	rm -rf $(BUILD_DIR)/* $(TARGETS)

image: Dockerfile
	docker build -t $(IMAGE):$(VERSION) .
	docker tag $(IMAGE):$(VERSION) $(IMAGE):latest

shell:
	docker run \
		--rm \
		--name zion-shell \
		-it $(IMAGE):$(VERSION) \
		bash

