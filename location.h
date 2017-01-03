#pragma once
#include "atom.h"
#include <ostream>

#define INTERNAL_LOC() ::location{__FILE__, __LINE__, 1}

struct location {
	template <typename T>
	location(T t) = delete;

	location() : line(-1), col(-1) {}
	location(atom filename, int line, int col) : filename(filename), line(line), col(col) {}

	std::string str(bool vim_mode=false) const;
	std::string repr() const;
	std::string operator()() const;

	atom filename;
	int line;
	int col;
	bool has_file_location() const;
    bool operator ==(const location &rhs) const;
};

std::ostream &operator <<(std::ostream &os, const location &location);
