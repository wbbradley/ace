LLVM_DIR=$(HOME)/opt/llvm/release_40/MinSizeRel

zion:
	(cd $(HOME)/var/zion && \
		make -j8 2>&1)

clean:
	(export LLVM_DIR=$(LLVM_DIR) ; \
		export CPLUS_INCLUDE_PATH=$(LLVM_DIR)/include/c++/v1 ; \
		cd $(HOME)/var && \
		rm -rf zion && \
		mkdir -p zion && \
		cd zion && \
		cmake $(HOME)/src/zion)
