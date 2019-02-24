ZION=$(HOME)/var/zion/zion

zion:
	mkdir -p $(HOME)/var/zion
	(cd $(HOME)/var/zion && \
		make -j8 2>&1)

clean: $(LLVM_DIR)/LLVMConfig.cmake
	(cd $(HOME)/var && \
		rm -rf zion && \
		mkdir -p zion && \
		cd zion && \
		cmake $(HOME)/src/zion)

clean-zion: clean
	make zion

clean-test: clean-zion
	make test

test: zion
	$(ZION) compile test_basic
