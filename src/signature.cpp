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
		assert(name.size());
	}

	bool signature::operator !() const {
		/* signatures must have a name */
		return name.size() == 0;
	}

	signature &signature::operator =(const signature &rhs) {
		not_impl();
		return *this;
	}

	bool signature::operator ==(const signature &rhs) const {
		return (name == rhs.name);
	}

	const std::string &signature::repr() const {
		assert(name.size());
		return name;
	}

	const char *signature::c_str() const {
		return name.c_str();
	}

	std::string signature::str() const {
		std::stringstream ss;
		ss << C_SIG << repr() << C_RESET;
		return ss.str();
	}

	bool signature::operator <(const types::signature &rhs) const {
		return repr() < rhs.repr();
	}

	bool signature::operator !=(const types::signature &rhs) const {
		return !(*this == rhs);
	}
}

std::ostream &operator <<(std::ostream &os, const types::signature &signature) {
	return os << signature.str();
}
