#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "utils.h"
#include "identifier.h"
#include "token.h"
#include "defn_id.h"
struct env_t;

extern const char *NULL_TYPE;
extern const char *STD_MAP_TYPE;
extern const char *VOID_TYPE;
extern const char *BOTTOM_TYPE;


/* used to reset the generic type id counter */
void reset_generics();

using env_ref_t = const env_t &;

namespace types {
	typedef std::map<std::string, int> name_index_t;
	typedef std::map<std::string, std::set<std::string>> predicate_map;

	struct signature;
	struct scheme_t;

	struct type_t : public std::enable_shared_from_this<type_t> {
		typedef std::shared_ptr<const type_t> ref;
		typedef std::vector<ref> refs;
		typedef std::map<std::string, ref> map;
        typedef std::pair<ref, ref> pair;

		virtual ~type_t() {}
		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const = 0;

		/* how many free type variables exist in this type? NB: Assumes you have
         * already bound existing bindings at the callsite prior to this check. */
		virtual int ftv_count() const = 0;

        /* NB: Also assumes you have rebound the bindings at the callsite. */
		virtual predicate_map get_predicate_map() const = 0;

		std::shared_ptr<scheme_t> generalize(const types::predicate_map &pm) const;
		std::string repr(const map &bindings) const;
		std::string repr() const { return this->repr({}); }

		virtual location_t get_location() const = 0;

		std::string str() const;
		std::string str(const map &bindings) const;
		std::string get_signature() const { return repr(); }

		virtual type_t::ref rebind(const map &bindings) const = 0;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const = 0;
		virtual type_t::ref prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const = 0;
		virtual type_t::ref apply(ref type) const;

		virtual int get_precedence() const { return 10; }
	};

	struct type_variable_t : public type_t {
		type_variable_t(identifier_t id, std::set<std::string> predicates);
		type_variable_t(identifier_t id);
		type_variable_t(location_t location /* auto-generated fresh type variables */);
		identifier_t id;
		std::set<std::string> predicates;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual predicate_map get_predicate_map() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual type_t::ref prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const;
		virtual location_t get_location() const;
	};

	struct type_predicate_t : public type_t {
		type_predicate_t(identifier_t id, type_t::ref type);
		identifier_t id;
		type_t::ref type;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual predicate_map get_predicate_map() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual type_t::ref prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const;
		virtual location_t get_location() const;
	};

	struct type_id_t : public type_t {
		type_id_t(identifier_t id);
		identifier_t id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual predicate_map get_predicate_map() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual type_t::ref prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const;
		virtual location_t get_location() const;
	};

	struct type_operator_t : public type_t {
		typedef std::shared_ptr<const type_operator_t> ref;

		type_operator_t(type_t::ref oper, type_t::ref operand);
		type_t::ref oper;
		type_t::ref operand;

		virtual int get_precedence() const { return 7; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual predicate_map get_predicate_map() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual type_t::ref prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const;
		virtual location_t get_location() const;
	};

	struct type_integer_t : public type_t {
		typedef std::shared_ptr<const type_integer_t> ref;
		type_integer_t(type_t::ref bit_size, type_t::ref signed_);
		type_t::ref bit_size;
		type_t::ref signed_;

		virtual int get_precedence() const { return 9; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual predicate_map get_predicate_map() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual type_t::ref prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const;
		virtual location_t get_location() const;
	};

	struct type_tuple_t : public type_t {
		typedef std::shared_ptr<const type_tuple_t> ref;

		type_tuple_t(type_t::refs dimensions);

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual predicate_map get_predicate_map() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual type_t::ref prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const;
		virtual location_t get_location() const;

		type_t::refs dimensions;
	};

	struct type_lambda_t : public type_t {
		type_lambda_t(identifier_t binding, type_t::ref body);
		identifier_t binding;
		type_t::ref body;

		virtual int get_precedence() const { return 6; }
		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual predicate_map get_predicate_map() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual type_t::ref prefix_ids(const std::set<std::string> &bindings, const std::string &pre) const;
		virtual type_t::ref apply(types::type_t::ref type) const;
		virtual location_t get_location() const;
	};

	struct scheme_t : public std::enable_shared_from_this<scheme_t> {
		typedef std::shared_ptr<scheme_t> ref;
		typedef std::vector<ref> refs;
		typedef std::map<std::string, ref> map;

		scheme_t(std::vector<std::string> vars, const predicate_map &predicates, types::type_t::ref type) : vars(vars), predicates(predicates), type(type) {}
		types::type_t::ref instantiate(location_t location);
		scheme_t::ref rebind(const types::type_t::map &env);
		scheme_t::ref normalize();

		/* count of the bounded type variables */
		int btvs() const;

		predicate_map get_predicate_map();
		std::string str();
		location_t get_location() const;

		std::vector<std::string> vars;
		predicate_map predicates;
		types::type_t::ref type;
	};

	bool is_type_id(type_t::ref type, const std::string &type_name);
	type_t::refs rebind(const type_t::refs &types, const type_t::map &bindings);
};

typedef std::map<std::string, types::type_t::map> data_ctors_map_t;
typedef std::unordered_map<const bitter::expr_t *, types::type_t::ref> tracked_types_t;
typedef std::unordered_map<std::string, int> ctor_id_map_t;
struct defn_ref_t {
	location_t location;
	defn_id_t from_defn_id;
};

typedef std::map<defn_id_t, std::vector<defn_ref_t>> needed_defns_t;
void insert_needed_defn(needed_defns_t &needed_defns, const defn_id_t &defn_id, location_t location, const defn_id_t &from_defn_id);

identifier_t gensym(location_t location);

/* type data ctors */
types::type_t::ref type_bottom();
types::type_t::ref type_bool(location_t location);
types::type_t::ref type_bool(location_t location);
types::type_t::ref type_string(location_t location);
types::type_t::ref type_int(location_t location);
types::type_t::ref type_unit(location_t location);
types::type_t::ref type_null(location_t location);
types::type_t::ref type_void(location_t location);
types::type_t::ref type_map(types::type_t::ref a, types::type_t::ref b);
types::type_t::ref type_arrow(location_t location, types::type_t::ref a, types::type_t::ref b);
types::type_t::ref type_arrow(types::type_t::ref a, types::type_t::ref b);
types::type_t::ref type_arrows(types::type_t::refs types, int offset=0);
types::type_t::ref type_integer(types::type_t::ref size, types::type_t::ref is_signed);
types::type_t::ref type_id(identifier_t var);
types::type_t::ref type_variable(identifier_t id, const std::set<std::string> &predicates);
types::type_t::ref type_variable(identifier_t name);
types::type_t::ref type_variable(location_t location);
types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand);
types::type_t::ref type_operator(const types::type_t::refs &xs);
types::type_t::ref type_deref(types::type_t::ref type);
types::scheme_t::ref scheme(std::vector<std::string> vars, const types::predicate_map &predicates, types::type_t::ref type);
types::type_tuple_t::ref type_tuple(types::type_t::refs dimensions);
types::type_t::ref type_ptr(types::type_t::ref raw);
types::type_t::ref type_lambda(identifier_t binding, types::type_t::ref body);
types::type_t::ref type_vector_type(types::type_t::ref element);
types::type_t::ref type_tuple_accessor(int i, int max, const std::vector<std::string> &vars);

std::string str(types::type_t::refs refs);
std::string str(const types::type_t::map &coll);
std::string str(const types::predicate_map &pm);
std::string str(const data_ctors_map_t &data_ctors_map);
std::ostream& operator <<(std::ostream &out, const types::type_t::ref &type);

types::type_t::ref tuple_deref_type(location_t location, types::type_t::ref tuple, int index);
void unfold_binops_rassoc(std::string id, types::type_t::ref t, types::type_t::refs &unfolding);
void unfold_ops_lassoc(types::type_t::ref t, types::type_t::refs &unfolding);
void mutating_merge(const types::predicate_map::value_type &pair, types::predicate_map &c);
void mutating_merge(const types::predicate_map &a, types::predicate_map &c);
types::predicate_map merge(const types::predicate_map &a, const types::predicate_map &b);
types::predicate_map safe_merge(const types::predicate_map &a, const types::predicate_map &b);

std::ostream &join_dimensions(std::ostream &os, const types::type_t::refs &dimensions, const types::name_index_t &name_index, const types::type_t::map &bindings);
std::string get_name_from_index(const types::name_index_t &name_index, int i);
bool is_valid_udt_initial_char(int ch);
