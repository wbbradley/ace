#pragma once
#include "zion.h"
#include "dbg.h"
#include "status.h"
#include "utils.h"
#include <string>
#include <map>
#include "ast_decls.h"
#include <unordered_map>
#include "bound_var.h"

struct unchecked_type_t {
	unchecked_type_t() = delete;
	unchecked_type_t(const unchecked_type_t &) = delete;
	unchecked_type_t(
			atom name,
			ptr<const ast::item_t> node,
			ptr<scope_t> const module_scope);

	atom const name;
	ptr<const ast::item_t> const node;
	ptr<scope_t> const module_scope;

	std::string str() const;

	typedef ptr<const unchecked_type_t> ref;
	typedef std::vector<ref> refs;
	typedef std::map<atom, ref> map;

	static ref create(
			atom name,
			ptr<const ast::item_t> node,
			ptr<scope_t> const module_scope);
};
