PN = zion
BUILD_DIR ?= $(HOME)/var/$(PN)
SRCDIR = $(shell pwd)
LLVM_DIR ?= $(shell llvm-config --cmakedir)
prefix ?= /usr/local
BUILT_BINARY = $(BUILD_DIR)/zion
MAKEFLAGS=--no-print-directory

# Installation-related directories
installdir = $(DESTDIR)/$(prefix)
bindir = $(DESTDIR)/$(prefix)/bin
sharedir = $(DESTDIR)/$(prefix)/share/$(PN)
stdlibdir = $(sharedir)/lib
runtimedir = $(sharedir)/runtime
man1dir = $(DESTDIR)/$(prefix)/share/man/man1

test_destdir ?= $(HOME)/var/zion-test

.PHONY: release
release:
	make DEBUG= zion

.PHONY: debug
debug:
	make DEBUG=1 zion

.PHONY: zion
zion:
	-@echo "Building Zion..."
	make clean
	make install

.PHONY: $(BUILT_BINARY)
$(BUILT_BINARY): $(BUILD_DIR)/Makefile
	@(cd $(BUILD_DIR) && make -j16 $(PN))

$(BUILD_DIR)/Makefile: $(LLVM_DIR)/LLVMConfig.cmake CMakeLists.txt
	-mkdir -p $(BUILD_DIR)
	@if [ "$(DEBUG)" = "" ]; then \
		echo "Release mode..."; \
		(cd $(BUILD_DIR) && cmake $(SRCDIR) -G 'Unix Makefiles') \
	else \
		echo "Debug mode..."; \
		(cd $(BUILD_DIR) && cmake $(SRCDIR) -DDEBUG=ON -G 'Unix Makefiles') \
	fi

ZION_LIBS=$(shell cd lib && find *.zion)
RUNTIME_C_FILES=$(shell find runtime -regex '.*\.c$$')

.PHONY: install
install: $(BUILT_BINARY) $(addprefix $(SRCDIR)/lib/,$(ZION_LIBS)) $(RUNTIME_C_FILES) $(SRCDIR)/$(PN).1 zion-tags
	-@echo "Installing Zion to ${DESTDIR}..."
	-@echo "Making sure that various installation dirs exist..." 
	@mkdir -p $(bindir)
	-@rm -rf $(stdlibdir)
	@mkdir -p $(stdlibdir)
	-@rm -rf $(man1dir)
	@mkdir -p $(man1dir)
	-@rm -rf $(runtimedir)
	@mkdir -p $(runtimedir)
	-@echo "Copying compiler binary from $(BUILT_BINARY) to $(bindir)..."
	@cp $(BUILT_BINARY) $(bindir)
	@cp ./zion-tags $(bindir)
	@for f in $(RUNTIME_C_FILES); do cp "$$f" "$(runtimedir)"; done
	@cp $(addprefix $(SRCDIR)/lib/,$(ZION_LIBS)) $(stdlibdir)
	@cp $(SRCDIR)/$(PN).1 $(man1dir)
	@cp $(SRCDIR)/$(PN).1 $(man1dir)

.PHONY: clean
clean:
	((cd $(BUILD_DIR)/.. && rm -rf $(notdir $(BUILD_DIR))) 2>/dev/null ||:)

.PHONY: install-test
install-test:
	-@rm -rf $(test_destdir)
	make DESTDIR=$(test_destdir) install

.PHONY: test
test:
	-@echo "Running Tests for Zion..."
	make $(BUILT_BINARY)
	make install-test
	@echo "ZION_ROOT=$(test_destdir)/$(prefix)/share/$(PN)"
	@ZION_ROOT="$(test_destdir)/$(prefix)/share/$(PN)" \
		"$(SRCDIR)/tests/run-tests.sh" \
			"$(test_destdir)/$(prefix)/bin" \
			"$(SRCDIR)" \
			"$(SRCDIR)/tests"

.PHONY: format
format:
	clang-format -style=file -i src/*.cpp src/*.h

