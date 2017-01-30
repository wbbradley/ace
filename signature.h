#pragma once
#include "atom.h"
#include <vector>
#include <string>
#include <ostream>
#include "status.h"

namespace types {
	/* a signature is a name for a type. */
	struct signature {
		const atom name;

		signature(const signature &sig);
		signature(const char *name);
		signature(const atom name);
		signature &operator =(const signature &rhs);

		bool operator ==(const signature &rhs) const;
		bool operator <(const signature &rhs) const;
		bool operator !=(const signature &rhs) const;
		bool operator !() const;

		std::string str() const;
		atom repr() const;
	};
}

namespace std {
	template <>
		struct hash<types::signature> {
		int operator ()(const types::signature &s) const {
			return std::hash<atom>()(s.repr());
		}
	};
}

std::ostream &operator <<(std::ostream &os, const types::signature &signature);

types::signature sig(std::string input);
types::signature operator "" _s(const char *value, size_t);
