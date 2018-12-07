#pragma once
#include "var.h"

struct checked_var_t : public var_t {
	typedef std::shared_ptr<checked_var_t> ref;
	virtual ~checked_var_t() throw() {}
	checked_var_t() = delete;
	checked_var_t(types::type_t::ref type, identifier::ref id) :
		type(type),
		id(id)
	{}

	types::type_t::ref get_type(std::shared_ptr<scope_t> scope) const override;
	types::type_t::ref get_type() const override;
	location_t get_location() const override;
	std::string str() const override;
    std::string get_name() const override;
	identifier::ref get_id() const override;

private:
	location_t internal_location;
	types::type_t::ref const type;
	identifier::ref const id;
};

checked_var_t::ref make_checked_var(types::type_t::ref type, identifier::ref id);
checked_var_t::ref make_checked_var(identifier::ref id, types::type_t::ref type);
