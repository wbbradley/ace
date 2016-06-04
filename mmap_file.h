#pragma once
#include <string>

struct mmap_file_t
{
    mmap_file_t(const mmap_file_t &) = delete;
    mmap_file_t &operator =(const mmap_file_t &) = delete;
    mmap_file_t() = delete;
    mmap_file_t(const std::string &filename);
    bool valid() const;
    ~mmap_file_t();

    int fd;
    void *addr;
    size_t len;
};


