#include "zion.h"
#include <sstream>
#include "dbg.h"
#include "identifier.h"

atom iid::get_name() const {
	return name;
}

struct location iid::get_location() const {
	return location;
}

std::string iid::str() const {
	return string_format(c_id("%s"), name.c_str());
}

identifier::ref make_iid_impl(atom name, struct location location) {
	return make_ptr<iid>(name, location);
}

identifier::ref make_iid_impl(const char *name, struct location location) {
	return make_ptr<iid>(atom{name}, location);
}
