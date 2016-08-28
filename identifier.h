#pragma once
#include <string>
#include "utils.h"
#include "location.h"
#include <vector>

/* the abstract notion of an identifer */
struct identifier {
	typedef ptr<const identifier> ref;
	typedef std::vector<ptr<const identifier>> refs;

	virtual ~identifier() {}
	virtual atom get_name() const = 0;
	virtual location get_location() const = 0;
	virtual std::string str() const = 0;
};

/* internal identifiers - note that they lack a "location" */
struct iid : public identifier {
	typedef ptr<iid> ref;

	atom name;
	struct location location;

	iid(atom name, struct location location) : name(name), location(location) {}
	iid() = delete;
	iid(const iid &) = delete;
	iid(const iid &&) = delete;
	iid &operator =(const iid &) = delete;

	virtual atom get_name() const;
	virtual struct location get_location() const;
	virtual std::string str() const;
};

identifier::ref make_iid_impl(atom name, struct location location);
identifier::ref make_iid_impl(const char *name, struct location location);

#define make_iid(name) make_iid_impl(name, location{__FILE__, __LINE__, 1})

