#include "checked_var.h"

types::type_t::ref checked_var_t::get_type(std::shared_ptr<scope_t> scope) const {
	return type;
}

types::type_t::ref checked_var_t::get_type() const {
	return type;
}

location_t checked_var_t::get_location() const {
	return id->get_location();
}

std::string checked_var_t::str() const {
	std::stringstream ss;
	ss << id->str() << " : " << type->str();
	return ss.str();
}

std::string checked_var_t::get_name() const {
	return id->get_name();
}

identifier::ref checked_var_t::get_id() const {
	return id;
}

checked_var_t::ref make_checked_var(types::type_t::ref type, identifier::ref id) {
	return std::make_shared<checked_var_t>(type, id);
}

checked_var_t::ref make_checked_var(identifier::ref id, types::type_t::ref type) {
	return std::make_shared<checked_var_t>(type, id);
}
