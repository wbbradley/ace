#include "zion.h"
#include <sstream>
#include "dbg.h"
#include "identifier.h"

std::string identifier_t::str() const {
	std::stringstream ss;
	ss << C_ID << "`" << id.str() << "`" << C_RESET;
	ss << location.str();
	return ss.str();
}


