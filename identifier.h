#pragma once
#include <string>
#include "utils.h"
#include "location.h"

/* the abstract notion of an identifer */
struct identifier {
	typedef ptr<const identifier> ref;

	virtual ~identifier() {}
	virtual atom get_name() const = 0;
	virtual ptr<location> get_location() const = 0;
	virtual std::string str() const = 0;
};

/* internal identifiers - note that they lack a "location" */
struct iid : public identifier {
	typedef ptr<iid> ref;

	atom name;

	iid(atom name) : name(name) {}
	iid() = delete;
	iid(const iid &) = delete;
	iid(const iid &&) = delete;
	iid &operator =(const iid &) = delete;

	virtual atom get_name() const;
	virtual ptr<location> get_location() const;
	virtual std::string str() const;
};

