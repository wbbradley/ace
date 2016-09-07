#include "zion.h"
#include "dbg.h"
#include "location.h"
#include "utils.h"
#include <string>
#include <sstream>

std::string location::str(bool vim_mode) const {
	std::stringstream ss;
	if (has_file_location()) {
		ss << C_LINE_REF;
		if (starts_with(filename, "./")) {
			auto str = filename.c_str();
			ss << (str + 2);
		} else {
			ss << filename;
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

std::string location::repr() const {
	return clean_ansi_escapes(str());
}

std::string location::operator()() const {
	return str();
}

std::ostream &operator <<(std::ostream &os, const location &location) {
	return os << location.str();
}

bool location::has_file_location() const {
	return filename.size() != 0 && line != -1 && col != -1;
}
