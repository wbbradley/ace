#include "zion.h"
#include "dbg.h"
#include "location.h"
#include "utils.h"
#include <string>
#include <sstream>
#include <unistd.h>

std::string location_t::str(bool vim_mode, bool make_dir_relative) const {
	static char *cwd = (char *)calloc(4096, 1);
	static unsigned cwdlen = 0;
	if (cwd[0] == 0) {
		getcwd(cwd, 4096);
		cwdlen = strlen(cwd);
	}

	std::stringstream ss;
	if (has_file_location()) {
		ss << C_LINE_REF;
		if (starts_with(filename, "./")) {
			auto str = filename.c_str();
			ss << (str + 2);
		} else {
			if (starts_with(filename, cwd) && filename.size() > cwdlen) {
				ss << filename.c_str() + strlen(cwd) + 1;
			} else {
				ss << filename;
			}
		}
		if (vim_mode) {
			ss << ':' << line << ':' << col;
		} else {
			ss << '(' << line << ')';
		}
		ss << C_RESET;
	} else {
		ss << C_LINE_REF << "builtin" << C_RESET;
	}
	return ss.str();
}

std::string location_t::repr() const {
	return clean_ansi_escapes(str());
}

std::string location_t::operator()() const {
	return str();
}

std::ostream &operator <<(std::ostream &os, const location_t &location) {
	return os << location.str();
}

bool location_t::operator ==(const location_t &rhs) const {
    return filename == rhs.filename && line == rhs.line && col == rhs.col;
}

bool location_t::has_file_location() const {
	return filename.size() != 0 && line != -1 && col != -1;
}
