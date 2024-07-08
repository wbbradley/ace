PN = ace

# TODO: ifeq ($(OS),Windows_NT)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	# Assume you are using homebrew for now on Mac
	LLVM_CONFIG ?= "/usr/local/opt/llvm@11/bin/llvm-config"
else
	LLVM_CONFIG ?= "llvm-config"
endif

BUILD_DIR ?= $(HOME)/var/$(PN)
SRCDIR = $(shell pwd)
LLVM_DIR ?= $(shell $(LLVM_CONFIG) --cmakedir)
prefix ?= /usr/local
BUILT_BINARY = $(BUILD_DIR)/ace
MAKEFLAGS=--no-print-directory

# Installation-related directories
installdir ?= $(DESTDIR)/$(prefix)
bindir ?= $(DESTDIR)/$(prefix)/bin
sharedir ?= $(DESTDIR)/$(prefix)/share/$(PN)
stdlibdir = $(sharedir)/lib
runtimedir = $(sharedir)/runtime
man1dir ?= $(DESTDIR)/$(prefix)/share/man/man1
manfile = $(man1dir)/$(PN).1
test_destdir ?= $(HOME)/var/ace-test

.PHONY: release
release:
	make DEBUG= ace

.PHONY: debug
debug:
	make DEBUG=1 ace

.PHONY: ace
ace:
	-@echo "Building Ace..."
	# make clean
	make install

.PHONY: uninstall
uninstall:
	-rm -rf $(sharedir)
	-rm $(bindir)/$(PN)
	-rm $(manfile)
	-rm $(bindir)/ace-tags

.PHONY: $(BUILT_BINARY)
$(BUILT_BINARY): $(BUILD_DIR)/Makefile
	@(cd $(BUILD_DIR) && make -j16 $(PN))

$(BUILD_DIR)/Makefile: export CMAKE_EXPORT_COMPILE_COMMANDS=1
$(BUILD_DIR)/Makefile: $(LLVM_DIR)/LLVMConfig.cmake CMakeLists.txt
	-mkdir -p $(BUILD_DIR)
	@if [ "$(DEBUG)" = "" ]; then \
		echo "Release mode..."; \
		(cd $(BUILD_DIR) && LLVM_DIR="$(LLVM_DIR)" cmake $(SRCDIR) -G 'Unix Makefiles') \
	else \
		echo "Debug mode..."; \
		(cd $(BUILD_DIR) && LLVM_DIR="$(LLVM_DIR)" cmake $(SRCDIR) -DDEBUG=ON -G 'Unix Makefiles') \
	fi

ACE_LIBS=$(shell cd lib && find *.ace)
RUNTIME_C_FILES=$(shell find runtime -regex '.*\.c$$')

.PHONY: install
install: $(BUILT_BINARY) $(addprefix $(SRCDIR)/lib/,$(ACE_LIBS)) $(RUNTIME_C_FILES) $(SRCDIR)/$(PN).1 ace-tags
	-echo "Installing Ace to ${DESTDIR}..."
	-echo "Making sure that various installation dirs exist..." 
	mkdir -p $(bindir)
	-rm -rf $(stdlibdir)
	mkdir -p $(stdlibdir)
	-rm -rf $(man1dir)
	mkdir -p $(man1dir)
	-rm -rf $(runtimedir)
	mkdir -p $(runtimedir)
	-echo "Copying compiler binary from $(BUILT_BINARY) to $(bindir)..."
	cp $(BUILT_BINARY) $(bindir)
	cp ./ace-tags $(bindir)
	for f in $(RUNTIME_C_FILES); do cp "$$f" "$(runtimedir)"; done
	cp $(addprefix $(SRCDIR)/lib/,$(ACE_LIBS)) $(stdlibdir)
	cp $(SRCDIR)/$(PN).1 $(man1dir)
	-test -x ./ace-link-to-src && ACE_ROOT=$(sharedir) ./ace-link-to-src

.PHONY: clean
clean:
	((cd $(BUILD_DIR)/.. && rm -rf $(notdir $(BUILD_DIR))) 2>/dev/null ||:)

.PHONY: install-test
install-test:
	-@rm -rf $(test_destdir)
	make DESTDIR=$(test_destdir) install

.PHONY: test
test:
	-@echo "Running Tests for Ace..."
	make $(BUILT_BINARY)
	$(BUILT_BINARY) unit-test
	make install-test
	@echo "ACE_ROOT=$(test_destdir)/$(prefix)/share/$(PN)"
	@ACE_ROOT="$(test_destdir)/$(prefix)/share/$(PN)" \
		"$(SRCDIR)/tests/run-tests.sh" \
		"$(test_destdir)/$(prefix)/bin" \
		"$(SRCDIR)"

.PHONY: format
format:
	clang-format -style=file -i src/*.cpp src/*.h

