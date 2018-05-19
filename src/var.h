#pragma once
#include "zion.h"
#include "types.h"
#include <list>
#include "user_error.h"

struct scope_t;
struct unification_t;

struct var_t {
    virtual ~var_t() throw() {}

    typedef ptr<const var_t> ref;
    typedef std::list<ref> refs;

	virtual types::type_t::ref get_type(ptr<scope_t> scope) const = 0;
	virtual location_t get_location() const = 0;
	virtual std::string str() const = 0;
    virtual std::string get_name() const = 0;

	unification_t accepts_callsite(
		   	ptr<scope_t> scope,
		   	types::type_t::ref args,
			types::type_t::ref return_type) const;
};

std::string str(const var_t::refs &vars);
