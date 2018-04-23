#include "zion.h"
#include "dbg.h"
#include "atom.h"
#include <iostream>
#include <vector>
#include <map>
#include "utils.h"

struct atom {
	typedef std::set<atom> set;
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

bool starts_with(atom atom_str, const std::string &search);

atom::set to_set(atom::many atoms);

static std::map<std::string, int> atom_str_index = {{"", 0}};
std::vector<std::string> atoms = {""};

int atomize(std::string &&str) {
	auto iter = atom_str_index.find(str);
	if (iter != atom_str_index.end()) {
		return iter->second;
	} else {
		int iatom = atoms.size();
		atom_str_index[str] = iatom;
		// log("atomizing %s as %d", str.c_str(), iatom);
		atoms.push_back(std::move(str));
		return iatom;
	}
}

int atomize(const std::string &str) {
	auto iter = atom_str_index.find(str);
	if (iter != atom_str_index.end()) {
		return iter->second;
	} else {
		int iatom = atoms.size();
		atom_str_index[str] = iatom;
		atoms.push_back(str);
		// log("atomizing %s as %d", str.c_str(), iatom);
		return iatom;
	}
}

int atomize(const char *str) {
	auto iter = atom_str_index.find(str);
	if (iter != atom_str_index.end()) {
		return iter->second;
	} else {
		int iatom = atoms.size();

		/* if our atoms grow too large, we'll need to change how we pack CtorID's */
		assert(iatom < (1 << 30));

		atom_str_index[str] = iatom;
		atoms.push_back(str);
		// log("atomizing %s as %d", str, iatom);
		return iatom;
	}
}

atom::atom(std::string &&str) : iatom(atomize(str)) {
	value = str;
}

atom::atom(const std::string &str) : iatom(atomize(str)) {
	value = str;
}

atom::atom(const char *str) : iatom(atomize(str)) {
	value = str;
}


atom &atom::operator =(const atom &rhs) {
	iatom = rhs.iatom;
	value = rhs.value;
	return *this;
}

atom &atom::operator =(std::string &&rhs) {
	iatom = atomize(rhs);
	value = str();
	return *this;
}

atom &atom::operator =(const std::string &rhs) {
	iatom = atomize(rhs);
	value = str();
	return *this;
}

atom &atom::operator =(const char *rhs) {
	iatom = atomize(rhs);
	value = str();
	return *this;
}

atom atom::operator + (const atom rhs) const {
	return {str() + rhs.str()};
}

const char *atom::c_str() const {
	return atoms[iatom].c_str();
}

const std::string atom::str() const {
	return atoms[iatom];
}

size_t atom::size() const {
	return atoms[iatom].size();
}

bool atom::is_generic_type_alias() const {
	const auto &val = str();
	return starts_with(val, "any ") || val == "any";
}

atom::set to_set(atom::many atoms) {
	atom::set set;
	std::for_each(
			atoms.begin(),
			atoms.end(),
			[&] (atom x) {
				set.insert(x);
			});
	return set;
}

void dump_atoms() {
	int i = 0;
	for (auto a: atoms) {
		std::cerr << i++ << ": " << a << std::endl;
	}
}
