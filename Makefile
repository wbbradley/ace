LLVM_DIR=/usr/local/Cellar/llvm@6/6.0.0
ZION=$(HOME)/var/zion/zion

zion:
	mkdir -p $(HOME)/var/zion
	(cd $(HOME)/var/zion && \
		make 2>&1)

clean:
	(export LLVM_DIR=$(LLVM_DIR) ; \
		(cd $(HOME)/var && \
			rm -rf zion && \
			mkdir -p zion && \
			cd zion && \
			cmake $(HOME)/src/zion))

clean-zion: clean
	make zion

clean-test: clean-zion
	make test

test: zion
	$(ZION) compile test_basic
