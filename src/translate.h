#pragma once
#include "ast_decls.h"
#include "types.h"
#include "defn_id.h"
#include <list>
#include <unordered_set>

struct translation_t {
	typedef std::shared_ptr<translation_t> ref;
	translation_t(
			bitter::expr_t *expr,
			const std::unordered_map<bitter::expr_t *, types::type_t::ref> &typing) :
		expr(expr),
		typing(typing)
	{}

	bitter::expr_t * const expr;
	std::unordered_map<bitter::expr_t *, types::type_t::ref> const typing;

	std::string str() const;
	location_t get_location() const;
};

struct translation_env_t {
	translation_env_t(
			std::shared_ptr<std::unordered_map<bitter::expr_t *, types::type_t::ref>> tracked_types,
			const data_ctors_map_t &data_ctors_map) :
		tracked_types(tracked_types),
		data_ctors_map(data_ctors_map)
	{}

	std::shared_ptr<std::unordered_map<bitter::expr_t *, types::type_t::ref>> tracked_types;
	const data_ctors_map_t &data_ctors_map;

	types::type_t::ref get_type(bitter::expr_t *e) const;
	std::map<std::string, types::type_t::refs> get_data_ctors_terms(types::type_t::ref type) const;
	types::type_t::refs get_data_ctor_terms(types::type_t::ref type, identifier_t ctor_id) const;
	types::type_t::refs get_fresh_data_ctor_terms(identifier_t ctor_id) const;
};

translation_t::ref translate(
		const defn_id_t &for_defn_id,
		bitter::expr_t *expr,
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		std::set<defn_id_t> &needed_defns);
bitter::expr_t *texpr(
		const defn_id_t &for_defn_id,
		bitter::expr_t *expr,
		const std::unordered_set<std::string> &bound_vars,
		const translation_env_t &tenv,
		std::unordered_map<bitter::expr_t *, types::type_t::ref> &typing,
		std::set<defn_id_t> &needed_defns);
