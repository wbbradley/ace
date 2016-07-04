#include "zion.h"
#include <sstream>
#include "dbg.h"
#include "identifier.h"

atom iid::get_name() const {
	return name;
}

ptr<location> iid::get_location() const {
	return nullptr;
}

std::string iid::str() const {
	return string_format(c_id("%s"), name.c_str());
}
