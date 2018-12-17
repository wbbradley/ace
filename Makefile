# LLVM_DIR=$(HOME)/opt/llvm/release_40/MinSizeRel
LLVM_DIR=/usr/local/Cellar/llvm@4/4.0.1
ZION=$(HOME)/var/zion/zion

zion:
	mkdir -p $(HOME)/var/zion
	(cd $(HOME)/var/zion && \
		make 2>&1)

clean:
	(export LLVM_DIR=$(LLVM_DIR) ; \
		export CPLUS_INCLUDE_PATH=$(LLVM_DIR)/include/c++/v1 ; \
		cd $(HOME)/var && \
		rm -rf zion && \
		mkdir -p zion && \
		cd zion && \
		cmake $(HOME)/src/zion)

clean-zion: clean
	make zion

clean-test: clean-zion
	make test

test:
	$(ZION) test
	./expect-tests.sh $(HOME)/var/zion
