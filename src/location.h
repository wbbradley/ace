#pragma once
#include <ostream>

#define INTERNAL_LOC() ::location_t{__FILE__, __LINE__, 1}

struct location_t {
	template <typename T>
	location_t(T t) = delete;

	location_t() : line(-1), col(-1) {}
	location_t(std::string filename, int line, int col) : filename(filename), line(line), col(col) {}

	std::string str() const;
	std::string repr() const;
	std::string operator()() const;
	std::string filename_repr() const;

	std::string filename;
	int line;
	int col;
	bool has_file_location() const;
    bool operator <(const location_t &rhs) const;
    bool operator ==(const location_t &rhs) const;
    bool operator !=(const location_t &rhs) const;
};

std::ostream &operator <<(std::ostream &os, const location_t &location);
location_t best_location(location_t a, location_t b);
