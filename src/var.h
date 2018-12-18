#pragma once
#include "zion.h"
#include "types.h"
#include <list>
#include "user_error.h"

struct scope_t;
struct unification_t;
struct bound_type_t;
struct delegate_t;

struct var_t {
    virtual ~var_t() throw() {}

	typedef std::shared_ptr<const var_t> ref;
	typedef std::vector<ref> refs;
	typedef std::map<std::string, ref> overloads;
	typedef std::weak_ptr<var_t> weak_ref;
	typedef std::map<std::string, overloads> map;

	virtual types::type_t::ref get_type(std::shared_ptr<scope_t> scope) const = 0;
	virtual types::type_t::ref get_type() const = 0;
	virtual location_t get_location() const = 0;
	virtual std::string str() const = 0;
    virtual std::string get_name() const = 0;
	virtual identifier_t get_id() const = 0;

	unification_t accepts_callsite(
		   	std::shared_ptr<scope_t> scope,
		   	types::type_t::ref args,
			types::type_t::ref return_type) const;
};

std::string str(const var_t::refs &vars);
types::type_args_t::ref get_args_type(var_t::refs args);
std::vector<std::shared_ptr<const bound_type_t>> get_bound_types(delegate_t &delegate, std::shared_ptr<scope_t> scope, var_t::refs values);
