#include "zion.h"
#include "dbg.h"
#include "atom.h"
#include <vector>
#include <unordered_map>
#include "utils.h"

static std::unordered_map<std::string, int> atom_str_index = {{"", 0}};
static std::vector<std::string> atoms = {""};

int memoize_atom(std::string &&str) {
	auto iter = atom_str_index.find(str);
	if (iter != atom_str_index.end()) {
		return iter->second;
	} else {
		int iatom = atoms.size();
		atom_str_index[str] = iatom;
		atoms.push_back(std::move(str));
		return iatom;
	}
}

int memoize_atom(const std::string &str) {
	auto iter = atom_str_index.find(str);
	if (iter != atom_str_index.end()) {
		return iter->second;
	} else {
		int iatom = atoms.size();
		atom_str_index[str] = iatom;
		atoms.push_back(str);
		return iatom;
	}
}

int memoize_atom(const char *str) {
	auto iter = atom_str_index.find(str);
	if (iter != atom_str_index.end()) {
		return iter->second;
	} else {
		int iatom = atoms.size();
		atom_str_index[str] = iatom;
		atoms.push_back(str);
		return iatom;
	}
}

atom::atom(std::string &&str) : iatom(memoize_atom(str)) {
	value = str;
}

atom::atom(const std::string &str) : iatom(memoize_atom(str)) {
	value = str;
}

atom::atom(const char *str) : iatom(memoize_atom(str)) {
	value = str;
}


atom &atom::operator =(const atom &rhs) {
	iatom = rhs.iatom;
	value = rhs.value;
	return *this;
}

atom &atom::operator =(std::string &&rhs) {
	iatom = memoize_atom(rhs);
	value = str();
	return *this;
}

atom &atom::operator =(const std::string &rhs) {
	iatom = memoize_atom(rhs);
	value = str();
	return *this;
}

atom &atom::operator =(const char *rhs) {
	iatom = memoize_atom(rhs);
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

atom get_indexed_generic(int generic_index) {
	return {string_format("any _%d", generic_index)};
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
