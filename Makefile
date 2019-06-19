PN = zion
BUILD_DIR ?= $(HOME)/var/$(PN)
SRCDIR = $(shell pwd)
LLVM_DIR ?= $(shell llvm-config --prefix)/share/llvm/cmake
prefix ?= /usr/local
BUILT_BINARY = $(BUILD_DIR)/zion

# Installation-related directories
installdir = $(DESTDIR)/$(prefix)
bindir = $(DESTDIR)/$(prefix)/bin
stdlibdir = $(DESTDIR)/$(prefix)/share/$(PN)/lib
man1dir = $(DESTDIR)/$(prefix)/share/man/man1
runtimedir = $(DESTDIR)/$(prefix)/share/$(PN)/runtime

test_destdir ?= $(HOME)/var/zion-test

.PHONY: $(BUILT_BINARY)
$(BUILT_BINARY): $(BUILD_DIR)/build.ninja
	@(cd $(BUILD_DIR) && ninja -j8 $(PN))

$(BUILD_DIR)/build.ninja: $(LLVM_DIR)/LLVMConfig.cmake CMakeLists.txt
	-mkdir -p $(BUILD_DIR)
	if [ "$(DEBUG)" == "" ]; then \
		echo "Release mode..."; \
		(cd $(BUILD_DIR) && cmake $(SRCDIR) -G Ninja) \
	else \
		echo "Debug mode..."; \
		(cd $(BUILD_DIR) && cmake $(SRCDIR) -DDEBUG=1 -G Ninja) \
	fi

ZION_LIBS=\
		  std.zion \
		  itertools.zion \
		  complex.zion \

.PHONY: install
install: $(BUILT_BINARY) $(addprefix $(SRCDIR)/lib/,$(ZION_LIBS)) $(SRCDIR)/src/zion_rt.c $(SRCDIR)/$(PN).1
	-echo "Making sure that various installation dirs exist..." 
	mkdir -p $(bindir)
	mkdir -p $(stdlibdir)
	mkdir -p $(man1dir)
	mkdir -p $(runtimedir)
	-echo "Copying compiler binary from $(BUILT_BINARY) to $(bindir)..."
	cp $(BUILT_BINARY) $(bindir)
	cp $(addprefix $(SRCDIR)/lib/,$(ZION_LIBS)) $(stdlibdir)
	cp $(SRCDIR)/$(PN).1 $(man1dir)
	cp $(SRCDIR)/src/zion_rt.c $(runtimedir)
	find $(installdir) -type f 2>/dev/null | grep -E '\bzion\b'

.PHONY: clean
clean:
	(cd $(BUILD_DIR)/.. && rm -rf $(notdir $(BUILD_DIR))) 2>/dev/null

@PHONY: install-test
install-test:
	-@rm -rf $(test_destdir)
	make DESTDIR=$(test_destdir) install

.PHONY: test
test:
	make $(BUILT_BINARY)
	make install-test
	ZION_PATH=$(test_destdir)/$(prefix)/share/$(PN)/lib \
	  $(SRCDIR)/tests/run-tests.sh \
		  $(test_destdir)/$(prefix)/bin \
		  $(SRCDIR) \
		  $(SRCDIR)/tests

.PHONY: format
format:
	clang-format -style=file -i src/*.cpp src/*.h

