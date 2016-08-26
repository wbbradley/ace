#pragma once
#include <string>
#include <unordered_set>
#include <vector>

struct atom {
	typedef std::unordered_set<atom> set;
	typedef std::vector<atom> many;

	atom() : iatom(0) {}
	atom(std::string &&str);
	atom(const std::string &str);
	atom(const char *str);

	atom &operator =(std::string &&rhs);
	atom &operator =(const std::string &rhs);
	atom &operator =(const char *rhs);
	atom &operator =(const atom &);

	bool operator ==(int) const = delete;
	bool operator ==(const atom rhs) const { return iatom == rhs.iatom; }
	bool operator !=(const atom rhs) const { return iatom != rhs.iatom; }
	bool operator  <(const atom rhs) const { return iatom  < rhs.iatom; }
	bool operator ! () const               { return iatom == 0; }
	atom operator + (const atom rhs) const;

	const char *c_str() const;
	const std::string str() const;
	size_t size() const;
	bool is_generic_type_alias() const;

	int iatom;

private:
	std::string value;
};

namespace std {
	template <>
	struct hash<atom> {
		int operator ()(atom s) const {
			return 1301081 * s.iatom;
		}
	};
}

inline std::ostream &operator <<(std::ostream &os, atom value) {
	return os << value.str();
}

inline std::string operator +(const std::string &lhs, const atom rhs) {
	return lhs + rhs.str();
}

atom get_indexed_generic(int generic_index);
bool starts_with(atom atom_str, const std::string &search);

atom::set to_set(atom::many atoms);
