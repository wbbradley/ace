BUILD_DIR ?= $(HOME)/var/zion
ZION=$(BUILD_DIR)/zion
SRC_DIR=$(shell pwd)
LLVM_DIR ?= $(shell llvm-config --prefix)/share/llvm/cmake

.PHONY: zion
zion: $(BUILD_DIR)/build.ninja
	-@rm zion 2>/dev/null
	@(cd $(BUILD_DIR) && ninja -j8 zion)
	@ln -s $(ZION) zion

clean:
	(cd $(BUILD_DIR)/.. && rm -rf $(notdir $(BUILD_DIR))) 2>/dev/null
	-rm zion 2>/dev/null

$(BUILD_DIR)/build.ninja: $(LLVM_DIR)/LLVMConfig.cmake CMakeLists.txt
	-mkdir -p $(BUILD_DIR)
	(cd $(BUILD_DIR) && cmake $(SRC_DIR) -G Ninja)

.PHONY: test
test:
	time make zion
	time $(SRC_DIR)/tests/run-tests.sh $(BUILD_DIR) $(SRC_DIR) $(SRC_DIR)/tests

.PHONY: format
format:
	clang-format -style=file -i src/*.cpp src/*.h

