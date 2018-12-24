#include <set>
#include <map>
#include <string>
#include "identifier.h"
#include "ast.h"


std::string prefix(const std::set<std::string> &bindings, std::string pre, std::string name);
identifier_t prefix(const std::set<std::string> &bindings, std::string pre, identifier_t name);
token_t prefix(const std::set<std::string> &bindings, std::string pre, token_t name);
bitter::expr_t *prefix(const std::set<std::string> &bindings, std::string pre, bitter::expr_t *value);
bitter::predicate_t *prefix(
		const std::set<std::string> &bindings,
	   	std::string pre,
	   	bitter::predicate_t *predicate,
	   	std::set<std::string> &new_symbols);
bitter::pattern_block_t *prefix(const std::set<std::string> &bindings, std::string pre, bitter::pattern_block_t *pattern_block);
bitter::decl_t *prefix(const std::set<std::string> &bindings, std::string pre, bitter::decl_t *value);
bitter::type_decl_t prefix(const std::set<std::string> &bindings, std::string pre, const bitter::type_decl_t &type_decl);
bitter::type_class_t *prefix(const std::set<std::string> &bindings, std::string pre, bitter::type_class_t *type_class);


types::type_t::ref prefix(const std::set<std::string> &bindings, std::string pre, types::type_t::ref type);
bitter::expr_t *prefix(const std::set<std::string> &bindings, std::string pre, bitter::expr_t *value);
std::vector<bitter::expr_t *> prefix(const std::set<std::string> &bindings, std::string pre, std::vector<bitter::expr_t *> values);
bitter::module_t *prefix(const std::set<std::string> &bindings, bitter::module_t *module);

template <typename T>
std::vector<T> prefix(const std::set<std::string> &bindings, std::string pre, const std::vector<T> &things) {
	std::vector<T> new_things;
	for (T pb : things) {
		new_things.push_back(::prefix(bindings, pre, pb));
	}
	return new_things;
}

template <typename T>
std::map<std::string, T> prefix(
		const std::set<std::string> &bindings,
	   	std::string pre,
	   	const std::map<std::string, T> &map)
{
	std::map<std::string, T> new_map;
	for (auto pair : map) {
		new_map[pair.first] = prefix(bindings, pre, pair.second);
	}
	return new_map;
}

template <typename T>
std::set<T> prefix(
		const std::set<std::string> &bindings,
	   	std::string pre,
	   	const std::set<T> &set)
{
	std::set<T> new_set;
	for (auto s : set) {
		new_set.insert(prefix(bindings, pre, s));
	}
	return new_set;
}
