#pragma once
#include <string>
#include "utils.h"
#include "location.h"
#include <vector>
#include <set>
#include <list>

/* the abstract notion of an identifer */
struct identifier {
	typedef ptr<const identifier> ref;
	typedef std::vector<ref> refs;
	typedef std::set<ref, shared_comparator> set;

	virtual ~identifier() {}
	virtual atom get_name() const = 0;
	virtual location_t get_location() const = 0;
	virtual std::string str() const = 0;

	bool operator <(const identifier &rhs) const;
};

/* internal identifiers - note that they lack a "location" */
struct iid : public identifier {
	typedef ptr<iid> ref;

	atom name;
	location_t location;

	iid(atom name, location_t location) : name(name), location(location) {}
	iid() = delete;
	iid(const iid &) = delete;
	iid(const iid &&) = delete;
	iid &operator =(const iid &) = delete;

	virtual atom get_name() const;
	virtual location_t get_location() const;
	virtual std::string str() const;
};

std::string str(identifier::refs ids);
identifier::ref make_iid_impl(atom name, location_t location);
identifier::ref make_iid_impl(const char *name, location_t location);

identifier::set to_set(identifier::refs identifiers);

#define make_iid(name_) make_iid_impl(name_, location_t{__FILE__, __LINE__, 1})

namespace std {
	template <>
	struct hash<identifier::ref> {
		int operator ()(identifier::ref s) const {
			return std::hash<atom>()(s->get_name());
		}
	};
}

atom::set to_atom_set(const identifier::refs &refs);
identifier::set to_identifier_set(const identifier::refs &refs);
identifier::ref reduce_ids(std::list<identifier::ref> ids, location_t location);
