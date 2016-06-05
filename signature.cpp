#include "zion.h"
#include "dbg.h"
#include "signature.h"
#include <sstream>

namespace types {
	signature::signature(const char *name) : name(name) {
		assert(!!name);
	}

	signature::signature(const atom name) :
		name(name)
	{
		assert(!!name);
	}

	signature::signature(const many &args) :
		args(args)
	{
		assert(args.size() != 0);
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
		if (name != rhs.name) {
			return false;
		}

		if (args.size() != rhs.args.size()) {
			return false;
		}

		for (size_t i=0; i < args.size(); ++i) {
			if (args[i] != rhs.args[i]) {
				return false;
			}
		}

		return true;
	}


	atom signature::repr() const {
		if (!signature_cache) {
			std::stringstream ss;
			ss << name;
			if (args.size() != 0) {
				ss << "{";
			}

			const char *sep = "";
			for (const auto &arg : args) {
				ss << sep << arg.repr();
				sep = ", ";
			}

			if (args.size() != 0) {
				ss << "}";
			}

			signature_cache = atom{ss.str()};
		}
		return signature_cache;
	}

	std::string signature::str() const {
		std::stringstream ss;
		ss << C_SIG << repr().str() << C_RESET;
		return ss.str();
	}

	std::string str(const signature::many &args) {
		std::stringstream ss;
		ss << "[";
		const char *sep = "";
		for (const auto &arg : args) {
			ss << sep << arg.str();
			sep = ", ";
		}
		ss << "]";
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
