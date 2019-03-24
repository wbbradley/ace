ZION=$(HOME)/var/zion/zion

.PHONY: zion
zion: $(HOME)/var/zion/build.ninja
	-((rm zion 2>&1) > /dev/null; true)
	ninja -C $(HOME)/var/zion zion
	ln -s $(ZION) zion

clean:
	(cd $(HOME)/var && rm -rf zion)
	-rm zion

$(HOME)/var/zion/build.ninja: $(LLVM_DIR)/LLVMConfig.cmake CMakeLists.txt
	-mkdir -p $(HOME)/var/zion
	(cd $(HOME)/var/zion && cmake $(HOME)/src/zion -G Ninja)

test: $(ZION)
	$(ZION) compile test_basic
