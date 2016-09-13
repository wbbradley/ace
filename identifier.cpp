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

identifier::set to_set(identifier::refs identifiers) {
	identifier::set set;
	std::for_each(
			identifiers.begin(),
			identifiers.end(),
			[&] (identifier::ref x) {
				set.insert(x);
			});
	return set;
}

atom::set to_atom_set(const identifier::refs &refs) {
	atom::set set;
	for (auto ref : refs) {
		set.insert(ref->get_name());
	}
	return set;
}

identifier::set to_identifier_set(const identifier::refs &refs) {
	identifier::set set;
	std::for_each(
			refs.begin(),
			refs.end(),
			[&] (const identifier::ref &x) {
				set.insert(x);
			});
	return set;
}

bool identifier::operator <(const identifier &rhs) const {
	return get_name().str() < rhs.get_name().str();
}
