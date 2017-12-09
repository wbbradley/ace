#include "zion.h"
#include "dbg.h"
#include "signature.h"
#include <sstream>

namespace types {
	signature::signature(const signature &sig) : signature(sig.name) {
	}

	signature::signature(const char *name) : name(name) {
		assert(!!name);
	}

	signature::signature(const std::string name) :
		name(name)
	{
		assert(!!name);
	}

	bool signature::operator !() const {
		/* signatures must have a name */
		return !name;
	}

	signature &signature::operator =(const signature &rhs) {
		not_impl();
		return *this;
	}

	bool signature::operator ==(const signature &rhs) const {
		return (name == rhs.name);
	}


	std::string signature::repr() const {
		assert(!!name);
		return name;
	}

	std::string signature::str() const {
		std::stringstream ss;
		ss << C_SIG << repr().str() << C_RESET;
		return ss.str();
	}
}

bool types::signature::operator <(const types::signature &rhs) const {
	return repr() < rhs.repr();
}

bool types::signature::operator !=(const types::signature &rhs) const {
	return !(*this == rhs);
}

std::ostream &operator <<(std::ostream &os, const types::signature &signature) {
	return os << signature.str();
}
