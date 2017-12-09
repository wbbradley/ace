#include "zion.h"
#include <sstream>
#include "dbg.h"
#include "identifier.h"

std::string iid::get_name() const {
	return name;
}

location_t iid::get_location() const {
	return location;
}

std::string iid::str() const {
	return string_format(c_id("%s"), name.c_str());
}

identifier::ref make_iid_impl(std::string name, location_t location) {
	return make_ptr<iid>(name, location);
}

identifier::ref make_iid_impl(const char *name, location_t location) {
	return make_ptr<iid>(std::string{name}, location);
}

std::string str(identifier::refs ids) {
	return std::string("[") + join(ids, ", ") + "]";
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

std::set<std::string> to_atom_set(const identifier::refs &refs) {
	std::set<std::string> set;
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
