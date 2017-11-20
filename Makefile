IMAGE=zionlang/zion
VERSION=0.1
INSTALL_DIR=/usr/local/zion
OPT_LEVEL=-O0
UNAME := $(shell uname)
DEBUG_FLAGS := -DZION_DEBUG -g $(OPT_LEVEL)
CFLAGS = \
	-c \
	-Wall \
	-Werror \
	-Wno-narrowing \
	-pthread \
	$(DEBUG_FLAGS) \
	-fms-extensions \

ifeq ($(UNAME),Darwin)
	CPP = /opt/MinSizeRel/bin/clang++
	CLANG = /opt/MinSizeRel/bin/clang
	CC = $(CLANG)

	LLVM_CONFIG = /opt/debug/bin/llvm-config
	LLVM_CFLAGS = $(CFLAGS) \
				  -nostdinc++ \
				  $(shell $(LLVM_CONFIG) --cxxflags) \
				  -g \
				  $(OPT_LEVEL) \
				  -std=c++11 \
				  -I$(shell $(LLVM_CONFIG) --includedir)/c++/v1

	LINKER = $(CLANG)
	LINKER_OPTS := \
		$(DEBUG_FLAGS) \
		$(shell $(LLVM_CONFIG) --ldflags) \
		-stdlib=libc++ \
		-lstdc++ \
		$(shell $(LLVM_CONFIG) --cxxflags --ldflags --system-libs --libs)

	LINKER_DEBUG_OPTS := $(DEBUG_FLAGS)
	LLDB = /usr/local/opt/llvm/bin/lldb
else

ifeq ($(UNAME),Linux)
	CLANG_BIN = clang-3.9
	CLANG := $(CLANG_BIN)
	CPP := clang++-3.9
	LLVM_LINK_BIN = llvm-link-3.9
	LLVM_CONFIG = llvm-config-3.9
	LLVM_CFLAGS = $(CFLAGS) \
				  -nostdinc++ \
				  -I/usr/lib/llvm-3.9/include \
				  -I/usr/include/c++/5 \
				  -I/usr/include/x86_64-linux-gnu \
				  -I/usr/include/x86_64-linux-gnu/c++/5 \
				  -std=gnu++11 \
				  -gsplit-dwarf \
				  -fPIC \
				  -fvisibility-inlines-hidden \
				  -Wall \
				  -W \
				  -Wno-unused-parameter \
				  -Wwrite-strings \
				  -Wcast-qual \
				  -Wno-missing-field-initializers \
				  -pedantic \
				  -Wno-long-long \
				  -Wno-uninitialized \
				  -Wdelete-non-virtual-dtor \
				  -Wno-comment \
				  -Werror=date-time \
				  -ffunction-sections \
				  -fdata-sections \
				  $(OPT_LEVEL) \
				  -g \
				  -DNDEBUG \
				  -fno-exceptions \
				  -D_GNU_SOURCE \
				  -D__STDC_CONSTANT_MACROS \
				  -D__STDC_FORMAT_MACROS \
				  -D__STDC_LIMIT_MACROS \
				  -DLINUX

	# -I$(shell $(LLVM_CONFIG) --includedir)/llvm
	CPP = $(CPP)
	CPP_FLAGS = \
		  -I/usr/include/c++/v1 \
		  -g \
		  $(OPT_LEVEL) \
		  -fno-color-diagnostics \
		  -fno-caret-diagnostics
	CC = $(CLANG)
	LINKER = $(CLANG)

	LINKER_OPTS := \
		$(DEBUG_FLAGS) \
		$(shell $(LLVM_CONFIG) --ldflags) \
		-lstdc++ \
		$(shell $(LLVM_CONFIG) --cxxflags --ldflags --system-libs --libs)

	LINKER_DEBUG_OPTS := $(DEBUG_FLAGS)
	LLDB = lldb-3.9
endif

endif

VPATH = .:$(BUILD_DIR)
BUILD_DIR = build-$(UNAME)

ZION_LLVM_SOURCES = \
				main.cpp \
				signature.cpp \
				patterns.cpp \
				types.cpp \
				type_checker.cpp \
				type_instantiation.cpp \
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
				life.cpp \
				llvm_utils.cpp \
				llvm_test.cpp \
				llvm_types.cpp \
				location.cpp \
				logger.cpp \
				mmap_file.cpp \
				nil_check.cpp \
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
				zion_gc_lowering.cpp

ZION_LLVM_OBJECTS = $(addprefix $(BUILD_DIR)/,$(ZION_LLVM_SOURCES:.cpp=.llvm.o))
ZION_TARGET = zionc
ZION_RUNTIME = \
				rt_int.c \
				rt_fn.c \
				rt_float.c \
				rt_str.c \
				rt_typeid.c

ZION_RUNTIME_LLIR = $(ZION_RUNTIME:.c=.llir)

TARGETS = $(ZION_TARGET)

all: $(TARGETS)

-include $(ZION_LLVM_OBJECTS:.o=.d)

$(BUILD_DIR)/.gitignore:
	mkdir -p $(BUILD_DIR)
	echo "*" > $(BUILD_DIR)/.gitignore

value_semantics: $(BUILD_DIR)/value_semantics.o
	$(LINKER) $(LINKER_OPTS) $< -o value_semantics

.PHONY: test
test: unit-tests hello-world-test expect-tests

.PHONY: unit-tests
unit-tests: $(ZION_TARGET)
	@echo "Executing tests..."
	./$(ZION_TARGET) test

.PHONY: hello-world-test
hello-world-test: $(ZION_TARGET)
	@echo "Executing runtime test (test_hello_world)..."
	./$(ZION_TARGET) run test_hello_world


.PHONY: expect-tests
expect-tests: $(ZION_TARGET)
	@echo "Executing expect tests..."
	for f in tests/test_*.zion; do python expect.py -p $$f; done

.PHONY: test-html
test-html: $(ZION_TARGET)
	COLORIZE=1 ALL_TESTS=1 ./$(ZION_TARGET) test \
		| ansifilter -o /var/tmp/zion-test.html --html -la -F 'Menlo' -e=utf-8
	open /var/tmp/zion-test.html

.PHONY: dbg
dbg: $(ZION_TARGET)
	# ALL_TESTS=1 $(LLDB) -s .lldb-script -- ./$(ZION_TARGET) test
	ALL_TESTS=1 gdb --args ./$(ZION_TARGET) test

$(ZION_TARGET): $(BUILD_DIR)/.gitignore $(ZION_LLVM_OBJECTS) $(ZION_RUNTIME_LLIR)
	@echo Linking $@
	$(LINKER) $(LINKER_OPTS) $(ZION_LLVM_OBJECTS) -o $@
	@echo $@ successfully built
	@du -hs $@ | cut -f1 | xargs echo Target \`$@\` is

$(BUILD_DIR)/%.e: %.cpp
	@echo Precompiling $<
	@$(CPP) $(CPP_FLAGS) $(LLVM_CFLAGS) -E $< -o $@

$(BUILD_DIR)/%.llvm.o: %.cpp
	@echo Compiling $<
	@$(CPP) $(CPP_FLAGS) $(LLVM_CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	@$(CPP) $(CPP_FLAGS) $(LLVM_CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@echo Compiling $<
	@$(CPP) $(CPP_FLAGS) $(CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	@$(CC) $(CFLAGS) $< -o $@

%.llir: %.c zion_rt.h
	@echo Emitting LLIR from $<
	@$(CLANG) -S -emit-llvm -g $< -o $@

clean:
	rm -rf *.llir.ir $(BUILD_DIR)/* $(TARGETS)

image: Dockerfile
	docker build -t $(IMAGE):$(VERSION) .
	docker tag $(IMAGE):$(VERSION) $(IMAGE):latest

linux-test: image
	-docker kill zion-build
	-docker rm zion-build
	docker run \
		--rm \
		--name zion-build \
		-e LLVM_LINK_BIN=$(LLVM_LINK_BIN) \
		-e CLANG_BIN=$(CLANG_BIN) \
		-v `pwd`:/opt/zion \
		--privileged \
		-it $(IMAGE):$(VERSION) \
		make -j4 test

linux-shell: image
	-docker kill zion-shell
	-docker rm zion-shell
	docker run \
		--rm \
		--name zion-shell \
		-e LLVM_LINK_BIN=$(LLVM_LINK_BIN) \
		-e CLANG_BIN=$(CLANG_BIN) \
		-v `pwd`:/opt/zion \
		--privileged \
		-it $(IMAGE):$(VERSION) \
		bash
