IMAGE=zionlang/zion
VERSION=0.1
INSTALL_DIR=/usr/local/zion
OPT_LEVEL=-O3
UNAME := $(shell uname)
DEBUG_FLAGS := -DZION_NO_DEBUG -g $(OPT_LEVEL)
LLVM_VERSION = release_40
LLVM_DEBUG_ROOT = $(HOME)/opt/llvm/$(LLVM_VERSION)/Debug
LLVM_RELEASE_ROOT = $(HOME)/opt/llvm/$(LLVM_VERSION)/MinSizeRel

CFLAGS = \
	-c \
	-Wall \
	-Werror \
	-Wno-narrowing \
	-pthread \
	$(DEBUG_FLAGS) \
	-fms-extensions \

ifeq ($(UNAME),Darwin)
	CLANG = ccache $(LLVM_RELEASE_ROOT)/bin/clang
	CPP = $(CLANG)++
	CC = $(CLANG)

	LLVM_CONFIG = $(LLVM_DEBUG_ROOT)/bin/llvm-config
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
	CLANG_BIN = clang-4.0
	CLANG := $(CLANG_BIN)
	CPP := clang++-4.0
	LLVM_LINK_BIN = llvm-link-4.0
	LLVM_CONFIG = llvm-config-4.0
	LLVM_CFLAGS = $(CFLAGS) \
				  -nostdinc++ \
				  -I/usr/lib/llvm-4.0/include \
				  -I/usr/include/c++/5 \
				  -I/usr/include/x86_64-linux-gnu \
				  -I/usr/include/x86_64-linux-gnu/c++/5 \
				  -std=gnu++11 \
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
	LLDB = lldb-4.0
endif

endif

BUILD_DIR = build-$(UNAME)
VPATH = .:$(BUILD_DIR)

ZION_LLVM_SOURCES = \
				ast.cpp \
				atom.cpp \
				bound_type.cpp \
				bound_var.cpp \
				builtins.cpp \
				callable.cpp \
				coercions.cpp \
				compiler.cpp \
				dbg.cpp \
				disk.cpp \
				identifier.cpp \
				lexer.cpp \
				life.cpp \
				llvm_test.cpp \
				llvm_types.cpp \
				llvm_utils.cpp \
				location.cpp \
				logger.cpp \
				main.cpp \
				mmap_file.cpp \
				null_check.cpp \
				parse_state.cpp \
				parser.cpp \
				patterns.cpp \
				phase_scope_setup.cpp \
				render.cpp \
				scopes.cpp \
				signature.cpp \
				status.cpp \
				tests.cpp \
				token.cpp \
				token_queue.cpp \
				type_checker.cpp \
				type_instantiation.cpp \
				types.cpp \
				unchecked_type.cpp \
				unchecked_var.cpp \
				unification.cpp \
				utils.cpp \
				var.cpp \
				zion_gc_lowering.cpp

ZION_LLVM_OBJECTS = $(addprefix $(BUILD_DIR)/,$(ZION_LLVM_SOURCES:.cpp=.llvm.o))
ZION_TARGET = zion
ZION_RUNTIME = \
				rt_int.c \
				rt_float.c \
				rt_str.c \
				rt_typeid.c

ZION_RUNTIME_OBJECTS = $(ZION_RUNTIME:.c=.o)

TARGETS = $(ZION_TARGET)

all: $(TARGETS)

install: zion
	ln -s `pwd`/zion /usr/local/bin/zion

-include $(ZION_LLVM_OBJECTS:.o=.d)

$(BUILD_DIR)/.gitignore:
	mkdir -p $(BUILD_DIR)
	echo "*" > $(BUILD_DIR)/.gitignore

value_semantics: $(BUILD_DIR)/value_semantics.o
	$(LINKER) $(LINKER_OPTS) $< -o value_semantics

.PHONY: test
test: expect-tests

.PHONY: unit-tests
unit-tests: $(ZION_TARGET)
	@echo "Executing tests..."
	./$(ZION_TARGET) test

.PHONY: hello-world-test
hello-world-test: $(ZION_TARGET) unit-tests
	@echo "Executing runtime test (test_hello_world)..."
	./$(ZION_TARGET) run test_hello_world

/tmp/test_link_extern_entry.o: $(BUILD_DIR)/tests/test_link_extern_entry.o
	cp $< $@

.PHONY: expect-tests
expect-tests: $(ZION_TARGET) hello-world-test /tmp/test_link_extern_entry.o
	@echo "Executing expect tests..."
	./expect-tests.sh

.PHONY: test-html
test-html: $(ZION_TARGET)
	COLORIZE=1 ALL_TESTS=1 ./$(ZION_TARGET) test \
		| ansifilter -o /var/tmp/zion-test.html --html -la -F 'Menlo' -e=utf-8
	open /var/tmp/zion-test.html

.PHONY: dbg
dbg: $(ZION_TARGET)
	# ALL_TESTS=1 $(LLDB) -s .lldb-script -- ./$(ZION_TARGET) test
	ALL_TESTS=1 gdb --args ./$(ZION_TARGET) test

$(ZION_TARGET): $(BUILD_DIR)/.gitignore $(ZION_LLVM_OBJECTS) $(ZION_RUNTIME_OBJECTS)
	@echo Linking $@...
	$(LINKER) $(LINKER_OPTS) $(ZION_LLVM_OBJECTS) -o $@
	@echo $@ successfully built
	@ccache -s
	@du -hs $@ | cut -f1 | xargs echo Target \`$@\` is

$(BUILD_DIR)/%.e: src/%.cpp
	@echo Precompiling $<
	$(CPP) $(CPP_FLAGS) $(LLVM_CFLAGS) -E $< -o $@

$(BUILD_DIR)/%.llvm.o: src/%.cpp
	@echo Compiling $<
	@$(CPP) $(CPP_FLAGS) $(LLVM_CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	$(CPP) $(CPP_FLAGS) $(LLVM_CFLAGS) $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.c
	@-mkdir -p $(@D)
	@echo Compiling $<
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/%.c
	@echo Compiling $<
	@$(CLANG) $(CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	$(CLANG) $(CFLAGS) $< -o $@

%.o: src/%.c
	@echo Compiling $<
	@$(CLANG) $(CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	$(CLANG) $(CFLAGS) $< -o $@

%.llir: %.c zion_rt.h
	@echo Emitting LLIR from $<
	$(CLANG) -S -emit-llvm -g $< -o $@

clean:
	rm -rf *.llir.ir $(BUILD_DIR) tests/*.o *.o *.zx tests/*.zx *.a $(TARGETS)

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
