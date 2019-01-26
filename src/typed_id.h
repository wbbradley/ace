#include <string>
#include "identifier.h"
#include "types.h"

struct typed_id_t {
	typed_id_t(
			identifier_t id,
			types::type_t::ref type) :
		id(id),
		type(type)
	{}
	identifier_t const id;
	types::type_t::ref const type;

private:
	mutable std::string cached_repr;

public:
	std::string repr() const;
	bool operator <(const typed_id_t &rhs) const;
};

std::ostream &operator <<(std::ostream &os, const typed_id_t &typed_id);
