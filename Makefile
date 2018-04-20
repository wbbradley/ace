IMAGE=zionlang/zion
VERSION=0.1
INSTALL_DIR=/usr/local/zion
OPT_LEVEL=-O0
UNAME := $(shell uname)
DEBUG_FLAGS := -DZION_DEBUG -g

CFLAGS = \
	-c \
	-Wall \
	-Werror \
	-Wno-narrowing \
	-pthread \
	$(OPT_LEVEL) \
	$(DEBUG_FLAGS) \
	-fms-extensions \

LLVM_CONFIG=$(LLVM_ROOT)/bin/llvm-config

ifeq ($(UNAME),Darwin)
	LLVM_ROOT = $(LLVM_RELEASE_ROOT)

	CLANG=ccache $(LLVM_ROOT)/bin/clang
	CPP=$(CLANG)++
	CC=$(CLANG)
	LINKER=$(CPP)
	STDCPP=c++14
	STDCPPLIB=-stdlib=libc++
	LLVM_VERSION = release_50
	LLVM_DEBUG_ROOT = $(HOME)/opt/llvm/$(LLVM_VERSION)/Debug
	LLVM_RELEASE_ROOT = $(HOME)/opt/llvm/$(LLVM_VERSION)/MinSizeRel
	PLATFORM_CPP_FLAGS = -I$(shell $(LLVM_CONFIG) --includedir)/c++/v1
endif

ifeq ($(UNAME),Linux)
	LLVM_ROOT=$(shell llvm-config --prefix)

	CPP=ccache g++
	CC=ccache gcc
	LINKER=$(CPP)
	STDCPP=c++14
	STDCPPLIB=
	PLATFORM_CPP_FLAGS = \
				 -I/usr/include/c++/5 \
				 -I/usr/include/x86_64-linux-gnu/c++/5 \
				 -I/usr/include/libcxxabi \
				 -Wno-unused-result

endif

CPP_FLAGS := $(CFLAGS) \
			  -nostdinc++ \
			  $(shell $(LLVM_CONFIG) --cxxflags | xargs -n 1 echo | grep -E "^-D|^-I|^-std|^-W[^l]" | sed -E 's/-Wno-maybe-uninitialized/-Wno-uninitialized/') \
			  $(PLATFORM_CPP_FLAGS) \
			  $(shell $(LLVM_CONFIG) --cppflags) \
			  -g \
			  $(OPT_LEVEL) \
			  -std=$(STDCPP) \
			  -I$(shell $(LLVM_CONFIG) --includedir) \
			  -fexceptions

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
				dump.cpp \
				fitting.cpp \
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
				tests.cpp \
				token.cpp \
				token_queue.cpp \
				type_checker.cpp \
				type_instantiation.cpp \
				type_parser.cpp \
				type_eval.cpp \
				types.cpp \
				unchecked_type.cpp \
				unchecked_var.cpp \
				unification.cpp \
				user_error.cpp \
				utils.cpp \
				var.cpp \
				zion_gc_lowering.cpp

ZION_LLVM_OBJECTS = $(addprefix $(BUILD_DIR)/,$(ZION_LLVM_SOURCES:.cpp=.o))
ZION_TARGET = zion
ZION_RUNTIME = \
				rt_posix.c \
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

.PHONY: test
test: expect-tests

.PHONY: unit-tests
unit-tests: $(ZION_TARGET)
	@echo "Executing unit tests..."
	time ./$(ZION_TARGET) test

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
	ALL_TESTS=1 gdb --args ./$(ZION_TARGET) test

$(ZION_TARGET): $(BUILD_DIR)/.gitignore $(ZION_LLVM_OBJECTS) $(ZION_RUNTIME_OBJECTS)
	@echo Linking $@ with linker "$(LINKER)"...
	$(LINKER) -v \
		$(OPT_LEVEL) \
		$(DEBUG_FLAGS) \
		$(shell $(LLVM_CONFIG) --ldflags) \
		$(ZION_LLVM_OBJECTS) \
		$(shell $(LLVM_CONFIG) --libs) \
		$(STDCPPLIB) \
		-std=$(STDCPP) \
		-lz \
		-lcurses \
		-o $@
	@echo $@ successfully built
	@ccache -s
	@du -hs $@ | cut -f1 | xargs echo Target \`$@\` is

$(BUILD_DIR)/%.o: src/%.cpp
	@$(CPP) $(CPP_FLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	@echo Compiling $<...
	@$(CPP) $(CPP_FLAGS) $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.c
	@-mkdir -p $(@D)
	@$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/%.c
	@$(CC) $(CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	@$(CC) $(CFLAGS) $< -o $@

%.o: src/%.c
	@$(CC) $(CFLAGS) $< -E -MMD -MP -MF $(patsubst %.o, %.d, $@) -MT $@ > /dev/null
	@$(CC) $(CFLAGS) $< -o $@

%.llir: %.c zion_rt.h
	@echo Emitting LLIR from $<
	@$(CC) -S -emit-llvm -g $< -o $@

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
