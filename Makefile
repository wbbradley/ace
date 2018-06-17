LLVM_DIR=$(HOME)/opt/llvm/release_40/MinSizeRel
ZION=$(HOME)/var/zion/zion

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

clean-zion: clean
	make zion

test: clean-zion
	$(ZION) test
	./expect-tests.sh $(HOME)/var/zion

docker-test:
	(cd /opt/zion && \
		cmake . && \
		make -j8 zion && \
		./zion test && \
		./expect-tests.sh /opt/zion)
