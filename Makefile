BUILD_DIR ?= $(HOME)/var/zion
ZION=$(BUILD_DIR)/zion
SRC_DIR=$(shell pwd)
LLVM_DIR ?= $(shell llvm-config --prefix)/share/llvm/cmake

.PHONY: zion
zion: $(BUILD_DIR)/build.ninja
	-rm zion 2>/dev/null
	ninja -j8 -C $(BUILD_DIR) zion
	ln -s $(ZION) zion

clean:
	(cd $(BUILD_DIR)/.. && rm -rf $(notdir $(BUILD_DIR))) 2>/dev/null
	-rm zion 2>/dev/null

$(BUILD_DIR)/build.ninja: $(LLVM_DIR)/LLVMConfig.cmake CMakeLists.txt
	-mkdir -p $(BUILD_DIR)
	(cd $(BUILD_DIR) && cmake $(SRC_DIR) -G Ninja)

test:
	make zion
	CTEST_OUTPUT_ON_FAILURE=TRUE ninja -j8 -C $(BUILD_DIR) test

.PHONY: format
format:
	clang-format -style=file -i src/*.cpp src/*.h

