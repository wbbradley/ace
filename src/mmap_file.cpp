#include "mmap_file.h"
#include <unistd.h>
#ifndef ANDROID
#include <sys/fcntl.h>
#endif
#include <sys/mman.h>
#include <sys/errno.h>
#include "disk.h"
#include "logger_decls.h"
#include "assert.h"

mmap_file_t::mmap_file_t(const std::string &filename) {
	addr = MAP_FAILED;
	errno = 0;
	len = 0;
	fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0) {
		check_errno("negative file descriptor");
	} else {
		len = size_t(lseek(fd, 0, SEEK_END));
		if (len == size_t(-1)) {
			check_errno("no data");
		}
		addr = mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0 /*offset*/);
		if (addr == MAP_FAILED) {
			close(fd);
			fd = -1;
		}
	}
}

bool mmap_file_t::valid() const {
	return (addr != MAP_FAILED) && (len > 0);
}

mmap_file_t::~mmap_file_t() {
	if (addr != MAP_FAILED) {
		assert(addr != nullptr);
		if (munmap(addr, len) < 0) {
			check_errno("unmap");
		}
	}
	if (fd >= 0) {
		if (close(fd) < 0) {
			check_errno("close");
		}
	}
}

